#pragma once
#include <awareness/TrackingApi.hpp>
#include "weapon_catalog.hpp"
#include <array>
namespace awareness::tracking {
enum class Group : std::uint32_t { Default, Pistols, SMGs, Rifles, Snipers, Heavy };
inline constexpr const char *GroupNames[]{"Default", "Pistols", "SMGs", "Rifles", "Snipers", "Heavy"};
inline constexpr std::uint32_t GroupIcons[]{0, 4, 17, 7, 9, 14};
inline Group WeaponGroup(std::uint32_t id) noexcept {
    switch (id) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 30:
    case 32:
    case 36:
    case 61:
    case 63:
    case 64:
        return Group::Pistols;
    case 17:
    case 19:
    case 23:
    case 24:
    case 26:
    case 33:
    case 34:
        return Group::SMGs;
    case 7:
    case 8:
    case 10:
    case 13:
    case 16:
    case 39:
    case 60:
        return Group::Rifles;
    case 9:
    case 11:
    case 38:
    case 40:
        return Group::Snipers;
    case 14:
    case 25:
    case 27:
    case 28:
    case 29:
    case 35:
        return Group::Heavy;
    default:
        return Group::Default;
    }
}
inline unsigned WeaponSlot(std::uint32_t id) noexcept {
    for (unsigned i = 0; i < std::size(WeaponIcons); ++i)
        if (WeaponIcons[i].id == id)
            return i + 1;
    return 0;
}
struct Profile {
    std::uint32_t custom{}, enabled{1};
    float fov{1}, speed{24};
};
struct Options {
    std::array<Profile, 6> groups{};
    std::array<Profile, std::size(WeaponIcons) + 1> weapons{};
    std::uint32_t selection{}, weaponSelection{}, compensation{};
    float latencyMs{}, maxPredictionMs{80}, strength{1};
};
inline bool Valid(const Options &o) noexcept {
    if ((o.weaponSelection && !WeaponSlot(o.weaponSelection)) || o.selection >= o.groups.size() || o.compensation > 1 ||
        !std::isfinite(o.latencyMs) || o.latencyMs < 0 || o.latencyMs > 200 || !std::isfinite(o.maxPredictionMs) ||
        o.maxPredictionMs < 0 || o.maxPredictionMs > 200 || !std::isfinite(o.strength) || o.strength < 0 ||
        o.strength > 1)
        return false;
    const auto valid = [](const Profile &p) {
        return p.custom <= 1 && p.enabled <= 1 && std::isfinite(p.fov) && p.fov >= 1 && p.fov <= 90 &&
               std::isfinite(p.speed) && p.speed >= 0 && p.speed <= camera::InstantFollowSpeed;
    };
    for (const auto &p : o.groups)
        if (!valid(p))
            return false;
    for (const auto &p : o.weapons)
        if (!valid(p))
            return false;
    return true;
}
inline TrackingConfiguration Resolve(const TrackingConfiguration &base, const Options &o, std::uint32_t weapon,
                                     bool native) noexcept {
    auto result = base;
    const auto group = WeaponGroup(weapon);
    // Unknown, knife and utility equipment cannot inherit a firearm profile in CS2.
    if (native && group == Group::Default) {
        result.enabled = 0;
        return result;
    }
    const auto slot = WeaponSlot(weapon);
    const auto &p = slot && o.weapons[slot].custom ? o.weapons[slot] : o.groups[static_cast<unsigned>(group)];
    if (group != Group::Default && p.custom) {
        result.enabled = base.enabled && p.enabled;
        result.fovDegrees = p.fov;
        result.interpolationSpeed = p.speed;
    }
    return result;
}
inline std::uint32_t LocalWeapon(const FrameSnapshot &frame) noexcept {
    for (unsigned i = 0; i < frame.entityCount && i < MaxEntities; ++i)
        if (frame.entities[i].id == frame.localEntityId)
            return frame.weaponDefinitionIndices[i];
    return 0;
}
struct Motion {
    std::uint32_t id{}, handle{};
    Vector3 velocity{};
    bool valid{};
};
struct Sample {
    std::uint32_t weapon{}, owner{}, weaponHandle{};
    std::array<Motion, MaxEntities> motion{};
    double time{};
};
inline Vector3 Predict(Vector3 point, const Motion &motion, double age, const Options &o) noexcept {
    if (!o.compensation || !motion.valid || !Finite(point) || !Finite(motion.velocity) || !std::isfinite(age) ||
        age < 0 || age > .1)
        return point;
    const float speed = Distance(motion.velocity, {});
    if (!std::isfinite(speed) || speed > 1200)
        return point;
    const float seconds =
        std::min(static_cast<float>(age) + o.latencyMs * .001f, o.maxPredictionMs * .001f) * o.strength;
    auto delta = Vector3{motion.velocity.x * seconds, motion.velocity.y * seconds, motion.velocity.z * seconds};
    const float distance = Distance(delta, {});
    if (distance > 32)
        delta = {delta.x * 32 / distance, delta.y * 32 / distance, delta.z * 32 / distance};
    return point + delta;
}
inline float SmoothMilliseconds(float speed) noexcept {
    return speed >= camera::InstantFollowSpeed ? 0.f : speed > 0 ? 1000.f / speed : 500.f;
}
inline float ResponseSpeed(float milliseconds) noexcept {
    return milliseconds <= 5 ? camera::InstantFollowSpeed : 1000.f / milliseconds;
}
} // namespace awareness::tracking
