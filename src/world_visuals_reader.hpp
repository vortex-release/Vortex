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
    if (!EntityName(m, pawn, name) ||
        (std::strcmp(name, "cs_player_pawn") && std::strcmp(name, "c_cs_player_for_precache")) ||
        !FullHandle(m, pawn, out.handle) || EntityAt(m, list, out.handle) != pawn ||
        !m.Field(pawn, offsets::LifeState, life) || life || !m.Field(pawn, offsets::Team, team) || team < 2 ||
        team > 3 || !m.Field(pawn, offsets::SceneNode, scene) || !m.Field(scene, offsets::Dormant, dormant) ||
        dormant || !m.Field(scene, offsets::Origin, out.position) || !PlausiblePosition(out.position) ||
        !FullHandle(m, pawn, after) || after != out.handle)
        return false;
    out.team = team;
    out.time = now;
    out.position.z += 1.5f;
    return true;
}
// Observe the engine's alternating step phase, not a synthetic distance timer.
// This supplements event dispatch when a client-side step is never broadcast.
class FootstepReader {
    struct Phase {
        std::uint32_t handle{};
        std::uintptr_t movement{};
        int side{};
        double time{};
    };
    std::array<Phase, MaxEntities> phases_{};
    worldvisuals::Footsteps events_;
    std::uintptr_t list_{};
    std::uint64_t accepted_{};

  public:
    void Reset() noexcept {
        phases_ = {};
        events_.Clear();
        list_ = 0;
        accepted_ = 0;
    }
    const auto &Events() const noexcept { return events_; }
    auto Count() const noexcept { return accepted_; }
    void Update(const Memory &m, std::uintptr_t list, const FrameSnapshot &frame, double now) noexcept {
        if (!list || !std::isfinite(now)) {
            Reset();
            return;
        }
        if (list != list_) {
            Reset();
            list_ = list;
        }
        for (unsigned i = 0; i < MaxEntities; ++i) {
            auto &previous = phases_[i];
            if (i >= frame.entityCount) {
                previous = {};
                continue;
            }
            const auto &entity = frame.entities[i];
            if (!entity.valid || entity.dormant || entity.health <= 0) {
                previous = {};
                continue;
            }
            const auto pawn = EntityAt(m, list, entity.id);
            std::uint32_t handle{}, after{}, flags{};
            std::uintptr_t movement{};
            Vector3 velocity{};
            int side{};
            if (!FullHandle(m, pawn, handle) || (handle & offsets::EntryMask) != entity.id ||
                !m.Field(pawn, offsets::MovementServices, movement) || !movement ||
                !m.Field(movement, offsets::MovementStepSide, side) || side < 0 || side > 1 ||
                !m.Field(pawn, offsets::MovementFlags, flags) || !m.Field(pawn, offsets::AbsVelocity, velocity) ||
                !Finite(velocity) || !FullHandle(m, pawn, after) || after != handle) {
                previous = {};
                continue;
            }
            const bool transition = previous.handle == handle && previous.movement == movement && now > previous.time &&
                                    now - previous.time < .25 && previous.side != side;
            previous = {handle, movement, side, now};
            const float speed2 = velocity.x * velocity.x + velocity.y * velocity.y;
            if (!transition || !(flags & 1) || speed2 < 1600 || speed2 > 1000000)
                continue;
            worldvisuals::Footstep sample;
            if (ReadFootstep(m, list, pawn, now, sample) && sample.handle == handle && events_.Add(sample))
                ++accepted_;
        }
    }
};
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
        !worldvisuals::DroppedName(id) || !m.Field(entity, offsets::SceneNode, scene) ||
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
    if (worldvisuals::DropCategory(id) < 5) {
        int clip{};
        if (m.Field(entity, offsets::Clip1, clip) && clip >= 0 && clip <= 250)
            out.ammo = clip;
    }
    std::uint16_t definitionAfter{};
    if (!FullHandle(m, entity, after) || after != expected || !m.Field(entity, offsets::EntityOwner, after) ||
        after != owner ||
        !m.Field(entity, offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, definitionAfter) ||
        definitionAfter != id)
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
    void Reset() noexcept { ResetInPlace(*this); }
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
                !worldvisuals::DroppedName(id))
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
