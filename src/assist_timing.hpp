#pragma once
#include "assist_features.hpp"
namespace awareness::assist {
// The recoil runtime stops consuming punch changes 300 ms after a new shot.
// A historical nonzero shot count is not evidence that it still owns the camera.
class RecoilActivity {
    std::uint32_t owner_{}, weapon_{}, shots_{};
    double previous_{}, until_{};
    bool initialized_{};

  public:
    void Reset() noexcept { *this = {}; }
    bool Update(const Sample &s, double now, bool enabled) noexcept {
        if (!enabled || !s.valid || !s.weaponKnown || !s.owner || !s.weaponHandle || s.shots > 300 ||
            !std::isfinite(now) || now < 0) {
            Reset();
            return false;
        }
        if (!initialized_ || owner_ != s.owner || weapon_ != s.weaponHandle || s.shots < shots_ || now < previous_ ||
            now - previous_ > .15) {
            owner_ = s.owner;
            weapon_ = s.weaponHandle;
            shots_ = s.shots;
            previous_ = now;
            until_ = 0;
            initialized_ = true;
            return false;
        }
        if (s.shots > shots_)
            until_ = now + .3;
        shots_ = s.shots;
        previous_ = now;
        return until_ > 0 && now <= until_;
    }
};
inline unsigned PollIntervalMs(bool active, bool sampleValid, const Options &options, const Keys &keys) noexcept {
    // This remains scheduled Windows polling, not a simulation/subtick callback.
    // Only an armed held jump requests the shorter wait; no busy spin is used.
    return !active || !sampleValid ? 20u : options.jumper && keys.Held(Space) ? 1u : 4u;
}
} // namespace awareness::assist
