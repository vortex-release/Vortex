#pragma once
#include "in_place_reset.hpp"
#include "cs2_reader.hpp"
#include <awareness/Trajectories.hpp>

namespace awareness::cs2 {
inline bool EntityName(const Memory &m, std::uintptr_t entity, char (&name)[64]) noexcept {
    std::uintptr_t identity{}, str{};
    name[0] = 0;
    if (!m.Field(entity, offsets::Identity, identity) || !m.Field(identity, offsets::DesignerName, str))
        return false;
    std::array<char, 64> text{};
    if (m.Read(str, text)) {
        std::memcpy(name, text.data(), text.size());
        return std::memchr(name, 0, 64) != nullptr;
    }
    for (unsigned i = 0; i < 64; ++i) {
        if (!m.Read(str + i, name[i]))
            return false;
        if (!name[i])
            return true;
    }
    name[63] = 0;
    return false;
}
inline flight::Utility ProjectileType(const char *name) noexcept {
    if (!std::strcmp(name, "hegrenade_projectile"))
        return flight::Utility::HE;
    if (!std::strcmp(name, "smokegrenade_projectile"))
        return flight::Utility::Smoke;
    if (!std::strcmp(name, "flashbang_projectile"))
        return flight::Utility::Flash;
    if (!std::strcmp(name, "molotov_projectile") || !std::strcmp(name, "incgrenade_projectile"))
        return flight::Utility::Fire;
    if (!std::strcmp(name, "decoy_projectile"))
        return flight::Utility::Decoy;
    return flight::Utility::None;
}
struct ProjectileFrame {
    std::array<flight::Projectile, 64> values{};
    std::uint32_t count{};
    std::uintptr_t generation{};
};
inline bool ReadProjectile(const Memory &m, std::uintptr_t entity, flight::Utility type, flight::Projectile &result,
                           std::uint32_t expected = 0) noexcept {
    std::uintptr_t identity{}, scene{};
    std::uint32_t handle{}, after{};
    std::uint8_t exploded{}, dormant{};
    Vector3 position{};
    float spawn{};
    if (!m.Field(entity, offsets::Identity, identity) || !m.Field(identity, 0x10, handle) || handle == 0xffffffff ||
        (expected && expected != handle) || !m.Field(entity, offsets::SceneNode, scene) ||
        !m.Field(scene, offsets::Dormant, dormant) || dormant || !m.Field(scene, offsets::Origin, position) ||
        !PlausiblePosition(position) || !m.Field(entity, offsets::ProjectileExploded, exploded) || exploded ||
        !m.Field(entity, offsets::ProjectileSpawn, spawn) || !std::isfinite(spawn))
        return false;
    if (type == flight::Utility::Smoke && (!m.Field(entity, offsets::SmokeEffect, exploded) || exploded))
        return false;
    if (!m.Field(identity, 0x10, after) || after != handle)
        return false;
    result = {handle, type, position};
    return true;
}
inline bool ReadProjectiles(const Memory &m, std::uintptr_t list, ProjectileFrame &result) noexcept {
    result = {};
    result.generation = list;
    if (!list)
        return false;
    for (std::uint32_t i = 1; i <= offsets::EntryMask && result.count < result.values.size(); ++i) {
        std::uintptr_t chunk{}, entity{};
        if (!m.Field(list, offsets::EntityTable + sizeof(std::uintptr_t) * (i >> 9), chunk) || !chunk) {
            i |= 511; // Skip unallocated chunks, including the sparse gap before client-only entities.
            continue;
        }
        m.Field(chunk, offsets::EntityStride * (i & 511), entity);
        if (!entity)
            continue;
        char name[64]{};
        if (!EntityName(m, entity, name))
            continue;
        const auto type = ProjectileType(name);
        if (type == flight::Utility::None)
            continue;
        if (ReadProjectile(m, entity, type, result.values[result.count]))
            ++result.count;
    }
    return true;
}
// The reported highest entity does not bound the client-only chunks in this build.
// Walk allocated chunks with a fixed slot budget; refresh known entities every frame.
class EntityDiscovery {
    struct NameCache {
        std::uintptr_t entity{};
        std::uint32_t handle{};
        char name[64]{};
    };
    std::array<NameCache, 1024> names_{};
    std::uintptr_t list_{};
    int cursor_{};
    double next_{}, previous_{};

