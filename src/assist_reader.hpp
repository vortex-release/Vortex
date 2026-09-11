#pragma once
#include "assist_features.hpp"
#include "combat_reader.hpp"
#include "cs2_camera.hpp"
namespace awareness::assist {
struct Addresses {
    std::uintptr_t listSlot{}, pawnSlot{}, controllerSlot{}, angles{};
};
} // namespace awareness::assist
namespace awareness::cs2 {
inline bool AssistPawn(const Memory &m, std::uintptr_t pawn, std::uint32_t &handle, std::uint8_t &team) noexcept {
    std::uintptr_t identity{}, name{}, scene{};
    std::array<char, 32> type{};
    std::int32_t health{};
    std::uint8_t life{}, dormant{};
    return FullHandle(m, pawn, handle) && m.Field(pawn, offsets::Identity, identity) &&
           m.Field(identity, offsets::DesignerName, name) && m.Read(name, type) &&
           std::memcmp(type.data(), "cs_player_pawn", sizeof("cs_player_pawn")) == 0 &&
           m.Field(pawn, offsets::Health, health) && health > 0 && health <= 1000 &&
           m.Field(pawn, offsets::LifeState, life) && life == 0 && m.Field(pawn, offsets::Team, team) &&
           (team == 2 || team == 3) && m.Field(pawn, offsets::SceneNode, scene) &&
           m.Field(scene, offsets::Dormant, dormant) && !dormant;
}
inline bool ReadAssistSample(const Memory &m, const assist::Addresses &a, assist::Sample &out) noexcept {
    out = {};
    std::uintptr_t list{}, pawn{}, controller{}, afterPawn{}, afterController{}, movement{};
    std::uint8_t team{}, scoped{}, wait{}, reloading{}, immune{};
    NativeViewAngles angles;
    assist::Sample s;
    std::uint32_t controllerHandle{}, controllerPawn{}, controllerAfter{};
    if (!m.Read(a.listSlot, list) || !m.Read(a.pawnSlot, pawn) || !m.Read(a.controllerSlot, controller) ||
        !AssistPawn(m, pawn, s.owner, team) || EntityAt(m, list, s.owner) != pawn ||
        !FullHandle(m, controller, controllerHandle) || EntityAt(m, list, controllerHandle) != controller ||
        !m.Field(controller, offsets::ControllerPawn, controllerPawn) || controllerPawn != s.owner ||
        !m.Field(pawn, offsets::MovementFlags, s.flags) || !m.Field(pawn, offsets::ActualMoveType, s.moveType) ||
        !m.Field(pawn, offsets::WaterLevel, s.water) || !std::isfinite(s.water) ||
        !m.Field(pawn, offsets::AbsVelocity, s.velocity) || !Finite(s.velocity) || !m.Read(a.angles, angles) ||
        !std::isfinite(angles.pitch) || std::abs(angles.pitch) > 89.1f || !std::isfinite(angles.yaw) ||
        std::abs(angles.yaw) > 180.1f)
        return false;
    s.pitch = angles.pitch;
    s.yaw = angles.yaw;
    s.grounded = (s.flags & 1) != 0;
    s.frozen = (s.flags & (1u << 5)) != 0;
    s.walking = s.moveType == 2;
    if (m.Field(pawn, offsets::MovementServices, movement)) {
        float value{};
        if (m.Field(movement, offsets::MovementMaxSpeed, value) && std::isfinite(value) && value > 0 && value <= 1000)
            s.maxSpeed = value;
        if (m.Field(movement, offsets::MovementFriction, value) && std::isfinite(value) && value > 0 && value <= 2)
            s.friction = value;
    }
    s.scoped = m.Field(pawn, offsets::IsScoped, scoped) && scoped == 1;
    std::uintptr_t services{}, weapon{};
    std::uint32_t nextAttack{}, weaponAfter{};
    std::int32_t clip{}, index{}, indexAfter{};
    if (m.Field(pawn, offsets::WeaponServices, services) && m.Field(services, offsets::ActiveWeapon, s.weaponHandle) &&
        (weapon = EntityAt(m, list, s.weaponHandle)) && FullHandle(m, weapon, weaponAfter) &&
        weaponAfter == s.weaponHandle &&
        m.Field(weapon, offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, s.weapon) &&
        combat::WeaponProfile(s.weapon) && m.Field(weapon, offsets::Clip1, clip) && clip > 0 && clip <= 250 &&
        m.Field(weapon, offsets::WeaponReload, reloading) && !reloading &&
        m.Field(pawn, offsets::WaitForNoAttack, wait) && !wait &&
        m.Field(controller, offsets::ControllerTick, s.tick) && s.tick > 0 &&
        m.Field(weapon, offsets::NextPrimaryTick, nextAttack) && nextAttack <= s.tick)
        s.weaponReady = true;
    if (m.Field(pawn, offsets::CrosshairIndex, index) && index > 0 &&
        static_cast<unsigned>(index) <= offsets::EntryMask) {
        const auto target = EntityAt(m, list, static_cast<unsigned>(index));
        std::uint32_t targetHandle{}, after{};
        std::uint8_t targetTeam{};
        if (target != pawn && AssistPawn(m, target, targetHandle, targetTeam) && targetTeam != team &&
            (targetHandle & offsets::EntryMask) == static_cast<unsigned>(index) &&
            m.Field(target, offsets::SpawnImmunity, immune) && !immune && FullHandle(m, target, after) &&
            after == targetHandle && m.Field(pawn, offsets::CrosshairIndex, indexAfter) && indexAfter == index) {
            s.target = targetHandle;
            s.enemy = true;
        }
    }
    std::uint32_t ownerAfter{}, activeAfter{};
    if (!m.Read(a.pawnSlot, afterPawn) || afterPawn != pawn || !m.Read(a.controllerSlot, afterController) ||
        afterController != controller || !FullHandle(m, controller, controllerAfter) ||
        controllerAfter != controllerHandle || !m.Field(controller, offsets::ControllerPawn, controllerPawn) ||
        controllerPawn != s.owner || !FullHandle(m, pawn, ownerAfter) || ownerAfter != s.owner)
        return false;
    if (s.weaponReady && (!m.Field(services, offsets::ActiveWeapon, activeAfter) || activeAfter != s.weaponHandle ||
                          !FullHandle(m, weapon, weaponAfter) || weaponAfter != s.weaponHandle))
        s.weaponReady = false;
    s.valid = true;
    out = s;
    return true;
}
} // namespace awareness::cs2
