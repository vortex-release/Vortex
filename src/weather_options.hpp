#pragma once
#include <cstdint>

namespace awareness::weather {
struct Options {
    std::uint32_t enabled{};
    std::uint32_t kind{};     // Rain, snow, ash.
    std::uint32_t density{1}; // One, two or three native emitters.
};
inline bool Valid(const Options &s) noexcept {
    return s.enabled <= 1 && s.kind < 3 && s.density < 3;
}
enum class Status : std::uint32_t { Disabled, WaitingForScene, Preparing, Active, Retrying, Unsupported, Stopping };
inline const char *Label(Status s) noexcept {
    switch (s) {
    case Status::Disabled:
        return "Off";
    case Status::WaitingForScene:
        return "Waiting for scene";
    case Status::Preparing:
        return "Preparing weather";
    case Status::Active:
        return "Active";
    case Status::Retrying:
        return "Effect unavailable; retrying";
    case Status::Unsupported:
        return "Game build not supported";
    case Status::Stopping:
        return "Stopping";
    }
    return "Unavailable";
}
struct Diagnostics {
    Status status{Status::Disabled};
    std::uint32_t active{}, created{}, retired{}, failures{};
};
} // namespace awareness::weather
