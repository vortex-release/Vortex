#pragma once
#include <awareness/OverlayApi.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace awareness::assist {
inline constexpr unsigned LeftMouse = 1, Mouse4 = 5, Mouse5 = 6, Space = 32, A = 65, D = 68, S = 83, W = 87;
inline constexpr std::array<unsigned, 5> ShootKeys{Mouse4, Mouse5, 164, 160, 162};
struct Options {
    std::uint32_t shoot{}, shootKey{Mouse4}, scopeOnly{}, jumper{}, strafer{}, cappedAcceleration{1};
    float delayMs{35}, intervalMs{100}, pressMs{20}, turnRate{360}, minSpeed{80};
    float airAcceleration{12}, airSpeedCap{30}, tickRate{64};
};
inline bool Valid(const Options &o) noexcept {
    const auto range = [](float v, float a, float b) { return std::isfinite(v) && v >= a && v <= b; };
    return o.shoot <= 1 && o.scopeOnly <= 1 && o.jumper <= 1 && o.strafer <= 1 && o.cappedAcceleration <= 1 &&
           std::find(ShootKeys.begin(), ShootKeys.end(), o.shootKey) != ShootKeys.end() && range(o.delayMs, 0, 300) &&
           range(o.intervalMs, 40, 600) && range(o.pressMs, 8, 40) && range(o.turnRate, 30, 720) &&
           range(o.minSpeed, 10, 400) && range(o.airAcceleration, 1, 200) && range(o.airSpeedCap, 1, 100) &&
           range(o.tickRate, 30, 128);
}
struct Keys {
    bool textInput{};
    std::array<bool, 256> down{};
    bool Held(unsigned key) const noexcept { return key < down.size() && down[key]; }
};
struct Sample {
    bool valid{}, grounded{}, walking{}, frozen{}, scoped{}, enemy{}, weaponReady{};
    std::uint32_t owner{}, weaponHandle{}, target{}, tick{}, flags{};
    std::uint16_t weapon{};
    std::uint8_t moveType{};
    Vector3 velocity{};
    float water{}, maxSpeed{250}, friction{1}, yaw{}, pitch{};
};
inline bool Movable(const Sample &s) noexcept {
    return s.valid && s.walking && !s.frozen && std::isfinite(s.water) && s.water < .1f;
}
inline bool IsAirborne(const Sample &s) noexcept {
    return Movable(s) && !s.grounded;
}
inline bool CrosshairEnemy(const Sample &s) noexcept {
    return s.valid && s.enemy && s.target != 0;
}
inline float NormalizeYaw(float yaw) noexcept {
    if (!std::isfinite(yaw))
        return 0;
    return std::remainder(yaw, 360.f);
}
struct StrafePlan {
    bool active{};
    float yaw{}, forwardMove{}, sideMove{}, predictedSpeed{};
};
inline float PredictedSpeed(const Sample &s, float yaw, int side, const Options &o) noexcept {
    constexpr float radians = .017453292519943295f;
    const float wishYaw = (yaw + side * 90.f) * radians;
    const float x = std::cos(wishYaw), y = std::sin(wishYaw);
    const float projection = s.velocity.x * x + s.velocity.y * y;
    const float budget =
        o.airAcceleration * (o.cappedAcceleration ? o.airSpeedCap : s.maxSpeed) * s.friction / o.tickRate;
    const float gain = std::max(0.f, std::min(budget, o.airSpeedCap - projection));
    return std::hypot(s.velocity.x + x * gain, s.velocity.y + y * gain);
}
inline StrafePlan OptimizeStrafe(const Sample &s, const Keys &keys, const Options &o, float delta) noexcept {
    StrafePlan out;
    const float speed = std::hypot(s.velocity.x, s.velocity.y);
    if (!o.strafer || !IsAirborne(s) || keys.Held(A) == keys.Held(D) || !std::isfinite(speed) || speed < o.minSpeed ||
        speed > 4000 || !std::isfinite(s.yaw) || !std::isfinite(delta) || delta <= 0 || !std::isfinite(s.maxSpeed) ||
        s.maxSpeed < 1 || s.maxSpeed > 1000 || !std::isfinite(s.friction) || s.friction <= 0 || s.friction > 2)
        return out;
    constexpr float degrees = 57.29577951308232f;
    const int side = keys.Held(A) ? 1 : -1;
    const float budget =
        o.airAcceleration * (o.cappedAcceleration ? o.airSpeedCap : s.maxSpeed) * s.friction / o.tickRate;
    // Maximize the horizontal gain under a capped wish-direction acceleration model.
    const float optimalProjection = std::clamp(o.airSpeedCap - budget, 0.f, speed);
    const float theta = std::acos(optimalProjection / speed) * degrees;
    const float velocityYaw = std::atan2(s.velocity.y, s.velocity.x) * degrees;
    const float ideal = NormalizeYaw(velocityYaw + side * (theta - 90.f));
    const float step = o.turnRate * std::clamp(delta, .001f, .02f);
    const float desired = NormalizeYaw(s.yaw + std::clamp(NormalizeYaw(ideal - s.yaw), -step, step));
    const float otherIdeal = NormalizeYaw(velocityYaw - side * theta - side * 90.f);
    const float other = NormalizeYaw(s.yaw + std::clamp(NormalizeYaw(otherIdeal - s.yaw), -step, step));
    const std::array<float, 5> choices{s.yaw, desired, other, NormalizeYaw(s.yaw - step), NormalizeYaw(s.yaw + step)};
    float best = -1;
    for (float yaw : choices) {
        const float predicted = PredictedSpeed(s, yaw, side, o);
        if (predicted > best + .0001f) {
            best = predicted;
            out.yaw = yaw;
        }
    }
    if (!std::isfinite(best) || best < 0)
        return {};
    out.active = true;
    out.forwardMove = 0;
    out.sideMove = -side * s.maxSpeed; // Source side-move is negative for left.
    out.predictedSpeed = best;
    return out;
}
enum class Status : unsigned { Off, Paused, Waiting, Target, Airborne, Steering, InputBlocked };
inline const char *Name(Status s) noexcept {
    switch (s) {
    case Status::Off:
        return "Off";
    case Status::Paused:
        return "Paused";
    case Status::Waiting:
        return "Ready";
    case Status::Target:
        return "Enemy under crosshair";
    case Status::Airborne:
        return "Airborne";
    case Status::Steering:
        return "Steering";
    default:
        return "Input unavailable";
    }
}
struct Diagnostics {
    Status shoot{}, jump{}, strafe{};
    std::uint64_t shots{}, jumps{}, turns{}, failures{};
    std::uint32_t target{}, flags{};
    std::uint8_t moveType{};
    float speed{}, forwardMove{}, sideMove{};
};
class Controller {
    std::uint32_t owner_{}, target_{}, weapon_{};
    double targetSince_{}, nextShot_{}, mouseUntil_{}, jumpUntil_{}, lastJump_{-1}, lastTime_{};
    bool mouseDown_{}, jumpDown_{}, jumpArmed_{true}, jumpEngaged_{};
    std::array<bool, 2> suppressed_{};
    Diagnostics stats_;
    template <class B> bool Key(B &b, unsigned key, bool down) {
        if (b.Key(key, down))
            return true;
        ++stats_.failures;
        return false;
    }
    template <class B> bool Mouse(B &b, bool down) {
        if (b.Mouse(down))
            return true;
        ++stats_.failures;
        return false;
    }
    template <class B> void ReleaseMouse(B &b, const Keys &keys) {
        if (mouseDown_ && (keys.Held(LeftMouse) || Mouse(b, false)))
            mouseDown_ = false;
    }
    template <class B> void ReleaseJump(B &b) {
        if (jumpDown_ && Key(b, Space, false))
            jumpDown_ = false;
    }
    template <class B> void RestoreForward(B &b, const Keys &keys, bool allow) {
        for (unsigned i = 0; i < 2; ++i)
            if (suppressed_[i]) {
                const unsigned key = i ? S : W;
                if (!allow || !keys.Held(key) || Key(b, key, true))
                    suppressed_[i] = false;
            }
    }

