#pragma once
#include <Windows.h>
#include <mmreg.h>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

namespace awareness::sound {
struct Clip {
    WAVEFORMATEX format{};
    std::vector<std::uint8_t> pcm;
};
// Caller initializes COM and Media Foundation. Only local files are accepted.
HRESULT Decode(const std::wstring &path, Clip &output, const std::atomic_bool *stopping = nullptr) noexcept;
Clip Builtin();
enum class Backend { XAudio2, WaveOut };
class Player {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit Player(Backend backend = Backend::XAudio2);
    ~Player();
    Player(const Player &) = delete;
    Player &operator=(const Player &) = delete;
    void Configure(bool enabled, const char *path, float volume);
    void Trigger(unsigned count = 1);
    void Preview();
    std::string Status();
    void Browse();
    bool TakePickedPath(std::string &path);
};
} // namespace awareness::sound
