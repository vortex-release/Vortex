#pragma once
#include <algorithm>
#include <cmath>
namespace awareness {
inline float MotionStep(float value, float goal, float seconds, bool animated = true, float speed = 18.f) noexcept {
    if (!animated || !std::isfinite(value))
        return goal;
    if (!std::isfinite(seconds) || seconds <= 0)
        return value;
    const float next = value + (goal - value) * (1.f - std::exp(-speed * std::clamp(seconds, 0.f, .1f)));
    return std::abs(next - goal) < .001f ? goal : next;
}
class MenuMotion {
    float progress_{};

  public:
    float Update(bool open, float seconds, bool animated = true) noexcept {
        if (!animated)
            progress_ = open ? 1.f : 0.f;
        else if (std::isfinite(seconds))
            progress_ = std::clamp(progress_ + (open ? 1.f : -1.f) * std::clamp(seconds, 0.f, .25f) / .18f, 0.f, 1.f);
        return progress_ * progress_ * (3.f - 2.f * progress_);
    }
    bool Visible() const noexcept { return progress_ > 0; }
};
} // namespace awareness
