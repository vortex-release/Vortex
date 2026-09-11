#include "kill_sound.hpp"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <xaudio2.h>
#include <mmsystem.h>
#include <wrl/client.h>
#include <commdlg.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>
#include <utility>

namespace awareness::sound {
using Microsoft::WRL::ComPtr;
HRESULT Decode(const std::wstring &path, Clip &output, const std::atomic_bool *stopping) noexcept {
    output = {};
    try {
        WIN32_FILE_ATTRIBUTE_DATA file{};
        if (path.empty() || path.starts_with(L"\\\\") ||
            !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &file) ||
            (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        if (file.nFileSizeHigh || file.nFileSizeLow > 64 * 1024 * 1024)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        ComPtr<IMFSourceReader> reader;
        HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
        if (FAILED(hr))
            return hr;
        if (FAILED(hr = reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE)))
            return hr;
        ComPtr<IMFMediaType> type;
        if (FAILED(hr = MFCreateMediaType(&type)) || FAILED(hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
            FAILED(hr = type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) ||
            FAILED(hr = type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)) ||
            FAILED(hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr,
                                                    type.Get())) ||
            FAILED(hr = reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE)))
            return hr;
        type.Reset();
        if (FAILED(hr = reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &type)))
            return hr;
        UINT32 channels{}, rate{}, bits{};
        if (FAILED(type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels)) ||
            FAILED(type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate)) ||
            FAILED(type->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits)) || channels < 1 || channels > 2 || bits != 16 ||
            rate < 8000 || rate > 192000)
            return MF_E_INVALIDMEDIATYPE;
        Clip clip;
        clip.format = {WAVE_FORMAT_PCM,
                       static_cast<WORD>(channels),
                       rate,
                       rate * channels * 2,
                       static_cast<WORD>(channels * 2),
                       16,
                       0};
        const std::size_t limit = static_cast<std::size_t>(clip.format.nAvgBytesPerSec) * 10;
        for (unsigned reads = 0; reads < 100000; ++reads) {
            if (stopping && stopping->load())
                return E_ABORT;
            DWORD flags{};
            ComPtr<IMFSample> sample;
            if (FAILED(hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr,
                                               &flags, nullptr, &sample)))
                return hr;
            if (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED))
                return MF_E_INVALIDMEDIATYPE;
            if (sample) {
                ComPtr<IMFMediaBuffer> buffer;
                if (FAILED(hr = sample->ConvertToContiguousBuffer(&buffer)))
                    return hr;
                BYTE *bytes{};
                DWORD length{};
                if (FAILED(hr = buffer->Lock(&bytes, nullptr, &length)))
                    return hr;
                struct Unlock {
                    IMFMediaBuffer *buffer;
                    ~Unlock() { buffer->Unlock(); }
                } unlock{buffer.Get()};
                if (length > limit - clip.pcm.size())
                    return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
                clip.pcm.insert(clip.pcm.end(), bytes, bytes + length);
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                if (clip.pcm.empty() || clip.pcm.size() % clip.format.nBlockAlign)
                    return MF_E_INVALIDMEDIATYPE;
                output = std::move(clip);
                return S_OK;
            }
        }
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    } catch (...) {
        return E_FAIL;
    }
}
Clip Builtin() {
    Clip clip;
    clip.format = {WAVE_FORMAT_PCM, 1, 48000, 96000, 2, 16, 0};
    clip.pcm.resize(14400);
    for (std::size_t i = 0; i < clip.pcm.size() / 2; ++i) {
        const double t = static_cast<double>(i) / 48000.;
        const double envelope = std::min(t / .005, 1.) * std::exp(-t * 28.);
        const auto value = static_cast<std::int16_t>(
            5500. * envelope *
            (std::sin(t * 2 * 3.141592653589793 * 1500) + .35 * std::sin(t * 2 * 3.141592653589793 * 2300)));
        std::memcpy(clip.pcm.data() + i * 2, &value, 2);
    }
    return clip;
}
struct WaveAudio {
    struct Voice {
        HWAVEOUT device{};
        WAVEHDR header{};
        std::vector<std::uint8_t> pcm;
    };
    std::array<Voice, 8> voices;
    unsigned next{};
    void Clear() noexcept {
        for (auto &v : voices)
            if (v.device) {
                waveOutReset(v.device);
                if (v.header.dwFlags & WHDR_PREPARED)
                    waveOutUnprepareHeader(v.device, &v.header, sizeof(v.header));
                waveOutClose(v.device);
                v.device = nullptr;
                v.header = {};
                v.pcm.clear();
            }
    }
    ~WaveAudio() { Clear(); }
    HRESULT Play(const Clip &clip, float volume) {
        auto &v = voices[next++ % voices.size()];
        if (v.device) {
            waveOutReset(v.device);
            if (v.header.dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(v.device, &v.header, sizeof(v.header));
        } else if (waveOutOpen(&v.device, WAVE_MAPPER, &clip.format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
            return E_FAIL;
        v.pcm = clip.pcm;
        // Scale this voice's samples; never change the system or game's audio volume.
        for (std::size_t i = 0; i + 1 < v.pcm.size(); i += 2) {
            std::int16_t sample{};
            std::memcpy(&sample, v.pcm.data() + i, 2);
            sample = static_cast<std::int16_t>(sample * volume);
            std::memcpy(v.pcm.data() + i, &sample, 2);
        }
        v.header = {};
        v.header.lpData = reinterpret_cast<LPSTR>(v.pcm.data());
        v.header.dwBufferLength = static_cast<DWORD>(v.pcm.size());
        if (waveOutPrepareHeader(v.device, &v.header, sizeof(v.header)) != MMSYSERR_NOERROR ||
            waveOutWrite(v.device, &v.header, sizeof(v.header)) != MMSYSERR_NOERROR)
            return E_FAIL;
        return S_OK;
    }
};
struct Player::Impl {
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic_bool stop{};
    bool enabled{}, preview{};
    unsigned pending{};
    std::uint64_t revision{1};
    float volume{.7f};
    std::string path, status;
    std::atomic<HWND> dialog{};
    std::future<std::string> picker;
    Backend backend;
    std::thread worker;
    explicit Impl(Backend selected) : backend(selected), worker([this] { Run(); }) {}
    ~Impl() {
        stop = true;
        wake.notify_all();
        if (auto window = dialog.load())
            PostMessageW(window, WM_CLOSE, 0, 0);
        if (picker.valid())
            picker.wait();
        if (worker.joinable())
            worker.join();
    }
    static UINT_PTR CALLBACK PickerHook(HWND window, UINT message, WPARAM, LPARAM data) {
        if (message == WM_INITDIALOG) {
            auto self = reinterpret_cast<Impl *>(reinterpret_cast<OPENFILENAMEW *>(data)->lCustData);
            self->dialog = GetParent(window);
            if (self->stop)
                PostMessageW(self->dialog, WM_CLOSE, 0, 0);
        }
        return 0;
    }
    std::string Pick() {
        const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        struct Scope {
            bool active;
            ~Scope() {
                if (active)
                    CoUninitialize();
            }
        } scope{SUCCEEDED(com)};
        wchar_t file[2048]{};
        OPENFILENAMEW ofn{sizeof(ofn)};
        ofn.lpstrFile = file;
        ofn.nMaxFile = 2048;
        ofn.lpstrTitle = backend == Backend::WaveOut ? L"Choose hit sound" : L"Choose kill sound";
        ofn.lpstrFilter = L"Audio\0*.wav;*.mp3;*.m4a;*.aac;*.wma;*.flac\0All files\0*.*\0";
        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ENABLEHOOK;
        ofn.lpfnHook = PickerHook;
        ofn.lCustData = reinterpret_cast<LPARAM>(this);
        const bool selected = !stop && GetOpenFileNameW(&ofn);
        dialog = nullptr;
        if (!selected)
            return {};
        char utf8[2048]{};
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, file, -1, utf8, 2048, nullptr, nullptr))
            return {};
        return utf8;
    }
    void Run() noexcept {
        const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const auto mf = SUCCEEDED(com) ? MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET) : com;
        struct Scope {
            HRESULT com, mf;
            ~Scope() {
                if (SUCCEEDED(mf))
                    MFShutdown();
                if (SUCCEEDED(com))
                    CoUninitialize();
            }
        } scope{com, mf};
        struct Audio {
            ComPtr<IXAudio2> engine;
            IXAudio2MasteringVoice *master{};
            std::array<IXAudio2SourceVoice *, 8> voices{};
            unsigned next{};
            void Clear() {
                for (auto &voice : voices)
                    if (voice) {
                        voice->DestroyVoice();
                        voice = nullptr;
                    }
            }
            ~Audio() {
                Clear();
                if (master)
                    master->DestroyVoice();
            }
        } audio;
        WaveAudio wave;
        Clip clip;
        std::string loaded = "\x01";
        std::uint64_t handled{};
        try {
            while (!stop) {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stop || revision != handled || pending || preview; });
                if (stop)
                    break;
                const auto current = revision;
                handled = current;
                const auto selected = path;
                const bool on = enabled, test = std::exchange(preview, false);
                const float gain = volume;
                unsigned count = std::exchange(pending, 0u) + (test ? 1u : 0u);
                lock.unlock();
                if (!on && !test) {
                    audio.Clear();
                    wave.Clear();
                    continue;
                }
                HRESULT hr = S_OK;
                if (selected != loaded || test) {
                    audio.Clear();
                    wave.Clear();
                    clip = {};
                    if (selected.empty())
                        clip = Builtin();
                    else if (FAILED(mf))
                        hr = mf;
                    else {
                        wchar_t wide[2048]{};
                        hr = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, selected.c_str(), -1, wide, 2048)
                                 ? Decode(wide, clip, &stop)
                                 : E_INVALIDARG;
                    }
                    loaded = selected;
                    lock.lock();
                    if (revision == current)
                        status = SUCCEEDED(hr) ? ""
                                 : hr == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)
                                     ? "Choose a sound under 10 seconds and 64 MB."
                                     : "Couldn't load this sound. Try a WAV or MP3 file.";
                    lock.unlock();
                }
                if (clip.pcm.empty())
                    continue;
                lock.lock();
                const bool currentConfig = current == revision && !stop;
                lock.unlock();
                if (!currentConfig)
                    continue;
                for (auto voice : audio.voices)
                    if (voice)
                        voice->SetVolume(gain);
                if (!count || gain <= 0)
                    continue;
                if (backend == Backend::WaveOut) {
                    for (unsigned i = 0; i < std::min(count, 8u); ++i)
                        if (FAILED(hr = wave.Play(clip, gain)))
                            break;
                    if (FAILED(hr)) {
                        std::scoped_lock guard(mutex);
                        status = "Audio output is unavailable.";
                    }
                    continue;
                }
                if (!audio.engine) {
                    hr = XAudio2Create(&audio.engine);
                    if (SUCCEEDED(hr))
                        hr = audio.engine->CreateMasteringVoice(&audio.master);
                    if (FAILED(hr)) {
                        audio.engine.Reset();
                        std::scoped_lock guard(mutex);
                        status = "Audio output is unavailable.";
                        continue;
                    }
                }
                count = (std::min)(count, 8u);
                for (unsigned i = 0; i < count; ++i) {
                    auto &voice = audio.voices[audio.next++ % audio.voices.size()];
                    if (voice) {
                        voice->Stop();
                        voice->FlushSourceBuffers();
                    } else
                        hr = audio.engine->CreateSourceVoice(&voice, &clip.format);
                    if (FAILED(hr) || !voice)
                        break;
                    voice->SetVolume(gain);
                    XAUDIO2_BUFFER buffer{};
                    buffer.Flags = XAUDIO2_END_OF_STREAM;
                    buffer.AudioBytes = static_cast<UINT32>(clip.pcm.size());
                    buffer.pAudioData = clip.pcm.data();
                    hr = voice->SubmitSourceBuffer(&buffer);
                    if (SUCCEEDED(hr))
                        hr = voice->Start();
                    if (FAILED(hr))
                        break;
                }
                if (FAILED(hr)) {
                    std::scoped_lock guard(mutex);
                    status = "Couldn't play this sound.";
                }
            }
        } catch (...) {
            std::scoped_lock lock(mutex);
            status = "Sound playback stopped.";
        }
        // Voices must stop referencing clip storage before it is destroyed.
        audio.Clear();
    }
};
Player::Player(Backend backend) : impl_(std::make_unique<Impl>(backend)) {}
Player::~Player() = default;
void Player::Configure(bool enabled, const char *path, float volume) {
    std::scoped_lock lock(impl_->mutex);
    if (impl_->enabled == enabled && impl_->path == path && impl_->volume == volume)
        return;
    if (impl_->path != path)
        impl_->status.clear();
    impl_->enabled = enabled;
    impl_->path = path;
    impl_->volume = std::clamp(volume, 0.f, 1.f);
    ++impl_->revision;
    if (!enabled)
        impl_->pending = 0;
    impl_->wake.notify_one();
}
void Player::Trigger(unsigned count) {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->enabled || impl_->volume <= 0)
        return;
    impl_->pending = (std::min)(8u, impl_->pending + (std::min)(8u, count));
    impl_->wake.notify_one();
}
void Player::Preview() {
    std::scoped_lock lock(impl_->mutex);
    impl_->preview = true;
    impl_->wake.notify_one();
}
std::string Player::Status() {
    std::scoped_lock lock(impl_->mutex);
    return impl_->status;
}
void Player::Browse() {
    if (!impl_->picker.valid())
        impl_->picker = std::async(std::launch::async, [this] { return impl_->Pick(); });
}
bool Player::TakePickedPath(std::string &path) {
    if (!impl_->picker.valid() || impl_->picker.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return false;
    try {
        path = impl_->picker.get();
        return !path.empty();
    } catch (...) {
        return false;
    }
}
} // namespace awareness::sound
