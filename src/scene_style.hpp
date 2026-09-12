#pragma once
#include <awareness/OverlayApi.hpp>
#include <algorithm>
#include <array>
#include <cmath>
namespace awareness::scene {
struct Options {
    std::uint32_t enabled{};
    float brightness{1};
    Color tint{1, 1, 1, 1};
};
inline bool Valid(const Options &s) noexcept {
    const auto unit = [](float x) { return std::isfinite(x) && x >= 0 && x <= 1; };
    return s.enabled <= 1 && std::isfinite(s.brightness) && s.brightness >= 0 && s.brightness <= 2 && unit(s.tint.r) &&
           unit(s.tint.g) && unit(s.tint.b) && unit(s.tint.a);
}
inline std::uint64_t Gains(const Options &s) noexcept {
    if (!s.enabled || !Valid(s))
        return 0;
    const auto gain = [&](float c) { return std::uint64_t(std::lround(c * s.brightness * 4096)); };
    return (1ull << 63) | gain(s.tint.r) | (gain(s.tint.g) << 16) | (gain(s.tint.b) << 32);
}
inline std::array<std::uint8_t, 4> Tint(std::array<std::uint8_t, 4> c, std::uint64_t gains) noexcept {
    if (!(gains >> 63))
        return c;
    for (int i = 0; i < 3; ++i)
        c[i] =
            static_cast<std::uint8_t>(std::min(255u, (c[i] * unsigned((gains >> (i * 16)) & 0xffff) + 2048u) / 4096u));
    return c;
}
} // namespace awareness::scene
