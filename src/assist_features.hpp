#pragma once
#include <awareness/OverlayApi.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace awareness::assist {
inline constexpr unsigned LeftMouse = 1, Mouse4 = 5, Mouse5 = 6, Space = 32, A = 65, D = 68, S = 83, W = 87,
                          LeftShift = 160, RightShift = 161;
inline constexpr std::array<unsigned, 5> ShootKeys{Mouse4, Mouse5, 164, 160, 162};
struct Options {
    std::uint32_t shoot{}, shootKey{Mouse4}, shootMode{1}, scopeOnly{}, jumper{}, strafer{}, cappedAcceleration{1};
    std::uint32_t preserveForward{1}, strafeWalkPause{1}, strafeMode{}, autoPistol{};
    float pistolIntervalMs{};
    float strafeStrength{.65f}, strafeRampMs{80};
    float delayMs{35}, intervalMs{100}, pressMs{20}, turnRate{360}, minSpeed{80};
    float airAcceleration{12}, airSpeedCap{30}, tickRate{64};
};
inline bool Valid(const Options &o) noexcept {
    const auto range = [](float v, float a, float b) { return std::isfinite(v) && v >= a && v <= b; };
    return o.shoot <= 1 && o.shootMode <= 2 && o.preserveForward <= 1 && o.strafeWalkPause <= 1 && o.strafeMode <= 1 &&
           o.autoPistol <= 1 && range(o.pistolIntervalMs, 0, 500) && range(o.strafeStrength, 0, 1) &&
           range(o.strafeRampMs, 0, 250) && o.scopeOnly <= 1 && o.jumper <= 1 && o.strafer <= 1 &&
           o.cappedAcceleration <= 1 && std::find(ShootKeys.begin(), ShootKeys.end(), o.shootKey) != ShootKeys.end() &&
           range(o.delayMs, 0, 300) && range(o.intervalMs, 40, 600) && range(o.pressMs, 8, 40) &&
           range(o.turnRate, 30, 720) && range(o.minSpeed, 10, 400) && range(o.airAcceleration, 1, 200) &&
           range(o.airSpeedCap, 1, 100) && range(o.tickRate, 30, 128);
}
struct Keys {
    bool textInput{};
    std::int64_t mouseTravelX{};
    std::uint64_t mouseSequence{};
    std::array<bool, 256> down{};
    bool Held(unsigned key) const noexcept { return key < down.size() && down[key]; }
    bool WalkingHeld() const noexcept { return Held(LeftShift) || Held(RightShift); }
};
struct Sample {
    bool valid{}, grounded{}, walking{}, frozen{}, scoped{}, enemy{}, weaponReady{};
    bool weaponKnown{}, reloading{}, empty{}, waitingAttack{}, cooldown{}, readinessKnown{}, targetKnown{};
    bool movementKnown{}, velocityKnown{}, anglesKnown{};
    std::uint32_t owner{}, weaponHandle{}, target{}, tick{}, flags{}, shots{};
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
inline bool SemiAutomaticPistol(std::uint16_t weapon) noexcept {
    switch (weapon) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 30:
    case 32:
    case 36:
    case 61:
        return true;
    default:
        return false;
    }
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
inline float WishAngle(const Keys &keys, const Options &o) noexcept {
    const float forward = o.preserveForward ? float(keys.Held(W)) - float(keys.Held(S)) : 0;
    const float left = float(keys.Held(A)) - float(keys.Held(D));
    return std::atan2(left, forward) * 57.29577951308232f;
}
inline float PredictedWishSpeed(const Sample &s, float yaw, float wishAngle, const Options &o) noexcept {
    constexpr float radians = .017453292519943295f;
    const float wishYaw = (yaw + wishAngle) * radians;
    const float x = std::cos(wishYaw), y = std::sin(wishYaw);
    const float projection = s.velocity.x * x + s.velocity.y * y;
    const float budget =
        o.airAcceleration * (o.cappedAcceleration ? o.airSpeedCap : s.maxSpeed) * s.friction / o.tickRate;
    const float gain = std::max(0.f, std::min(budget, o.airSpeedCap - projection));
    return std::hypot(s.velocity.x + x * gain, s.velocity.y + y * gain);
}
inline float PredictedSpeed(const Sample &s, float yaw, int side, const Options &o) noexcept {
    return PredictedWishSpeed(s, yaw, side * 90.f, o);
}
inline StrafePlan OptimizeStrafe(const Sample &s, const Keys &keys, const Options &o, float delta) noexcept {
    StrafePlan out;
    const float speed = std::hypot(s.velocity.x, s.velocity.y);
    if (!o.strafer || o.strafeStrength <= 0 || !s.anglesKnown || !s.velocityKnown || !IsAirborne(s) ||
        keys.Held(A) == keys.Held(D) || !std::isfinite(speed) || speed < o.minSpeed || speed > 4000 ||
        !std::isfinite(s.yaw) || !std::isfinite(delta) || delta <= 0 || !std::isfinite(s.maxSpeed) || s.maxSpeed < 1 ||
        s.maxSpeed > 1000 || !std::isfinite(s.friction) || s.friction <= 0 || s.friction > 2)
        return out;
    constexpr float degrees = 57.29577951308232f;
    const int side = keys.Held(A) ? 1 : -1;
    const float budget =
        o.airAcceleration * (o.cappedAcceleration ? o.airSpeedCap : s.maxSpeed) * s.friction / o.tickRate;
    const float optimalProjection = std::clamp(o.airSpeedCap - budget, 0.f, speed);
    const float theta = std::acos(optimalProjection / speed) * degrees;
    const float velocityYaw = std::atan2(s.velocity.y, s.velocity.x) * degrees;
    const float wishAngle = WishAngle(keys, o);
    const float ideal = NormalizeYaw(velocityYaw + side * theta - wishAngle);
    const float step = o.turnRate * o.strafeStrength * std::clamp(delta, .001f, .02f);
    const float desired = NormalizeYaw(s.yaw + std::clamp(NormalizeYaw(ideal - s.yaw), -step, step));
    // Stay on the requested side of the velocity vector. Searching the opposite
    // optimum can reverse the turn under A/D and visibly fight the player.
    const std::array<float, 2> choices{s.yaw, desired};
    float best = -1;
    for (float yaw : choices) {
        const float predicted = PredictedWishSpeed(s, yaw, wishAngle, o);
        if (predicted > best + .0001f) {
            best = predicted;
            out.yaw = yaw;
        }
    }
    if (!std::isfinite(best) || best < 0)
        return {};
    out.active = true;
    const float forward = o.preserveForward ? float(keys.Held(W)) - float(keys.Held(S)) : 0;
    const float scale = s.maxSpeed / std::sqrt(forward * forward + 1.f);
    out.forwardMove = forward * scale;
    out.sideMove = -side * scale;
    out.predictedSpeed = best;
    return out;
}
enum class Status : unsigned {
    Off,
    Paused,
    Waiting,
    Target,
    Airborne,
    Steering,
    InputBlocked,
    HoldKey,
    ToggleOff,
    NoTarget,
    WeaponCooldown,
    Reloading,
    Empty,
    ScopeRequired,
    ManualInput,
    Grounded,
    LowSpeed,
    CameraBusy,
    UnsupportedMovement,
    UnsupportedWeapon,
    DataUnavailable,
    Walking,
    MouseDirection
};
inline const char *Name(Status s) noexcept {
    switch (s) {
    case Status::Off:
        return "Off";
    case Status::Paused:
        return "Paused";
    case Status::Waiting:
        return "Ready";
    case Status::Target:
        return "Target acquired";
    case Status::Airborne:
        return "Waiting for landing";
    case Status::Steering:
        return "Steering";
    case Status::HoldKey:
        return "Hold activation key";
    case Status::ToggleOff:
        return "Press key to activate";
    case Status::NoTarget:
        return "Waiting for enemy";
    case Status::WeaponCooldown:
        return "Weapon not ready";
    case Status::Reloading:
        return "Reloading";
    case Status::Empty:
        return "Empty magazine";
    case Status::ScopeRequired:
        return "Scope required";
    case Status::ManualInput:
        return "Manual input";
    case Status::Grounded:
        return "Waiting for takeoff";
    case Status::LowSpeed:
        return "Below minimum speed";
    case Status::CameraBusy:
        return "Camera assist has priority";
    case Status::UnsupportedMovement:
        return "Movement unavailable";
    case Status::UnsupportedWeapon:
        return "Unsupported weapon";
    case Status::DataUnavailable:
        return "Game data unavailable";
    case Status::Walking:
        return "Walking / manual steering";
    case Status::MouseDirection:
        return "Move mouse while holding jump";
    default:
        return "Input unavailable";
    }
}
struct Diagnostics {
    Status shoot{}, jump{}, strafe{}, pistol{};
    std::uint64_t shots{}, jumps{}, turns{}, failures{};
    std::uint32_t target{}, flags{};
    std::uint8_t moveType{};
    float speed{}, forwardMove{}, sideMove{};
};
class Controller {
    std::uint32_t owner_{}, target_{}, weapon_{}, observedWeapon_{}, lastTick_{}, activationMode_{~0u},
        activationKey_{};
    double targetSince_{}, nextShot_{}, mouseUntil_{}, jumpUntil_{}, lastTime_{};
    double strafeSince_{-1};
    double jumpReleasedAt_{-1}, mouseDirectionUntil_{}, pistolReleasedAt_{}, nextPistol_{}, pistolUntil_{};
    std::uint32_t jumpReleaseTick_{}, pistolWeapon_{};
    std::int64_t mouseTravelX_{};
    std::uint64_t mouseSequence_{};
    int strafeSide_{}, mouseSide_{};
    unsigned ownedSide_{};
    bool mouseInitialized_{}, pistolEngaged_{}, pistolDown_{};
    bool mouseDown_{}, jumpDown_{}, jumpEngaged_{}, toggleOn_{}, activationHeld_{};
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