  public:
    void Reset() noexcept { ResetInPlace(*this); }
    template <class Visit> bool Scan(const Memory &m, std::uintptr_t list, double now, Visit &&visit) noexcept {
        if (!list || !std::isfinite(now)) {
            Reset();
            return false;
        }
        if (list_ != list || now < previous_) {
            Reset();
            list_ = list;
        }
        previous_ = now;
        if (!cursor_ && now >= next_) {
            cursor_ = 1;
            next_ = now + .05;
        }
        unsigned budget{};
        while (cursor_ > 0 && cursor_ <= static_cast<int>(offsets::EntryMask) && budget < 256) {
            std::uintptr_t chunk{};
            const auto end = (cursor_ | 511) + 1;
            if (!m.Field(list, offsets::EntityTable + sizeof(std::uintptr_t) * (cursor_ >> 9), chunk) || !chunk) {
                cursor_ = end;
                continue;
            }
            while (cursor_ < end && budget < 256) {
                std::uintptr_t entity{};
                m.Field(chunk, offsets::EntityStride * (cursor_ & 511), entity);
                std::uint32_t handle{};
                std::uintptr_t identity{};
                if (entity && m.Field(entity, offsets::Identity, identity) && m.Field(identity, 0x10, handle) &&
                    handle != 0xffffffff && (handle & offsets::EntryMask) == static_cast<unsigned>(cursor_)) {
                    auto &cached = names_[static_cast<unsigned>(cursor_) % names_.size()];
                    if (cached.entity != entity || cached.handle != handle) {
                        cached = {};
                        if (EntityName(m, entity, cached.name)) {
                            cached.entity = entity;
                            cached.handle = handle;
                        }
                    }
                    if (cached.entity)
                        visit(entity, cached.name);
                }
                ++cursor_;
                ++budget;
            }
        }
        if (cursor_ > static_cast<int>(offsets::EntryMask))
            cursor_ = 0;
        return true;
    }
};
class ProjectileTracker {
    ProjectileFrame tracked_;
    std::array<double, 64> lastRead_{};
    EntityDiscovery discovery_;
    double previous_{};

