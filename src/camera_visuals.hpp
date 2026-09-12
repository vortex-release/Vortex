#pragma once
#include <awareness/Trajectories.hpp>
#include <algorithm>
#include <cmath>
namespace awareness::camera_visuals {
struct Options {
    std::uint32_t thirdPerson{}, whileScoped{}, removeRecoil{}, scopedFovEnabled{};
    float distance{100}, shoulder{}, height{8}, scopedFov{45};
    std::uint32_t viewmodelEnabled{}, hideScoped{};
    float viewmodelFov{68};
    Vector3 viewmodelOffset{};
};
inline bool Valid(const Options &s) noexcept {
    const auto range = [](float x, float a, float b) { return std::isfinite(x) && x >= a && x <= b; };
    return s.thirdPerson <= 1 && s.whileScoped <= 1 && s.removeRecoil <= 1 && s.scopedFovEnabled <= 1 &&
           s.viewmodelEnabled <= 1 && s.hideScoped <= 1 && range(s.viewmodelFov, 40, 120) &&
           range(s.viewmodelOffset.x, -10, 10) && range(s.viewmodelOffset.y, -10, 10) &&
           range(s.viewmodelOffset.z, -10, 10) && range(s.distance, 30, 200) && range(s.shoulder, -50, 50) &&
           range(s.height, -20, 40) && range(s.scopedFov, 10, 90);
}
inline bool FiniteVector(Vector3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline Vector3 Offset(Vector3 angles, const Options &s) noexcept {
    if (!Valid(s) || !FiniteVector(angles) || std::abs(angles.x) > 90)
        return {};
    const auto yaw = std::remainder(angles.y, 360.f) * .01745329252f, pitch = angles.x * .01745329252f;
    return {-std::cos(pitch) * std::cos(yaw) * s.distance - std::sin(yaw) * s.shoulder,
            -std::cos(pitch) * std::sin(yaw) * s.distance + std::cos(yaw) * s.shoulder,
            std::sin(pitch) * s.distance + s.height};
}
// Pull in immediately at a wall; interpolation must not carry the camera through it.
inline Vector3 Clipped(Vector3 start, Vector3 delta, float fraction, bool solid) noexcept {
    if (!FiniteVector(start))
        return {};
    if (solid || !FiniteVector(delta) || !std::isfinite(fraction) || fraction < 0 || fraction > 1)
        return start;
    const auto length = std::hypot(delta.x, delta.y, delta.z);
    if (!std::isfinite(length) || length <= 0)
        return start;
    const float t = fraction < 1 ? std::max(0.f, fraction - 2.f / length) : 1.f;
    return {start.x + delta.x * t, start.y + delta.y * t, start.z + delta.z * t};
}
inline float FrameFov(flight::Fov &smoother, float engine, bool enabled, float desired, bool scoped, const Options &s,
                      double now) noexcept {
    if (scoped) {
        smoother.Reset();
        return Valid(s) && s.scopedFovEnabled && std::isfinite(engine) && engine >= 1 && engine < 179 ? s.scopedFov
                                                                                                      : engine;
    }
    return smoother.Update(engine, enabled, desired, now);
}
// Applies to the current call's outputs only; engine state is regenerated next frame.
inline bool ViewmodelOutputs(const Options &s, bool scoped, Vector3 &offset, float &fov) noexcept {
    if (!Valid(s))
        return false;
    if (s.hideScoped && scoped) {
        offset = {0, 0, -64};
        fov = 1;
        return true;
    }
    if (!s.viewmodelEnabled)
        return false;
    offset = s.viewmodelOffset;
    fov = s.viewmodelFov;
    return true;
}
class PredictionCadence {
    double next_{}, previous_{};
    flight::Utility type_{flight::Utility::None};
    float strength_{-1};
    bool stationary_{};

  public:
    void Reset() noexcept {
        next_ = previous_ = 0;
        type_ = flight::Utility::None;
        strength_ = -1;
        stationary_ = false;
    }
    bool Due(double now, flight::Utility type, float strength, bool unchanged) noexcept {
        if (!std::isfinite(now) || now < 0 || !std::isfinite(strength))
            return false;
        const bool changed = type != type_ || std::abs(strength - strength_) > .01f || now < previous_;
        const bool resumed = stationary_ && !unchanged;
        previous_ = now;
        if (!changed && !resumed && now < next_)
            return false;
        // Only an actual stationary publication earns the immediate motion
        // refresh. Alternating unchanged/changed polls cannot bypass the cap.
        stationary_ = unchanged;
        type_ = type;
        strength_ = strength;
        next_ = now + (unchanged ? .1 : 1. / 30.);
        return true;
    }
};
} // namespace awareness::camera_visuals
