#pragma once
#include <awareness/OverlayApi.hpp>
#include "cs2_offsets.hpp"
#include "preview_pose.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace awareness::cs2 {
struct Memory {
    void *context{};
    bool (*read)(void *, std::uintptr_t, void *, std::size_t) noexcept {};
    template <class T> bool Read(std::uintptr_t address, T &value) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "Memory reads require a trivially copyable value");
        value = {};
        if (!address || !read || address > (std::numeric_limits<std::uintptr_t>::max)() - (sizeof(T) - 1))
            return false;
        T candidate{};
        if (!read(context, address, &candidate, sizeof(candidate)))
            return false;
        value = candidate;
        return true;
    }
    template <class T> bool Field(std::uintptr_t object, std::uintptr_t offset, T &value) const noexcept {
        if (!object || object > (std::numeric_limits<std::uintptr_t>::max)() - offset) {
            value = {};
            return false;
        }
        return Read(object + offset, value);
    }
};
struct Globals {
    std::uintptr_t entitySlot{}, matrix{}, localControllerSlot{}, localPawnSlot{};
};
struct ReadReport {
    std::uint32_t controllers{}, pawns{}, failedReads{}, inactivePawns{}, duplicatePawns{};
    bool matrixValid{}, cameraValid{}, listValid{};
    std::uint32_t bonePositions{}, failedBoneReads{};
};
inline bool PlausiblePosition(Vector3 v) noexcept {
    return Finite(v) && std::abs(v.x) < 1e7f && std::abs(v.y) < 1e7f && std::abs(v.z) < 1e7f;
}
inline bool CameraFromMatrix(const Matrix4x4 &m, Vector3 &camera) noexcept {
    // The camera is the intersection of the perspective x=0, y=0 and w=0 planes.
    // This also works for a spectator/free camera, without relying on the local pawn.
    double a[3][4]{};
    const int rows[]{0, 1, 3};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            a[i][j] = m.m[rows[i]][j];
        a[i][3] = -m.m[rows[i]][3];
    }
    for (int column = 0; column < 3; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 3; ++row)
            if (std::abs(a[row][column]) > std::abs(a[pivot][column]))
                pivot = row;
        if (std::abs(a[pivot][column]) < 1e-9)
            return false;
        for (int j = 0; j < 4; ++j)
            std::swap(a[column][j], a[pivot][j]);
        const double divisor = a[column][column];
        for (int j = column; j < 4; ++j)
            a[column][j] /= divisor;
        for (int row = 0; row < 3; ++row)
            if (row != column) {
                const double factor = a[row][column];
                for (int j = column; j < 4; ++j)
                    a[row][j] -= factor * a[column][j];
            }
    }
    camera = {static_cast<float>(a[0][3]), static_cast<float>(a[1][3]), static_cast<float>(a[2][3])};
    return PlausiblePosition(camera);
}
inline std::uintptr_t EntityAt(const Memory &memory, std::uintptr_t list, std::uint32_t handle) noexcept {
    if (handle == 0xFFFFFFFFu)
        return 0;
    const auto index = handle & offsets::EntryMask;
    if (!index)
        return 0;
    std::uintptr_t chunk{}, entity{}, identity{};
    if (!memory.Field(list, offsets::EntityTable + sizeof(std::uintptr_t) * (index / offsets::EntriesPerChunk), chunk))
        return 0;
    const auto entryOffset = offsets::EntityStride * (index % offsets::EntriesPerChunk);
    if (!memory.Field(chunk, entryOffset, entity) || !entity)
        return 0;
    // Validate both directions before interpreting any pawn/controller fields.
    if (!memory.Field(entity, offsets::Identity, identity) || identity != chunk + entryOffset)
        return 0;
    std::uint32_t storedHandle{};
    if (!memory.Field(identity, 0x10, storedHandle) || (storedHandle & offsets::EntryMask) != index)
        return 0;
    return entity;
}
inline bool ReadBounds(const Memory &memory, std::uintptr_t collision, Vector3 &mins, Vector3 &maxs) noexcept {
    if (!memory.Field(collision, offsets::Mins, mins) || !memory.Field(collision, offsets::Maxs, maxs) ||
        !Finite(mins) || !Finite(maxs))
        return false;
    const auto extent = maxs - mins;
    return extent.x > 0.f && extent.y > 0.f && extent.z > 0.f && extent.x < 4096.f && extent.y < 4096.f &&
           extent.z < 4096.f;
}
using BoneTransform = PreviewJoint;
static_assert(sizeof(BoneTransform) == 32 && offsets::BoneStride == sizeof(BoneTransform));
static_assert(offsets::BoneHead == static_cast<int>(TargetBone::Head) &&
              offsets::BoneNeck == static_cast<int>(TargetBone::Neck) &&
              offsets::BoneChest == static_cast<int>(TargetBone::Chest) &&
              offsets::BonePelvis == static_cast<int>(TargetBone::Pelvis));