  public:
    void Reset() noexcept { ResetInPlace(*this); }
    bool Update(const Memory &m, std::uintptr_t list, double now, ProjectileFrame &result) noexcept {
        result = {};
        if (!list || !std::isfinite(now)) {
            Reset();
            return false;
        }
        if (tracked_.generation != list || now < previous_) {
            Reset();
            tracked_.generation = list;
        }
        previous_ = now;
        if (!discovery_.Scan(m, list, now, [&](std::uintptr_t entity, const char *name) {
                const auto type = ProjectileType(name);
                flight::Projectile sample;
                if (type == flight::Utility::None || !ReadProjectile(m, entity, type, sample))
                    return;
                for (std::uint32_t i = 0; i < tracked_.count; ++i)
                    if (tracked_.values[i].handle == sample.handle)
                        return;
                if (tracked_.count < tracked_.values.size()) {
                    lastRead_[tracked_.count] = now;
                    tracked_.values[tracked_.count++] = sample;
                }
            })) {
            Reset();
            return false;
        }
        result.generation = list;
        std::uint32_t retained{};
        for (std::uint32_t i = 0; i < tracked_.count; ++i) {
            auto sample = tracked_.values[i];
            auto last = lastRead_[i];
            if (ReadProjectile(m, EntityAt(m, list, sample.handle), sample.type, result.values[result.count],
                               sample.handle)) {
                sample = result.values[result.count++];
                last = now;
            } else if (now - last > .25) {
                continue;
            }
            // Retry the full identity briefly without publishing stale positions or
            // waiting for the budgeted discovery scan to wrap around the entire list.
            tracked_.values[retained] = sample;
            lastRead_[retained++] = last;
        }
        tracked_.count = retained;
        return true;
    }
};
inline bool PlayerEye(const Memory &m, std::uintptr_t pawn, Vector3 &eye) noexcept {
    std::uintptr_t scene{};
    Vector3 origin{}, offset{};
    std::uint8_t life{};
    if (!m.Field(pawn, offsets::LifeState, life) || life || !m.Field(pawn, offsets::SceneNode, scene) ||
        !m.Field(scene, offsets::Origin, origin) || !m.Field(pawn, offsets::ViewOffset, offset) ||
        !PlausiblePosition(origin) || !Finite(offset) || std::abs(offset.z) > 128)
        return false;
    eye = origin + offset;
    return true;
}
inline bool ReadTracerEffect(const Memory &m, std::uintptr_t list, std::uintptr_t effect, flight::Shot &shot,
                             const Vector3 *muzzle = nullptr) noexcept {
    shot = {};
    std::uint32_t handle{}, current{};
    if (muzzle)
        shot.start = *muzzle;
    if (!m.Field(effect, offsets::EffectOrigin, shot.end) ||
        (!muzzle && !m.Field(effect, offsets::EffectStart, shot.start)) || !PlausiblePosition(shot.start) ||
        !PlausiblePosition(shot.end) || Distance(shot.start, shot.end) < 1 || Distance(shot.start, shot.end) > 32768 ||
        !m.Field(effect, offsets::EffectEntity, handle))
        return false;
    for (int depth = 0; depth < 3; ++depth) {
        const auto entity = EntityAt(m, list, handle);
        std::uintptr_t identity{};
        if (!entity || !m.Field(entity, offsets::Identity, identity) || !m.Field(identity, 0x10, current) ||
            current != handle)
            return false;
        char name[64]{};
        if (!EntityName(m, entity, name))
            return false;
        if (!std::strcmp(name, "cs_player_pawn") || !std::strcmp(name, "c_cs_player_for_precache") ||
            !std::strcmp(name, "player")) {
            std::uint8_t team{};
            if (!m.Field(entity, offsets::Team, team) || team < 2 || team > 3)
                return false;
            shot.shooter = handle & offsets::EntryMask;
            shot.team = team;
            return true;
        }
        std::uint32_t owner{};
        if (!m.Field(entity, offsets::EntityOwner, owner) || owner == handle || owner == 0xffffffff)
            return false;
        handle = owner;
    }
    return false;
}
inline bool ReadThrow(const Memory &m, std::uintptr_t list, std::uintptr_t pawn, Vector3 angles,
                      flight::Throw &result) noexcept {
    result = {};
    std::uintptr_t services{}, identity{};
    std::uint32_t weaponHandle{}, current{}, after{};
    std::uint16_t id{};
    if (!Finite(angles) || std::abs(angles.x) > 89.1f || !PlayerEye(m, pawn, result.eye) ||
        !m.Field(pawn, offsets::AbsVelocity, result.velocity) || !Finite(result.velocity) ||
        !m.Field(pawn, offsets::WeaponServices, services) || !m.Field(services, offsets::ActiveWeapon, weaponHandle))
        return false;
    const auto weapon = EntityAt(m, list, weaponHandle);
    if (!m.Field(weapon, offsets::Identity, identity) || !m.Field(identity, 0x10, current) || current != weaponHandle ||
        !m.Field(weapon, offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, id))
        return false;
    result.type = flight::Weapon(id);
    if (result.type == flight::Utility::None)
        return false;
    float throwTime{};
    std::uint8_t pin{};
    if (!m.Field(weapon, offsets::ThrowTime, throwTime) || !std::isfinite(throwTime) || throwTime > 0 ||
        !m.Field(weapon, offsets::PinPulled, pin))
        return false;
    if (pin && (!m.Field(weapon, offsets::ThrowStrength, result.strength) || !std::isfinite(result.strength)))
        return false;
    if (!m.Field(identity, 0x10, after) || after != weaponHandle || !m.Field(services, offsets::ActiveWeapon, after) ||
        after != weaponHandle)
        return false;
    result.pitch = angles.x;
    result.yaw = std::remainder(angles.y, 360.f);
    return true;
}
} // namespace awareness::cs2
