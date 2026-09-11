#pragma once
#include <awareness/EffectsApi.hpp>
#include <algorithm>
#include <cmath>
namespace awareness::styling {
struct Player {
    std::uint32_t gradientFill{};
    float cornerLength{.25f}, boxRounding{}, boxGlow{};
    Color fillTop{.35f, .18f, .75f, 0}, fillBottom{.55f, .35f, 1, 1};
    float tintBrightness{1}, tintSaturation{1}, haloPulse{}, pulseSpeed{1};
};
struct Sky {
    std::uint32_t enabled{};
    float brightness{1};
    Color tint{1, 1, 1, 1};
};
inline bool ColorValid(Color c) {
    for (float v : {c.r, c.g, c.b, c.a})
        if (!std::isfinite(v) || v < 0 || v > 1)
            return false;
    return true;
}
inline bool Valid(const Player &p) {
    const auto range = [](float v, float a, float b) { return std::isfinite(v) && v >= a && v <= b; };
    return p.gradientFill <= 1 && range(p.cornerLength, .08f, .5f) && range(p.boxRounding, 0, 12) &&
           range(p.boxGlow, 0, 1) && ColorValid(p.fillTop) && ColorValid(p.fillBottom) &&
           range(p.tintBrightness, 0, 2) && range(p.tintSaturation, 0, 2) && range(p.haloPulse, 0, .9f) &&
           range(p.pulseSpeed, .1f, 3);
}
inline bool Valid(const Sky &s) {
    return s.enabled <= 1 && std::isfinite(s.brightness) && s.brightness >= 0 && s.brightness <= 3 &&
           ColorValid(s.tint);
}
inline Color Tint(Color c, const Player &p) {
    const float grey = c.r * .2126f + c.g * .7152f + c.b * .0722f;
    c.r = std::clamp((grey + (c.r - grey) * p.tintSaturation) * p.tintBrightness, 0.f, 1.f);
    c.g = std::clamp((grey + (c.g - grey) * p.tintSaturation) * p.tintBrightness, 0.f, 1.f);
    c.b = std::clamp((grey + (c.b - grey) * p.tintSaturation) * p.tintBrightness, 0.f, 1.f);
    return c;
}
inline EffectsConfiguration Tint(EffectsConfiguration e, const Player &p) {
    e.materialColor = Tint(e.materialColor, p);
    e.glowColor = Tint(e.glowColor, p);
    return e;
}
inline float Pulse(const Player &p, double now) {
    if (!std::isfinite(now) || p.haloPulse <= 0)
        return 1;
    return 1 - p.haloPulse *
                   (.5f + .5f * static_cast<float>(std::sin(std::remainder(now * p.pulseSpeed, 1.) * 6.28318530718)));
}
} // namespace awareness::styling
