#pragma once
#include "Math.hpp"
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>

namespace awareness::camera {
// The endpoint deliberately bypasses smoothing; existing speeds retain their response.
inline constexpr float InstantFollowSpeed = 200.f;
// Y-up, +Z forward, +X right. Degrees: positive pitch looks up; positive yaw looks right.
// Reuse awareness::Vector3 (x/y/z and +/-); do not introduce a second incompatible vector type.
struct Angles {
    float pitch{}, yaw{};
};
enum class TargetTeams : std::uint32_t { Opponents, AllTeams };
inline constexpr bool ValidTargetTeams(TargetTeams teams) noexcept {
    return teams == TargetTeams::Opponents || teams == TargetTeams::AllTeams;
}
struct Settings {
    bool enabled{true};
    float fovDegrees{45.f};        // Maximum angular deviation from the view axis (cone HALF-angle).
    float interpolationSpeed{8.f}; // Exponential response rate in inverse seconds; zero pauses.
    TargetTeams teams{TargetTeams::Opponents};
};
struct Target {
    std::uint32_t id{};
    std::int32_t team{};
    float health{};
    Vector3 position{};
    bool valid{true}, dormant{};
};
struct Selection {
    std::uint32_t id{};
    Angles desired{};
    float angularDistance{};
};
inline constexpr double DegreesPerRadian = 180.0 / std::numbers::pi_v<double>;

inline bool Finite(Angles angles) noexcept {
    return std::isfinite(angles.pitch) && std::isfinite(angles.yaw);
}
inline float WrapYaw(float degrees) noexcept {
    if (!std::isfinite(degrees))
        return 0.f;
    const float wrapped = std::remainder(degrees, 360.f);
    return wrapped >= 180.f ? wrapped - 360.f : wrapped; // [-180, 180)
}
inline Angles Normalize(Angles angles) noexcept {
    if (!Finite(angles))
        return {};
    return {std::clamp(angles.pitch, -89.f, 89.f), WrapYaw(angles.yaw)};
}
inline Vector3 Forward(Angles angles) noexcept {
    angles = Normalize(angles);
    const float pitch = static_cast<float>(angles.pitch / DegreesPerRadian);
    const float yaw = static_cast<float>(angles.yaw / DegreesPerRadian);
    const float horizontal = std::cos(pitch);
    return {horizontal * std::sin(yaw), std::sin(pitch), horizontal * std::cos(yaw)};
}
inline std::optional<Angles> LookAt(Vector3 origin, Vector3 target, float fallbackYaw = 0.f) noexcept {
    if (!awareness::Finite(origin) || !awareness::Finite(target) || !std::isfinite(fallbackYaw))
        return std::nullopt;
    // Double intermediates avoid overflowing when subtracting two finite float positions.
    const double x = static_cast<double>(target.x) - origin.x;
    const double y = static_cast<double>(target.y) - origin.y;
    const double z = static_cast<double>(target.z) - origin.z;
    const double horizontal = std::hypot(x, z);
    if (std::hypot(horizontal, y) <= 1e-6)
        return std::nullopt; // Coincident points do not define a viewing direction.
    const float pitch = static_cast<float>(std::atan2(y, horizontal) * DegreesPerRadian);
    // At a vertical pole, yaw is undefined. Preserve the caller's current yaw.
    const float yaw = horizontal <= 1e-6 ? fallbackYaw : static_cast<float>(std::atan2(x, z) * DegreesPerRadian);
    return Normalize({pitch, yaw});
}
inline float AngularDistance(Vector3 origin, Vector3 target, Angles current) noexcept {
    if (!awareness::Finite(origin) || !awareness::Finite(target) || !Finite(current))
        return std::numeric_limits<float>::infinity();
    const double x = static_cast<double>(target.x) - origin.x;
    const double y = static_cast<double>(target.y) - origin.y;
    const double z = static_cast<double>(target.z) - origin.z;
    const double length = std::hypot(x, y, z);
    if (length <= 1e-6)
        return std::numeric_limits<float>::infinity();
    const auto forward = Forward(current);
    const double dot = forward.x * x + forward.y * y + forward.z * z;
    const double crossX = forward.y * z - forward.z * y;
    const double crossY = forward.z * x - forward.x * z;
    const double crossZ = forward.x * y - forward.y * x;
    return static_cast<float>(std::atan2(std::hypot(crossX, crossY, crossZ), dot) * DegreesPerRadian);
}
inline Angles LerpAngles(Angles current, Angles target, float alpha) noexcept {
    if (!Finite(current) || !Finite(target) || !std::isfinite(alpha))
        return current;
    current = Normalize(current);
    target = Normalize(target);
    alpha = std::clamp(alpha, 0.f, 1.f);
    // Lerp the shortest yaw arc: +179 -> -179 crosses 180, not zero.
    return Normalize({std::lerp(current.pitch, target.pitch, alpha),
                      std::lerp(current.yaw, current.yaw + WrapYaw(target.yaw - current.yaw), alpha)});
}
inline std::optional<Selection> SelectTarget(Vector3 origin, Angles current, std::span<const Target> entities,
                                             std::uint32_t localId, std::int32_t localTeam, float fovDegrees,
                                             TargetTeams teams = TargetTeams::Opponents) noexcept {
    if (!awareness::Finite(origin) || !Finite(current) || !std::isfinite(fovDegrees) || !ValidTargetTeams(teams))
        return std::nullopt;
    fovDegrees = std::clamp(fovDegrees, 0.f, 180.f);
    std::optional<Selection> best;
    for (const auto &entity : entities) {
        if (!entity.valid || entity.dormant || entity.id == localId || !std::isfinite(entity.health) ||
            entity.health <= 0.f || (teams == TargetTeams::Opponents && entity.team == localTeam))
            continue;
        const auto desired = LookAt(origin, entity.position, current.yaw);
        if (!desired)
            continue;
        const float distance = AngularDistance(origin, entity.position, current);
        if (distance > fovDegrees)
            continue;
        // Stable ID tie-breaker prevents equal-distance selection depending on array order.
        if (!best || distance < best->angularDistance || (distance == best->angularDistance && entity.id < best->id))
            best = Selection{entity.id, *desired, distance};
    }
    return best;
}
// Caller supplies activation state and frame delta. No Win32, ImGui, engine pointers or memory writes here.
inline std::optional<Selection> Update(Vector3 origin, Angles &current, std::span<const Target> entities,
                                       std::uint32_t localId, std::int32_t localTeam, const Settings &settings,
                                       float deltaSeconds, bool activationHeld) noexcept {
    if (!activationHeld || !settings.enabled || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.f ||
        !std::isfinite(settings.interpolationSpeed) || settings.interpolationSpeed <= 0.f)
        return std::nullopt;
    const auto selection =
        SelectTarget(origin, current, entities, localId, localTeam, settings.fovDegrees, settings.teams);
    if (selection) {
        // Frame-time-adjusted lerp weight: equivalent response at 30, 60 and 144 FPS for a fixed target.
        const float alpha =
            settings.interpolationSpeed >= InstantFollowSpeed
                ? 1.f
                : static_cast<float>(-std::expm1(-static_cast<double>(settings.interpolationSpeed) * deltaSeconds));
        current = LerpAngles(current, selection->desired, alpha);
    }
    return selection;
}
} // namespace awareness::camera