inline bool ReadTargetBone(const Memory &memory, std::uintptr_t scene, const EntitySnapshot &entity,
                           TargetBone selected, EntityBones &result) noexcept {
    result = {};
    const int index = static_cast<int>(selected);
    if constexpr (offsets::BoneLayoutBuild != offsets::ExpectedBuild)
        return false;
    if (!ValidTargetBone(index))
        return false;
    std::uintptr_t array{};
    std::uint16_t count{};
    if (!memory.Field(scene, offsets::ModelState + offsets::BoneArray, array) || array < 0x10000 || (array & 0xF) ||
        !memory.Field(scene, offsets::ModelState + offsets::BoneCount, count) || count <= index || count > 1024)
        return false;
    BoneTransform bone{};
    if (!memory.Field(array, static_cast<std::uintptr_t>(index) * offsets::BoneStride, bone) ||
        !PlausiblePosition(bone.position))
        return false;
    float norm{};
    for (float q : bone.rotation) {
        if (!std::isfinite(q))
            return false;
        norm += q * q;
    }
    if (!std::isfinite(bone.scale) || norm < .25f || norm > 4.f || Distance(bone.position, entity.origin) > 512.f)
        return false;
    // Reject arrays replaced/resized during sampling instead of publishing a stale pose.
    std::uintptr_t after{};
    std::uint16_t afterCount{};
    if (!memory.Field(scene, offsets::ModelState + offsets::BoneArray, after) || after != array ||
        !memory.Field(scene, offsets::ModelState + offsets::BoneCount, afterCount) || afterCount != count)
        return false;
    result.positions[index] = bone.position;
    result.validMask = 1u << index;
    return true;
}
inline bool ReadPreviewPose(const Memory &memory, std::uintptr_t scene, const EntitySnapshot &entity,
                            PreviewPose &out) noexcept {
    out = {};
    if constexpr (offsets::BoneLayoutBuild != offsets::ExpectedBuild)
        return false;
    std::uintptr_t array{}, after{};
    std::uint16_t count{}, afterCount{};
    if (!entity.valid || entity.dormant || entity.health <= 0 ||
        !memory.Field(scene, offsets::ModelState + offsets::BoneArray, array) || array < 0x10000 || (array & 15) ||
        !memory.Field(scene, offsets::ModelState + offsets::BoneCount, count) || count < PreviewJointCount ||
        count > 1024)
        return false;
    PreviewPose candidate;
    if (!memory.Read(array, candidate.joints))
        return false;
    for (std::size_t i = 0; i < candidate.joints.size(); ++i) {
        const auto &joint = candidate.joints[i];
        float norm{};
        for (float q : joint.rotation)
            norm += q * q;
        if (!PlausiblePosition(joint.position) || Distance(joint.position, entity.origin) > 512 ||
            !std::isfinite(joint.scale) || joint.scale < .01f || joint.scale > 10 || !std::isfinite(norm) ||
            norm < .5f || norm > 1.5f) {
            // Optional jiggle/weapon joints can be absent at a lower animation LOD.
            if (i == 16 || i >= 23)
                continue;
            return false;
        }
        candidate.validMask |= 1u << i;
    }
    if (!memory.Field(scene, offsets::ModelState + offsets::BoneArray, after) || after != array ||
        !memory.Field(scene, offsets::ModelState + offsets::BoneCount, afterCount) || afterCount != count)
        return false;
    // Reject unrelated skeletons with implausible limb lengths.
    constexpr int limbs[][2]{{9, 10}, {10, 11}, {13, 14}, {14, 15}, {17, 18}, {18, 19}, {20, 21}, {21, 22}};
    for (const auto &limb : limbs) {
        const float length = Distance(candidate.joints[limb[0]].position, candidate.joints[limb[1]].position);
        if (length < 3 || length > 45)
            return false;
    }
    candidate.entity = entity;
    candidate.valid = true;
    out = candidate;
    return true;
}
inline bool ReadFrame(const Memory &memory, Globals globals, FrameSnapshot &frame, ReadReport &report,
                      TargetBone selected = TargetBone::Head) noexcept {
    frame = {};
    report = {};
    if (!memory.Read(globals.matrix, frame.viewProjection))
        return false;
    for (const auto &row : frame.viewProjection.m)
        for (float value : row)
            if (!std::isfinite(value))
                return false;
    report.matrixValid = true;
    report.cameraValid = CameraFromMatrix(frame.viewProjection, frame.cameraOrigin);
    if (!report.cameraValid)
        return false;
    std::uintptr_t list{}, localController{}, localPawn{};
    if (!memory.Read(globals.entitySlot, list) || !list)
        return false;
    report.listValid = true;
    memory.Read(globals.localControllerSlot, localController);
    memory.Read(globals.localPawnSlot, localPawn);
    std::uint8_t team{};
    if (!memory.Field(localController, offsets::Team, team) || team < 2 || team > 3)
        memory.Field(localPawn, offsets::Team, team);
    frame.localTeam = team == 2 || team == 3 ? team : 0;
    for (std::uint32_t slot = 1; slot <= MaxEntities; ++slot) {
        const auto controller = EntityAt(memory, list, slot);
        if (!controller)
            continue;
        ++report.controllers;
        std::uint32_t pawnHandle{};
        if (!memory.Field(controller, offsets::ControllerPawn, pawnHandle)) {
            ++report.failedReads;
            continue;
        }
        if (!pawnHandle || pawnHandle == 0xFFFFFFFFu) {
            ++report.inactivePawns;
            continue;
        }
        const auto pawn = EntityAt(memory, list, pawnHandle);
        if (!pawn) {
            ++report.failedReads;
            continue;
        }
        bool duplicate = false;
        for (std::uint32_t i = 0; i < frame.entityCount; ++i)
            if (frame.entities[i].id == (pawnHandle & offsets::EntryMask))
                duplicate = true;
        if (duplicate) {
            ++report.duplicatePawns;
            continue;
        }
        std::uintptr_t scene{}, collision{};
        std::int32_t health{}, maximum{};
        std::uint8_t pawnTeam{}, life{}, dormant{};
        EntitySnapshot entity;
        if (!memory.Field(pawn, offsets::SceneNode, scene) || !scene ||
            !memory.Field(scene, offsets::Origin, entity.origin) || !PlausiblePosition(entity.origin) ||
            !memory.Field(scene, offsets::Dormant, dormant) || !memory.Field(pawn, offsets::Health, health) ||
            health < 0 || health > 10000 || !memory.Field(pawn, offsets::MaxHealth, maximum) || maximum < 1 ||
            maximum > 10000 || !memory.Field(pawn, offsets::Team, pawnTeam) || pawnTeam > 3 ||
            !memory.Field(pawn, offsets::LifeState, life)) {
            ++report.failedReads;
            continue;
        }
        memory.Field(pawn, offsets::Collision, collision);
        if (!ReadBounds(memory, collision, entity.mins, entity.maxs) &&
            (pawn > (std::numeric_limits<std::uintptr_t>::max)() - offsets::EmbeddedCollision ||
             !ReadBounds(memory, pawn + offsets::EmbeddedCollision, entity.mins, entity.maxs))) {
            ++report.failedReads;
            continue;
        }
        std::array<char, 64> name{};
        memory.Field(controller, offsets::PlayerName, name);
        std::memcpy(entity.name, name.data(), name.size());
        entity.name[sizeof(entity.name) - 1] = '\0';
        entity.id = pawnHandle & offsets::EntryMask;
        entity.valid = 1;
        entity.dormant = dormant ? 1u : 0u;
        entity.team = pawnTeam;
        entity.health = life == 0 ? static_cast<float>(health) : 0.f;
        entity.maxHealth = static_cast<float>(maximum);
        if (pawn == localPawn)
            frame.localEntityId = entity.id;
        // Weapon data is optional: a missing/dropped weapon must not hide its player.
        std::uintptr_t services{};
        std::uint32_t weaponHandle{};
        std::uint16_t definition{};
        if (memory.Field(pawn, offsets::WeaponServices, services) && services &&
            memory.Field(services, offsets::ActiveWeapon, weaponHandle) && weaponHandle &&
            weaponHandle != 0xFFFFFFFFu) {
            const auto weapon = EntityAt(memory, list, weaponHandle);
            memory.Field(weapon, offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, definition);
        }
        frame.weaponDefinitionIndices[frame.entityCount] = definition;
        if (entity.health > 0 && !entity.dormant) {
            if (ReadTargetBone(memory, scene, entity, selected, frame.bones[frame.entityCount]))
                ++report.bonePositions;
            else
                ++report.failedBoneReads;
        }
        frame.entities[frame.entityCount++] = entity;
        ++report.pawns;
    }
    return true;
}
} // namespace awareness::cs2
