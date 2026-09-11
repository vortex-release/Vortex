#include "kill_sound.hpp"
#include <mfapi.h>
#include <xaudio2.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <thread>
#include <chrono>
using namespace awareness::sound;
static void Wave(const std::filesystem::path &path, const Clip &clip) {
    std::ofstream out(path, std::ios::binary);
    const auto put = [&]<typename T>(T v) { out.write(reinterpret_cast<const char *>(&v), sizeof(v)); };
    out.write("RIFF", 4);
    put(std::uint32_t(36 + clip.pcm.size()));
    out.write("WAVEfmt ", 8);
    put(std::uint32_t(16));
    put(clip.format.wFormatTag);
    put(clip.format.nChannels);
    put(clip.format.nSamplesPerSec);
    put(clip.format.nAvgBytesPerSec);
    put(clip.format.nBlockAlign);
    put(clip.format.wBitsPerSample);
    out.write("data", 4);
    put(std::uint32_t(clip.pcm.size()));
    out.write(reinterpret_cast<const char *>(clip.pcm.data()), clip.pcm.size());
}
int main(int argc, char **argv) {
    const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com))
        return 2;
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) {
        CoUninitialize();
        return 2;
    }
    unsigned checks{}, failures{};
    const auto check = [&](bool ok, const char *label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("FAIL: %s\n", label);
        }
    };
    const auto directory = std::filesystem::current_path() / L"sound-test";
    std::filesystem::create_directories(directory);
    const auto path = directory / L"\u97f3-kill.wav";
    auto clip = Builtin();
    Wave(path, clip);
    Clip decoded;
    const HRESULT decode = Decode(path.wstring(), decoded);
    check(SUCCEEDED(decode) && decoded.format.nSamplesPerSec == 48000 && decoded.format.nChannels == 1 &&
              decoded.pcm == clip.pcm,
          "Unicode WAV decodes to exact PCM");
    check(FAILED(Decode((directory / L"missing.wav").wstring(), decoded)) && decoded.pcm.empty(),
          "missing files clear the previous clip");
    {
        std::ofstream out(directory / L"invalid.mp3");
        out << "invalid";
    }
    check(FAILED(Decode((directory / L"invalid.mp3").wstring(), decoded)), "malformed audio is rejected");
    auto longClip = clip;
    longClip.pcm.resize(48000 * 2 * 11);
    Wave(directory / L"long.wav", longClip);
    check(Decode((directory / L"long.wav").wstring(), decoded) == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE),
          "long clips are rejected without retaining partial audio");
    std::atomic_bool stop{true};
    check(Decode(path.wstring(), decoded, &stop) == E_ABORT, "shutdown cancels decoding");
    Microsoft::WRL::ComPtr<IXAudio2> engine;
    IXAudio2MasteringVoice *master{};
    IXAudio2SourceVoice *voice{};
    HRESULT hr = XAudio2Create(&engine);
    if (SUCCEEDED(hr))
        hr = engine->CreateMasteringVoice(&master);
    if (SUCCEEDED(hr))
        hr = engine->CreateSourceVoice(&voice, &clip.format);
    check(SUCCEEDED(hr), "XAudio2 creates the sound output graph");
    if (voice) {
        voice->SetVolume(0); // Unattended tests must not produce audible sound.
        // Leave EOS clear so SamplesPlayed is not reset at the end of the buffer.
        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(clip.pcm.size());
        buffer.pAudioData = clip.pcm.data();
        check(SUCCEEDED(voice->SubmitSourceBuffer(&buffer)) && SUCCEEDED(voice->Start()),
              "sound buffer submitted and started");
        XAUDIO2_VOICE_STATE state{};
        const auto deadline = GetTickCount64() + 3000;
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            voice->GetState(&state);
        } while (state.BuffersQueued && GetTickCount64() < deadline);
        check(!state.BuffersQueued && state.SamplesPlayed == clip.pcm.size() / clip.format.nBlockAlign,
              "audio device consumed every sample");
        voice->DestroyVoice();
    }
    if (master)
        master->DestroyVoice();
    engine.Reset();
    {
        Player player;
        const auto missing = (directory / L"missing.wav").u8string();
        player.Configure(true, reinterpret_cast<const char *>(missing.c_str()), 0);
        player.Preview();
        const auto deadline = GetTickCount64() + 3000;
        while (player.Status().empty() && GetTickCount64() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(!player.Status().empty(), "asynchronous load errors reach the UI");
        player.Configure(false, "", 0);
        player.Preview();
        check(player.Status().empty(), "changing back to built-in clears old errors");
    }
    {
        Clip silent = Builtin();
        std::fill(silent.pcm.begin(), silent.pcm.end(), std::uint8_t{0});
        const auto silence = directory / L"silent.wav";
        Wave(silence, silent);
        Player hit(Backend::WaveOut);
        const auto utf8 = silence.u8string();
        hit.Configure(true, reinterpret_cast<const char *>(utf8.c_str()), 1);
        hit.Trigger(2);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        check(hit.Status().empty(), "Windows Multimedia hit player accepts silent WAV and overlapping voices");
        hit.Configure(false, "", 0);
        std::filesystem::remove(silence);
    }
    if (argc == 2) {
        const auto extra = std::filesystem::absolute(argv[1]);
        check(SUCCEEDED(Decode(extra.wstring(), decoded)) && !decoded.pcm.empty(),
              "additional compressed audio fixture decodes");
    }
    std::filesystem::remove(path);
    std::filesystem::remove(directory / L"invalid.mp3");
    std::filesystem::remove(directory / L"long.wav");
    std::filesystem::remove(directory);
    MFShutdown();
    CoUninitialize();
    std::printf("%u audio checks; %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