    template <class B> void ReleaseSide(B &b, const Keys &keys) {
        if (ownedSide_ && (keys.Held(ownedSide_) || Key(b, ownedSide_, false)))
            ownedSide_ = 0;
    }
    template <class B> bool Pistol(B &b, bool down) {
        if (b.Pistol(down))
            return true;
        ++stats_.failures;
        return false;
    }
    template <class B> void StopPistol(B &b, const Keys &keys, bool allowRestore) {
        if (!pistolEngaged_ && !pistolDown_)
            return;
        if (keys.Held(LeftMouse) && allowRestore) {
            if (!pistolDown_ && !b.RestorePrimary()) {
                ++stats_.failures;
                return;
            }
        } else if (pistolDown_ && !Pistol(b, false)) {
            return;
        }
        pistolDown_ = pistolEngaged_ = false;
        pistolWeapon_ = 0;
        nextPistol_ = pistolReleasedAt_ = 0;
    }

  public:
    const Diagnostics &GetStatus() const noexcept { return stats_; }
    unsigned SuppressedRepeats() const noexcept {
        return (suppressed_[0] ? 1u : 0u) | (suppressed_[1] ? 2u : 0u) | (jumpEngaged_ ? 4u : 0u);
    }
    bool PendingRelease() const noexcept {
        return mouseDown_ || jumpDown_ || ownedSide_ || pistolEngaged_ || pistolDown_ || suppressed_[0] ||
               suppressed_[1];
    }
    template <class B> void Stop(B &b, const Keys &keys, bool allowRestore = false) {
        ReleaseMouse(b, keys);
        ReleaseJump(b);
        ReleaseSide(b, keys);
        StopPistol(b, keys, allowRestore);
        RestoreForward(b, keys, allowRestore);
        owner_ = target_ = weapon_ = observedWeapon_ = lastTick_ = 0;
        strafeSince_ = -1;
        strafeSide_ = 0;
        mouseInitialized_ = false;
        mouseSide_ = 0;
        mouseDirectionUntil_ = 0;
        jumpReleasedAt_ = -1;
        jumpReleaseTick_ = 0;
        toggleOn_ = false;
        activationHeld_ = false;
        activationMode_ = ~0u;
        targetSince_ = nextShot_ = 0;
        jumpEngaged_ = false;
        lastTime_ = 0;
        stats_.target = stats_.flags = stats_.moveType = 0;
        stats_.speed = stats_.forwardMove = stats_.sideMove = 0;
    }
    template <class B>
    void Step(const Options &o, const Sample &s, const Keys &keys, double now, bool active, bool cameraBusy, B &b,
              bool allowInactiveRestore = false) {
        const float delta = lastTime_ > 0 ? static_cast<float>(now - lastTime_) : .004f;
        if (!std::isfinite(now) || now < 0 || !active || keys.textInput || !s.valid || s.frozen || !Valid(o)) {
            Stop(b, keys, allowInactiveRestore);
            const auto reason = active && !s.valid ? Status::DataUnavailable : Status::Paused;
            stats_.shoot = o.shoot ? reason : Status::Off;
            stats_.jump = o.jumper ? reason : Status::Off;
            stats_.strafe = o.strafer ? reason : Status::Off;
            stats_.pistol = o.autoPistol ? reason : Status::Off;
            return;
        }
        if (owner_ != s.owner || (lastTick_ && s.tick && s.tick < lastTick_) ||
            (lastTime_ > 0 && (delta <= 0 || delta > .15f)))
            Stop(b, keys, true);
        owner_ = s.owner;
        lastTick_ = s.tick;
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
        stats_.pistol = o.autoPistol ? Status::Waiting : Status::Off;
        // Target reacquisition restarts reaction time, not the firearm's click cadence.
        // Only a verified actual weapon switch can reset the repeat interval.
        if (s.weaponKnown && s.weaponHandle && observedWeapon_ != s.weaponHandle) {
            observedWeapon_ = s.weaponHandle;
            nextShot_ = now;
        }
        const bool pistolEligible =
            o.autoPistol && keys.Held(LeftMouse) && s.weaponKnown && SemiAutomaticPistol(s.weapon);
        const bool pistolHandoff = (pistolEngaged_ || pistolDown_) && !pistolEligible;
        if (pistolHandoff) {
            StopPistol(b, keys, true);
            // One owner releases before another starts. A manual-button release
            // cannot cancel a new assisted click later in this same worker poll.
            nextShot_ = std::max(nextShot_, now + 1. / o.tickRate);
        }
        const bool keyHeld = keys.Held(o.shootKey);
        if (activationMode_ != o.shootMode || activationKey_ != o.shootKey || !o.shoot) {
            activationMode_ = o.shootMode;
            activationKey_ = o.shootKey;
            toggleOn_ = false;
            activationHeld_ = keyHeld;
        }
        if (o.shoot && o.shootMode == 2 && keyHeld && !activationHeld_)
            toggleOn_ = !toggleOn_;
        activationHeld_ = keyHeld;
        const bool activated = o.shootMode == 1 || (o.shootMode == 0 ? keyHeld : toggleOn_);
        const bool eligible = o.shoot && activated && !pistolHandoff && !keys.Held(LeftMouse) && CrosshairEnemy(s) &&
                              (!o.scopeOnly || s.scoped);
        if (o.shoot) {
            if (!activated)
                stats_.shoot = o.shootMode == 0 ? Status::HoldKey : Status::ToggleOff;
            else if (keys.Held(LeftMouse))
                stats_.shoot = Status::ManualInput;
            else if (!CrosshairEnemy(s))
                stats_.shoot = Status::NoTarget;
            else if (o.scopeOnly && !s.scoped)
                stats_.shoot = Status::ScopeRequired;
        }
        if (mouseDown_ && (now >= mouseUntil_ || !eligible || target_ != s.target || weapon_ != s.weaponHandle))
            ReleaseMouse(b, keys);
        if (!eligible) {
            target_ = weapon_ = 0;
            targetSince_ = now;
        } else {
            stats_.shoot = s.weaponReady       ? Status::Target
                           : s.reloading       ? Status::Reloading
                           : s.empty           ? Status::Empty
                           : !s.weaponKnown    ? Status::UnsupportedWeapon
                           : !s.readinessKnown ? Status::DataUnavailable
                                               : Status::WeaponCooldown;
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
        if (o.shoot && stats_.failures != failuresBefore)
            stats_.shoot = Status::InputBlocked;
        const auto pistolFailures = stats_.failures;
        if (!pistolEligible) {
            StopPistol(b, keys, true);
            if (o.autoPistol)
                stats_.pistol = !keys.Held(LeftMouse) ? Status::HoldKey : Status::UnsupportedWeapon;
        } else {
            if (pistolEngaged_ && pistolWeapon_ != s.weaponHandle)
                StopPistol(b, keys, false);
            if (!pistolEngaged_ && Pistol(b, false)) {
                // The user's initial physical press is already delivered. Release
                // once, then wait for a distinct input interval before repeating.
                pistolEngaged_ = true;
                pistolWeapon_ = s.weaponHandle;
                pistolReleasedAt_ = now;
            }
            if (pistolDown_ && now >= pistolUntil_ && Pistol(b, false)) {
                pistolDown_ = false;
                pistolReleasedAt_ = now;
            }
            stats_.pistol = s.weaponReady       ? Status::Target
                            : s.reloading       ? Status::Reloading
                            : s.empty           ? Status::Empty
                            : !s.readinessKnown ? Status::DataUnavailable
                                                : Status::WeaponCooldown;
            if (pistolEngaged_ && pistolWeapon_ == s.weaponHandle && !pistolDown_ && s.weaponReady &&
                now - pistolReleasedAt_ + 1e-9 >= 1. / o.tickRate && now >= nextPistol_ && Pistol(b, true)) {
                pistolDown_ = true;
                pistolUntil_ = now + o.pressMs * .001;
                nextPistol_ = now + std::max(1. / o.tickRate, o.pistolIntervalMs * .001);
            }
        }
        if (o.autoPistol && stats_.failures != pistolFailures)
            stats_.pistol = Status::InputBlocked;
        const auto jumpFailures = stats_.failures;
        if (!o.jumper || !keys.Held(Space) || !Movable(s)) {
            if (o.jumper)
                stats_.jump = !Movable(s) ? Status::UnsupportedMovement : Status::HoldKey;
            ReleaseJump(b);
            jumpEngaged_ = false;
            jumpReleasedAt_ = -1;
        } else {
            if (!jumpEngaged_ && !jumpDown_ && Key(b, Space, false)) {
                jumpEngaged_ = true;
                jumpReleasedAt_ = now;
                jumpReleaseTick_ = s.tick;
            }
            if (jumpDown_ && (now >= jumpUntil_ || !s.grounded)) {
                ReleaseJump(b);
                if (!jumpDown_) {
                    jumpReleasedAt_ = now;
                    jumpReleaseTick_ = s.tick;
                }
            }
            // Never put release and press in the same input batch. Wait for a
            // new command tick, with one tick of elapsed time as the fallback.
            // A held press survives slow frames; a missed ground edge retries
            // after this release interval instead of the former 150 ms pause.
            const bool releaseObserved = jumpReleasedAt_ >= 0 && now > jumpReleasedAt_ &&
                                         ((s.tick && jumpReleaseTick_ && s.tick > jumpReleaseTick_) ||
                                          now - jumpReleasedAt_ + 1e-9 >= 1. / o.tickRate);
            if (jumpEngaged_ && s.grounded && !jumpDown_ && releaseObserved) {
                if (Key(b, Space, true)) {
                    jumpDown_ = true;
                    jumpUntil_ = now + std::max(.02, 2. / o.tickRate);
                    ++stats_.jumps;
                }
            }
        }
        if (o.jumper && stats_.failures != jumpFailures)
            stats_.jump = Status::InputBlocked;
        const auto strafeFailures = stats_.failures;
        const bool walkingOverride = o.strafeWalkPause && keys.WalkingHeld();
        const bool steer = o.strafer && !cameraBusy && !keys.Held(LeftMouse) && !mouseDown_ && !walkingOverride;
        if (o.strafer) {
            if (!s.movementKnown || !s.anglesKnown || !s.velocityKnown)
                stats_.strafe = Status::DataUnavailable;
            else if (!Movable(s))
                stats_.strafe = Status::UnsupportedMovement;
            else if (s.grounded)
                stats_.strafe = Status::Grounded;
            else if (walkingOverride)
                stats_.strafe = Status::Walking;
            else if (cameraBusy)
                stats_.strafe = Status::CameraBusy;
            else if (keys.Held(LeftMouse) || mouseDown_)
                stats_.strafe = Status::ManualInput;
            else if (stats_.speed < o.minSpeed)
                stats_.strafe = Status::LowSpeed;
            else if (keys.Held(A) == keys.Held(D))
                stats_.strafe = Status::HoldKey;
        }
        std::int64_t mouseDelta{};
        if (mouseInitialized_ && keys.mouseSequence != mouseSequence_)
            mouseDelta = std::clamp(keys.mouseTravelX - mouseTravelX_, std::int64_t{-10000}, std::int64_t{10000});
        mouseInitialized_ = true;
        mouseSequence_ = keys.mouseSequence;
        mouseTravelX_ = keys.mouseTravelX;
        if (o.strafeMode == 1) {
            // Mouse-direction mode mirrors real left/right movement. It never
            // edits camera angles and never alternates keys without mouse input.
            const bool manualSide = keys.Held(A) || keys.Held(D);
            const bool mouseSteer = steer && s.movementKnown && s.velocityKnown && IsAirborne(s) && keys.Held(Space) &&
                                    !manualSide && stats_.speed >= o.minSpeed;
            if (!mouseSteer) {
                mouseSide_ = 0;
                mouseDirectionUntil_ = 0;
                ReleaseSide(b, keys);
                RestoreForward(b, keys, true);
                if (steer && IsAirborne(s)) {
                    if (manualSide)
                        stats_.strafe = Status::ManualInput;
                    else if (!keys.Held(Space))
                        stats_.strafe = Status::HoldKey;
                }
            } else {
                if (mouseDelta) {
                    mouseSide_ = mouseDelta < 0 ? 1 : -1;
                    mouseDirectionUntil_ = now + .05;
                }
                const unsigned wanted = now < mouseDirectionUntil_ ? (mouseSide_ > 0 ? A : D) : 0;
                if (ownedSide_ != wanted)
                    ReleaseSide(b, keys);
                bool ready = !ownedSide_ || ownedSide_ == wanted;
                if (wanted && ready) {
                    if (o.preserveForward)
                        RestoreForward(b, keys, true);
                    for (unsigned i = 0; i < 2; ++i) {
                        const unsigned key = i ? S : W;
                        if (!o.preserveForward && keys.Held(key) && !suppressed_[i]) {
                            if (Key(b, key, false))
                                suppressed_[i] = true;
                            else
                                ready = false;
                        }
                        if (!keys.Held(key))
                            suppressed_[i] = false;
                    }
                    if (ready && !ownedSide_ && Key(b, wanted, true))
                        ownedSide_ = wanted;
                    if (ownedSide_ == wanted) {
                        stats_.strafe = Status::Steering;
                        stats_.forwardMove = o.preserveForward ? float(keys.Held(W)) - float(keys.Held(S)) : 0;
                        stats_.sideMove = wanted == A ? -1.f : 1.f;
                    }
                } else if (!wanted) {
                    RestoreForward(b, keys, true);
                    stats_.strafe = Status::MouseDirection;
                }
            }
            strafeSince_ = -1;
            strafeSide_ = 0;
            if (stats_.failures != strafeFailures)
                stats_.strafe = Status::InputBlocked;
            return;
        }
        ReleaseSide(b, keys);
        mouseSide_ = 0;
        mouseDirectionUntil_ = 0;
        Options steering = o;
        const int side = keys.Held(A) ? 1 : -1;
        if (strafeSince_ < 0 || strafeSide_ != side) {
            strafeSince_ = now;
            strafeSide_ = side;
        }
        if (steering.strafeRampMs > 0) {
            const double elapsed = now - strafeSince_ + std::clamp(delta, .001f, .02f);
            const float t = static_cast<float>(std::clamp(elapsed * 1000 / steering.strafeRampMs, 0., 1.));
            steering.strafeStrength *= t * t * (3 - 2 * t);
        }
        const auto plan = steer ? OptimizeStrafe(s, keys, steering, delta) : StrafePlan{};
        if (!plan.active) {
            strafeSince_ = -1;
            strafeSide_ = 0;
            RestoreForward(b, keys, true);
        } else {
            bool ready = true;
            if (o.preserveForward)
                RestoreForward(b, keys, true);
            for (unsigned i = 0; i < 2; ++i) {
                const unsigned key = i ? S : W;
                if (!o.preserveForward && keys.Held(key) && !suppressed_[i]) {
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
        if (o.strafer && stats_.failures != strafeFailures)
            stats_.strafe = Status::InputBlocked;
    }
};
} // namespace awareness::assist
