#pragma once
#include "combat_features.hpp"
#include "trajectory_reader.hpp"
namespace awareness::cs2 {
inline bool FullHandle(const Memory &m, std::uintptr_t entity, std::uint32_t &handle) noexcept {
    std::uintptr_t identity{};
    return m.Field(entity, offsets::Identity, identity) && m.Field(identity, 0x10, handle) && handle &&
           handle != 0xffffffff;
}
// player_hurt gives a body region, not an exact surface impact coordinate.
// Freeze the region's bone position at event time; fall back to posture-aware bounds.
inline bool ReadHitPosition(const Memory &m, std::uintptr_t pawn, std::uint32_t expected, int hitgroup,
                            Vector3 &out) noexcept {
    out = {};
    std::uintptr_t scene{}, collision{}, sceneAfter{};
    std::uint32_t handle{};
    EntitySnapshot entity;
    if (!expected || !FullHandle(m, pawn, handle) || handle != expected || !m.Field(pawn, offsets::SceneNode, scene) ||
        !m.Field(scene, offsets::Origin, entity.origin) || !PlausiblePosition(entity.origin))
        return false;
    Vector3 mins{-16, -16, 0}, maxs{16, 16, 72}, lo{}, hi{};
    if (m.Field(pawn, offsets::Collision, collision) && ReadBounds(m, collision, lo, hi) && hi.z - lo.z >= 16 &&
        hi.z - lo.z <= 144 && hi.x - lo.x <= 160 && hi.y - lo.y <= 160) {
        mins = lo;
        maxs = hi;
    }
    const float height = hitgroup == 1                      ? .94f
                         : hitgroup == 8                    ? .82f
                         : hitgroup == 3                    ? .48f
                         : (hitgroup == 6 || hitgroup == 7) ? .25f
                                                            : .68f;
    Vector3 position =
        entity.origin + Vector3{(mins.x + maxs.x) * .5f, (mins.y + maxs.y) * .5f, mins.z + (maxs.z - mins.z) * height};
    const auto bone = hitgroup == 1   ? TargetBone::Head
                      : hitgroup == 8 ? TargetBone::Neck
                      : hitgroup == 3 ? TargetBone::Pelvis
                                      : TargetBone::Chest;
    EntityBones bones;
    if (hitgroup != 6 && hitgroup != 7 && ReadTargetBone(m, scene, entity, bone, bones))
        position = bones.positions[static_cast<int>(bone)];
    if (!FullHandle(m, pawn, handle) || handle != expected || !m.Field(pawn, offsets::SceneNode, sceneAfter) ||
        sceneAfter != scene || !PlausiblePosition(position))
        return false;
    out = position;
    return true;
}
inline bool ReadRecoilMetadata(const Memory &m, std::uintptr_t list, std::uintptr_t pawn,
                               combat::RecoilSample &s) noexcept {
    s = {};
    std::uintptr_t services{};
    std::uint32_t after{};
    std::uint16_t id{};
    Vector3 eye{};
    if (!PlayerEye(m, pawn, eye) || !FullHandle(m, pawn, s.owner) || EntityAt(m, list, s.owner) != pawn ||
        !m.Field(pawn, offsets::WeaponServices, services) || !m.Field(services, offsets::ActiveWeapon, s.weaponHandle))
        return false;
    const auto weapon = EntityAt(m, list, s.weaponHandle);
    if (!FullHandle(m, weapon, after) || after != s.weaponHandle ||
        !m.Field(weapon, offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, id) ||
        !combat::WeaponProfile(id) || !m.Field(pawn, offsets::ShotsFired, s.shots) || s.shots > 300 ||
        !m.Field(weapon, offsets::RecoilIndex, s.recoilIndex) || !std::isfinite(s.recoilIndex) || s.recoilIndex < 0 ||
        s.recoilIndex > 300 || !m.Field(weapon, offsets::LastShotTime, s.lastShot) || !std::isfinite(s.lastShot) ||
        !FullHandle(m, pawn, after) || after != s.owner)
        return false;
    s.weapon = id;
    return true;
}
inline bool ReadMouseSensitivity(const Memory &m, std::uintptr_t client, std::uintptr_t pawn, float &value) noexcept {
    std::uintptr_t settings{};
    float base{}, overrideValue{}, zoom{};
    if (!m.Field(client, offsets::Sensitivity, settings) || !m.Field(settings, offsets::SensitivityValue, base) ||
        !std::isfinite(base) || base < .01f || base > 100)
        return false;
    if (m.Field(pawn, offsets::MouseSensitivity, overrideValue) && std::isfinite(overrideValue) && overrideValue > 0)
        base = overrideValue;
    if (!m.Field(pawn, offsets::FovSensitivity, zoom) || !std::isfinite(zoom) || zoom <= 0 || zoom > 10)
        return false;
    // The pawn override already includes zoom scaling (client AE1990..AE1998).
    // Zero means normal sensitivity; do not multiply the scoped value a second time.
    value = base;
    return std::isfinite(value) && value >= .01f && value <= 100;
}
// Build-verified queued bullet path: capture server/predicted endpoints before
// the engine changes the origin or applies tracer-frequency/visibility filters.
inline bool ReadDirectShot(const Memory &m, std::uintptr_t list, std::uintptr_t services, std::uintptr_t weapon,
                           std::uintptr_t start, std::uintptr_t end, flight::Shot &out) noexcept {
    out = {};
    std::uintptr_t pawn{}, current{};
    std::uint32_t owner{}, weaponHandle{}, weaponOwner{}, after{};
    std::uint8_t team{};
    if (!m.Field(services, trajectory_offsets::BulletPawn, pawn) || !FullHandle(m, pawn, owner) ||
        EntityAt(m, list, owner) != pawn || !m.Field(pawn, offsets::WeaponServices, current) || current != services ||
        !FullHandle(m, weapon, weaponHandle) || EntityAt(m, list, weaponHandle) != weapon ||
        !m.Field(weapon, offsets::EntityOwner, weaponOwner) || !m.Field(pawn, offsets::Team, team) || team < 2 ||
        team > 3 || !m.Read(start, out.start) || !m.Read(end, out.end) || !PlausiblePosition(out.start) ||
        !PlausiblePosition(out.end) || Distance(out.start, out.end) < 1 || Distance(out.start, out.end) > 32768 ||
        !FullHandle(m, pawn, after) || after != owner)
        return false;
    if (weaponOwner != owner) {
        // Some predicted weapon records have no owner handle yet. The verified
        // pawn's active-weapon reference is an independent ownership proof.
        std::uintptr_t weaponServices{};
        std::uint32_t active{};
        if ((weaponOwner && weaponOwner != 0xffffffff) || !m.Field(pawn, offsets::WeaponServices, weaponServices) ||
            !m.Field(weaponServices, offsets::ActiveWeapon, active) || active != weaponHandle ||
            EntityAt(m, list, active) != weapon)
            return false;
    }
    out.shooter = owner & offsets::EntryMask;
    out.team = team;
    return true;
}
inline bool ReadKit(const Memory &m, std::uintptr_t pawn, bool &kit) noexcept {
    std::uintptr_t services{};
    std::uint8_t value{};
    if (!m.Field(pawn, offsets::ItemServices, services) || !m.Field(services, offsets::HasDefuser, value) || value > 1)
        return false;
    kit = value != 0;
    return true;
}
inline bool ReadBomb(const Memory &m, std::uintptr_t list, std::uintptr_t entity, std::uintptr_t localPawn, float now,
                     std::uint32_t expected, combat::Bomb &out) noexcept {
    out = {};
    combat::Bomb b;
    std::uint32_t after{}, defuser{};
    float blow{}, length{}, defuseAt{};
    std::uint8_t ticking{}, defused{}, exploded{}, defusing{};
    if (!std::isfinite(now) || !FullHandle(m, entity, b.handle) || b.handle != expected ||
        !m.Field(entity, offsets::BombTicking, ticking) || ticking > 1 ||
        !m.Field(entity, offsets::BombDefused, defused) || defused > 1 ||
        !m.Field(entity, offsets::BombExploded, exploded) || exploded > 1 ||
        !m.Field(entity, offsets::BombDefusing, defusing) || defusing > 1 ||
        !m.Field(entity, offsets::BombBlow, blow) || !std::isfinite(blow) ||
        !m.Field(entity, offsets::BombLength, length) || !std::isfinite(length) || length <= 0 || length > 180 ||
        !m.Field(entity, offsets::BombSite, b.site) || b.site < 0 || b.site > 1)
        return false;
    b.remaining = blow - now;
    b.ticking = ticking;
    b.defused = defused;
    b.exploded = exploded;
    b.defusing = defusing;
    if (b.remaining > length + 1 || b.remaining < -3 || !ticking || defused || exploded)
        return false;
    b.kitKnown = ReadKit(m, localPawn, b.hasKit);
    b.defuseLength = b.hasKit ? 5.f : 10.f;
    if (defusing) {
        if (!m.Field(entity, offsets::DefuseLength, b.defuseLength) || !std::isfinite(b.defuseLength) ||
            b.defuseLength < 0 || b.defuseLength > 15 || !m.Field(entity, offsets::DefuseCountdown, defuseAt) ||
            !std::isfinite(defuseAt))
            return false;
        b.defuseRemaining = std::clamp(defuseAt - now, 0.f, 15.f);
        if (m.Field(entity, offsets::BombDefuser, defuser)) {
            auto pawn = EntityAt(m, list, defuser);
            std::uint32_t h{};
            if (FullHandle(m, pawn, h) && h == defuser)
                b.kitKnown = ReadKit(m, pawn, b.hasKit);
        }
    }
    if (!FullHandle(m, entity, after) || after != expected)
        return false;
    b.remaining = std::max(0.f, b.remaining);
    b.valid = true;
    out = b;
    return true;
}
inline void FireBoundary(combat::Area &area, std::span<const Vector3> positions,
                         std::span<const std::uint8_t> burning) noexcept {
    std::array<Vector3, 512> points{};
    std::size_t count{};
    for (std::size_t k = 0; k < positions.size() && k < burning.size() && k < 64; ++k) {
        if (burning[k] == 0 || !PlausiblePosition(positions[k]))
            continue;
        for (int i = 0; i < 8; ++i) {
            const float angle = i * .7853981634f;
            points[count++] = positions[k] + Vector3{std::cos(angle) * 25, std::sin(angle) * 25, 2};
        }
    }
    std::sort(points.begin(), points.begin() + count,
              [](auto a, auto b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    const auto cross = [](Vector3 a, Vector3 b, Vector3 c) {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    };
    std::array<Vector3, 1024> hull{};
    std::size_t n{};
    for (std::size_t i = 0; i < count; ++i) {
        while (n >= 2 && cross(hull[n - 2], hull[n - 1], points[i]) <= 0)
            --n;
        hull[n++] = points[i];
    }
    const auto lower = n;
    for (std::size_t i = count > 0 ? count - 1 : 0; i-- > 0;) {
        while (n > lower && cross(hull[n - 2], hull[n - 1], points[i]) <= 0)
            --n;
        hull[n++] = points[i];
    }
    if (n > 1)
        --n;
    area.boundaryCount = static_cast<std::uint32_t>(std::min<std::size_t>(n, area.boundary.size()));
    for (std::size_t i = 0; i < area.boundaryCount; ++i)
        area.boundary[i] = hull[i * n / area.boundaryCount];
}
class WorldReader {
    struct Entry {
        std::uint32_t handle{};
        int type{};
    };
    std::array<Entry, 65> entries_{};
    std::size_t count_{};
    std::uintptr_t list_{};
    EntityDiscovery discovery_;
    double previous_{};

  public:
    void Reset() noexcept { *this = {}; }
    bool Update(const Memory &m, std::uintptr_t list, std::uintptr_t localPawn, float gameTime, double now,
                combat::WorldSnapshot &out, const combat::InfernoEvents &events = {}) noexcept {
        out = {};
        out.gameTime = gameTime;
        if (!list || !std::isfinite(now)) {
            Reset();
            return false;
        }
        if (list != list_ || now < previous_) {
            Reset();
            list_ = list;
        }
        for (std::size_t i = 0; i < count_;) {
            std::uint32_t h{};
            if (!FullHandle(m, EntityAt(m, list, entries_[i].handle), h) || h != entries_[i].handle)
                entries_[i] = entries_[--count_];
            else
                ++i;
        }
        // Events identify newly lit infernos immediately, independently of designer-name discovery.
        for (const auto &event : events.Entries()) {
            if (!event.index || now < event.time || now - event.time > 7)
                continue;
            const auto entity = EntityAt(m, list, event.index);
            std::uint32_t handle{};
            if (!FullHandle(m, entity, handle) || (handle & offsets::EntryMask) != event.index ||
                (event.handle && event.handle != handle) || (!event.handle && now - event.time > .5))
                continue;
            bool known{};
            for (std::size_t i = 0; i < count_; ++i)
                known |= entries_[i].handle == handle;
            if (!known && count_ < entries_.size())
                entries_[count_++] = {handle, 2};
        }
        if (!discovery_.Scan(m, list, now, [&](std::uintptr_t entity, const char *name) {
                const int type = !std::strcmp(name, "planted_c4")                ? 1
                                 : !std::strcmp(name, "inferno")                 ? 2
                                 : !std::strcmp(name, "smokegrenade_projectile") ? 3
                                                                                 : 0;
                std::uint32_t handle{};
                if (!type || !FullHandle(m, entity, handle))
                    return;
                for (std::size_t i = 0; i < count_; ++i)
                    if (entries_[i].handle == handle)
                        return;
                if (count_ < entries_.size())
                    entries_[count_++] = {handle, type};
            })) {
            Reset();
            return false;
        }
        previous_ = now;
        out.discovered = static_cast<std::uint32_t>(count_);
        for (std::size_t i = 0; i < count_; ++i) {
            const auto &e = entries_[i];
            const auto entity = EntityAt(m, list, e.handle);
            std::uint32_t handle{}, after{};
            if (!FullHandle(m, entity, handle) || handle != e.handle)
                continue;
            if (e.type == 1) {
                combat::Bomb b;
                if (ReadBomb(m, list, entity, localPawn, gameTime, e.handle, b))
                    out.bomb = b;
                continue;
            }
            combat::Area a;
            a.handle = e.handle;
            if (e.type == 3) {
                std::uint8_t active{};
                if (!m.Field(entity, offsets::SmokeEffect, active) || active != 1 ||
                    !m.Field(entity, offsets::SmokeCenter, a.center) || !PlausiblePosition(a.center))
                    continue;
                // Geometric proxy for the standard smoke volume; active lifetime follows the entity.
                a.type = combat::AreaType::Smoke;
                a.radius = 144;
                a.height = 160;
            } else {
                int count{};
                std::array<Vector3, 64> positions{};
                std::array<std::uint8_t, 64> burning{};
                ++out.fireEntities;
                if (!m.Field(entity, offsets::FireCount, count) || count < 1 || count > 64 ||
                    !m.read(m.context, entity + offsets::FirePositions, positions.data(), sizeof(Vector3) * count) ||
                    !m.read(m.context, entity + offsets::FireBurning, burning.data(), count)) {
                    ++out.fireReadFailures;
                    continue;
                }
                unsigned active{};
                Vector3 low{1e6f, 1e6f, 1e6f}, high{-1e6f, -1e6f, -1e6f};
                for (int k = 0; k < count; ++k)
                    if (burning[k] != 0 && PlausiblePosition(positions[k])) {
                        const auto p = positions[k];
                        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
                        ++active;
                    }
                out.burningCells += active;
                if (!active)
                    continue;
                a.center = flight::Scale(low + high, .5f);
                a.center.z = low.z + 2;
                a.radius = std::clamp(Distance(low, high) * .5f + 25, 25.f, 300.f);
                a.height = 6;
                a.type = combat::AreaType::Fire;
                FireBoundary(a, {positions.data(), static_cast<std::size_t>(count)},
                             {burning.data(), static_cast<std::size_t>(count)});
            }
            if (FullHandle(m, entity, after) && after == e.handle && out.areaCount < out.areas.size())
                out.areas[out.areaCount++] = a;
        }
        return true;
    }
};
} // namespace awareness::cs2