  public:
    const Diagnostics &GetStatus() const noexcept { return stats_; }
    unsigned SuppressedRepeats() const noexcept {
        return (suppressed_[0] ? 1u : 0u) | (suppressed_[1] ? 2u : 0u) | (jumpEngaged_ ? 4u : 0u);
    }
    bool PendingRelease() const noexcept { return mouseDown_ || jumpDown_ || suppressed_[0] || suppressed_[1]; }
    template <class B> void Stop(B &b, const Keys &keys, bool allowRestore = false) {
        ReleaseMouse(b, keys);
        ReleaseJump(b);
        RestoreForward(b, keys, allowRestore);
        owner_ = target_ = weapon_ = 0;
        targetSince_ = nextShot_ = 0;
        jumpArmed_ = true;
        jumpEngaged_ = false;
        lastJump_ = -1;
        lastTime_ = 0;
    }
    template <class B>
    void Step(const Options &o, const Sample &s, const Keys &keys, double now, bool active, bool cameraBusy, B &b,
              bool allowInactiveRestore = false) {
        const float delta = lastTime_ > 0 ? static_cast<float>(now - lastTime_) : .004f;
        if (!std::isfinite(now) || now < 0 || !active || !s.valid || s.frozen || !Valid(o)) {
            Stop(b, keys, allowInactiveRestore);
            stats_.shoot = o.shoot ? Status::Paused : Status::Off;
            stats_.jump = o.jumper ? Status::Paused : Status::Off;
            stats_.strafe = o.strafer ? Status::Paused : Status::Off;
            return;
        }
        if (owner_ != s.owner || (lastTime_ > 0 && (delta <= 0 || delta > .15f)))
            Stop(b, keys, true);
        owner_ = s.owner;
        lastTime_ = now;
        const auto failuresBefore = stats_.failures;
        stats_.target = s.target;
        stats_.flags = s.flags;
        stats_.moveType = s.moveType;
        stats_.speed = std::hypot(s.velocity.x, s.velocity.y);
        stats_.forwardMove = stats_.sideMove = 0;
        stats_.shoot = o.shoot ? Status::Waiting : Status::Off;
        stats_.jump = o.jumper ? (IsAirborne(s) ? Status::Airborne : Status::Waiting) : Status::Off;
        stats_.strafe = o.strafer ? Status::Waiting : Status::Off;
        const bool eligible = o.shoot && keys.Held(o.shootKey) && !keys.Held(LeftMouse) && CrosshairEnemy(s) &&
                              (!o.scopeOnly || s.scoped);
        if (mouseDown_ && (now >= mouseUntil_ || !eligible || target_ != s.target || weapon_ != s.weaponHandle))
            ReleaseMouse(b, keys);
        if (!eligible) {
            target_ = weapon_ = 0;
            targetSince_ = now;
        } else {
            stats_.shoot = Status::Target;
            if (target_ != s.target || weapon_ != s.weaponHandle) {
                target_ = s.target;
                weapon_ = s.weaponHandle;
                targetSince_ = now;
            }
            if (!mouseDown_ && s.weaponReady && now >= nextShot_ && now - targetSince_ + 1e-9 >= o.delayMs * .001) {
                if (Mouse(b, true)) {
                    mouseDown_ = true;
                    mouseUntil_ = now + o.pressMs * .001;
                    ++stats_.shots;
                }
                nextShot_ = now + o.intervalMs * .001;
            }
        }
        if (!o.jumper || !keys.Held(Space) || !Movable(s)) {
            ReleaseJump(b);
            jumpArmed_ = true;
            jumpEngaged_ = false;
            lastJump_ = -1;
        } else {
            if (!jumpEngaged_) {
                // The physical press may already be down in the game; prime a fresh edge.
                if (Key(b, Space, false))
                    jumpEngaged_ = true;
            }
            if (jumpDown_ && (now >= jumpUntil_ || !s.grounded))
                ReleaseJump(b);
            if (!s.grounded)
                jumpArmed_ = true;
            if (jumpEngaged_ && s.grounded && !jumpDown_ && (jumpArmed_ || now - lastJump_ >= .15)) {
                if (Key(b, Space, true)) {
                    jumpDown_ = true;
                    jumpUntil_ = now + .02;
                    lastJump_ = now;
                    jumpArmed_ = false;
                    ++stats_.jumps;
                }
            }
        }
        const bool steer = o.strafer && !cameraBusy && !keys.Held(LeftMouse) && !mouseDown_;
        const auto plan = steer ? OptimizeStrafe(s, keys, o, delta) : StrafePlan{};
        if (!plan.active)
            RestoreForward(b, keys, true);
        else {
            bool ready = true;
            for (unsigned i = 0; i < 2; ++i) {
                const unsigned key = i ? S : W;
                if (keys.Held(key) && !suppressed_[i]) {
                    if (Key(b, key, false))
                        suppressed_[i] = true;
                    else
                        ready = false;
                }
                if (!keys.Held(key))
                    suppressed_[i] = false;
            }
            if (ready) {
                if (b.Yaw(s, plan.yaw)) {
                    ++stats_.turns;
                    stats_.strafe = Status::Steering;
                    stats_.forwardMove = plan.forwardMove;
                    stats_.sideMove = plan.sideMove;
                } else {
                    ++stats_.failures;
                    RestoreForward(b, keys, true);
                }
            }
        }
        if (stats_.failures != failuresBefore) {
            if (o.shoot)
                stats_.shoot = Status::InputBlocked;
            if (o.jumper)
                stats_.jump = Status::InputBlocked;
            if (o.strafer)
                stats_.strafe = Status::InputBlocked;
        }
    }
};
} // namespace awareness::assist
