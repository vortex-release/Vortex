#pragma once
#include "combat_reader.hpp"
#include "tracking_profiles.hpp"
#include "world_visuals.hpp"
namespace awareness::cs2 {
inline bool ReadTrackingWeapon(const Memory &m, std::uintptr_t list, std::uintptr_t pawn,
                               tracking::Sample &out) noexcept {
    std::uintptr_t services{};
    std::uint32_t after{};
    std::uint16_t definition{};
    std::uint8_t life{};
    if (!FullHandle(m, pawn, out.owner) || EntityAt(m, list, out.owner) != pawn ||
        !m.Field(pawn, offsets::LifeState, life) || life || !m.Field(pawn, offsets::WeaponServices, services) ||
        !m.Field(services, offsets::ActiveWeapon, out.weaponHandle))
        return false;
    const auto weapon = EntityAt(m, list, out.weaponHandle);
    if (!FullHandle(m, weapon, after) || after != out.weaponHandle ||
        !m.Field(weapon, offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, definition) ||
        !FullHandle(m, pawn, after) || after != out.owner)
        return false;
    out.weapon = definition;
    return true;
}
inline bool ReadFootstep(const Memory &m, std::uintptr_t list, std::uintptr_t pawn, double now,
                         worldvisuals::Footstep &out) noexcept {
    out = {};
    std::uintptr_t scene{};
    std::uint32_t after{};
    std::uint8_t life{}, team{}, dormant{};
    char name[64]{};
    if (!EntityName(m, pawn, name) || std::strcmp(name, "cs_player_pawn") || !FullHandle(m, pawn, out.handle) ||
        EntityAt(m, list, out.handle) != pawn || !m.Field(pawn, offsets::LifeState, life) || life ||
        !m.Field(pawn, offsets::Team, team) || team < 2 || team > 3 || !m.Field(pawn, offsets::SceneNode, scene) ||
        !m.Field(scene, offsets::Dormant, dormant) || dormant || !m.Field(scene, offsets::Origin, out.position) ||
        !PlausiblePosition(out.position) || !FullHandle(m, pawn, after) || after != out.handle)
        return false;
    out.team = team;
    out.time = now;
    out.position.z += 1.5f;
    return true;
}
inline Vector3 RotateWeapon(Vector3 p, Vector3 angles) noexcept {
    constexpr float rad = .01745329252f;
    const float cy = std::cos(angles.y * rad), sy = std::sin(angles.y * rad);
    const float cp = std::cos(angles.x * rad), sp = std::sin(angles.x * rad);
    const float cr = std::cos(angles.z * rad), sr = std::sin(angles.z * rad);
    const Vector3 roll{p.x, p.y * cr - p.z * sr, p.y * sr + p.z * cr};
    const Vector3 pitch{roll.x * cp + roll.z * sp, roll.y, -roll.x * sp + roll.z * cp};
    return {pitch.x * cy - pitch.y * sy, pitch.x * sy + pitch.y * cy, pitch.z};
}
inline bool ReadDroppedWeapon(const Memory &m, std::uintptr_t list, std::uint32_t expected,
                              worldvisuals::DroppedWeapon &out) noexcept {
    out = {};
    const auto entity = EntityAt(m, list, expected);
    std::uintptr_t scene{}, collision{};
    std::uint32_t handle{}, owner{}, after{};
    std::uint8_t dormant{};
    std::uint16_t id{};
    if (!FullHandle(m, entity, handle) || handle != expected || !m.Field(entity, offsets::EntityOwner, owner) ||
        (owner && owner != 0xffffffff) ||
        !m.Field(entity, offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, id) ||
        !FindWeaponIcon(id) || !m.Field(entity, offsets::SceneNode, scene) ||
        !m.Field(scene, offsets::Dormant, dormant) || dormant || !m.Field(scene, offsets::Origin, out.position) ||
        !PlausiblePosition(out.position))
        return false;
    Vector3 low{}, high{}, angles{};
    float scale{};
    if (m.Field(entity, offsets::Collision, collision) && collision && m.Field(collision, offsets::Mins, low) &&
        m.Field(collision, offsets::Maxs, high) && Finite(low) && Finite(high) && high.x > low.x && high.y > low.y &&
        high.z > low.z && Distance(low, {}) < 256 && Distance(high, {}) < 256 &&
        m.Field(scene, offsets::AbsRotation, angles) && Finite(angles) && m.Field(scene, offsets::AbsScale, scale) &&
        std::isfinite(scale) && scale > 0 && scale < 10) {
        for (unsigned i = 0; i < 8; ++i) {
            Vector3 p{i & 1 ? high.x : low.x, i & 2 ? high.y : low.y, i & 4 ? high.z : low.z};
            out.corners[i] = out.position + RotateWeapon({p.x * scale, p.y * scale, p.z * scale}, angles);
        }
        out.bounds = true;
    }
    if (!FullHandle(m, entity, after) || after != expected || !m.Field(entity, offsets::EntityOwner, after) ||
        after != owner)
        return false;
    out.handle = expected;
    out.definition = id;
    return true;
}
class DroppedReader {
    std::array<std::uint32_t, 256> handles_{};
    unsigned count_{};
    EntityDiscovery discovery_;
    std::uintptr_t list_{};
    double previous_{};

  public:
    void Reset() noexcept { *this = {}; }
    bool Update(const Memory &m, std::uintptr_t list, double now, worldvisuals::Drops &out) noexcept {
        out = {};
        if (!list || !std::isfinite(now)) {
            Reset();
            return false;
        }
        if (list != list_ || now < previous_) {
            Reset();
            list_ = list;
        }
        previous_ = now;
        for (unsigned i = 0; i < count_;) {
            std::uint32_t current{};
            if (!FullHandle(m, EntityAt(m, list, handles_[i]), current) || current != handles_[i])
                handles_[i] = handles_[--count_];
            else
                ++i;
        }
        discovery_.Scan(m, list, now, [&](std::uintptr_t entity, const char *name) {
            if (std::strncmp(name, "weapon_", 7))
                return;
            std::uint32_t h{};
            std::uint16_t id{};
            if (!FullHandle(m, entity, h) ||
                !m.Field(entity, offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, id) ||
                !FindWeaponIcon(id))
                return;
            for (unsigned i = 0; i < count_; ++i)
                if (handles_[i] == h)
                    return;
            if (count_ < handles_.size())
                handles_[count_++] = h;
        });
        for (unsigned i = 0; i < count_ && out.count < out.values.size(); ++i)
            if (ReadDroppedWeapon(m, list, handles_[i], out.values[out.count]))
                ++out.count;
        out.time = now;
        return true;
    }
};
} // namespace awareness::cs2
