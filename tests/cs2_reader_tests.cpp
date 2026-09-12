#include "cs2_reader.hpp"
#include "skeleton_reader.hpp"
#include "latency_reader.hpp"
#include <vector>
#include <cstdio>
#include <string>
using namespace awareness;
using namespace awareness::cs2;
namespace {
int checks{}, failures{};
void Check(bool result, const char *description) {
    ++checks;
    if (!result) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
}
struct Fixture {
    static constexpr std::uintptr_t base = 0x10000000, list = base + 0x1000;
    std::vector<unsigned char> bytes = std::vector<unsigned char>(0x100000);
    Globals globals{base + 0x100, base + 0x200, base + 0x108, base + 0x110};
    Memory memory{this, Read};
    static bool Read(void *context, std::uintptr_t address, void *target, std::size_t size) noexcept {
        const auto &bytes = static_cast<Fixture *>(context)->bytes;
        if (address < base || address - base > bytes.size() || size > bytes.size() - (address - base))
            return false;
        std::memcpy(target, bytes.data() + address - base, size);
        return true;
    }
    template <class T> void Put(std::uintptr_t address, const T &value) {
        std::memcpy(bytes.data() + address - base, &value, sizeof(value));
    }
    std::uintptr_t Entry(std::uint32_t index) const {
        return base + (index < 512 ? 0x10000 : 0x20000) + offsets::EntityStride * (index % 512);
    }
    void Entity(std::uint32_t index, std::uintptr_t object) {
        const auto entry = Entry(index);
        Put(entry, object);
        Put(object + offsets::Identity, entry);
        Put(entry + 0x10, index + 0x80000u);
    }
    static constexpr std::uintptr_t Pawn(int i) { return base + 0x40000 + 0x4000 * i; }
    static constexpr std::uintptr_t Scene(int i) { return base + 0x50000 + 0x200 * i; }
    static constexpr std::uintptr_t Collision(int i) { return base + 0x52000 + 0x100 * i; }
    static constexpr std::uintptr_t Bones(int i) { return base + 0x80000 + 0x1000 * i; }
    Fixture() {
        Matrix4x4 m{};
        m.m[0][0] = 1;
        m.m[0][3] = -10;
        m.m[1][1] = 1;
        m.m[1][3] = -20;
        m.m[2][2] = 10.f / 9;
        m.m[2][3] = 40.f / 9;
        m.m[3][2] = 1;
        m.m[3][3] = 5;
        Put(globals.matrix, m);
        Put(globals.entitySlot, list);
        Put(list + 0x10, base + 0x10000);
        Put(list + 0x18, base + 0x20000);
        const std::uint32_t slots[]{1, 2, 64};
        for (int i = 0; i < 3; ++i) {
            const auto controller = base + 0x30000 + 0x2000 * i;
            Entity(slots[i], controller);
            Entity(600 + i, Pawn(i));
            Put(controller + offsets::ControllerPawn, 0x80000u + 600 + i);
            Put(controller + offsets::Team, std::uint8_t{2});
            std::array<char, 64> name{};
            std::snprintf(name.data(), name.size(), "Player %d", i);
            Put(controller + offsets::PlayerName, name);
            Put(Pawn(i) + offsets::SceneNode, Scene(i));
            Put(Pawn(i) + offsets::Collision, Collision(i));
            Put(Pawn(i) + offsets::Health, std::int32_t{100 - 20 * i});
            Put(Pawn(i) + offsets::MaxHealth, std::int32_t{100});
            Put(Pawn(i) + offsets::Team, static_cast<std::uint8_t>(i == 0 ? 2 : 3));
            Put(Scene(i) + offsets::Origin, Vector3{10.f + i, 20.f, 30.f});
            Put(Collision(i) + offsets::Mins, Vector3{-16, -16, 0});
            Put(Collision(i) + offsets::Maxs, Vector3{16, 16, 72});
            Put(Scene(i) + offsets::ModelState + offsets::BoneArray, Bones(i));
            Put(Scene(i) + offsets::ModelState + offsets::BoneCount, std::uint16_t{8});
            for (auto bone : TargetBones) {
                const int index = static_cast<int>(bone);
                BoneTransform transform{{10.f + i, 20.f, 30.f + index * 8.f}, 1, {0, 0, 0, 1}};
                Put(Bones(i) + index * offsets::BoneStride, transform);
            }
        }
        Put(globals.localControllerSlot, base + 0x30000);
        Put(globals.localPawnSlot, Pawn(0));
    }
};
} // namespace
int main() {
    Fixture data;
    {
        Fixture ping;
        const auto controller = Fixture::base + 0x30000;
        ping.Put(controller + offsets::ControllerPing, std::uint32_t{26});
        Check(ReadControllerPing(ping.memory, controller) == 26, "badge reads controller ping in milliseconds");
        ping.Put(controller + offsets::ControllerPing, std::uint32_t{0});
        Check(ReadControllerPing(ping.memory, controller) == 0, "local zero ping remains valid");
        ping.Put(controller + offsets::ControllerPing, std::uint32_t{5001});
        Check(ReadControllerPing(ping.memory, controller) == -1, "invalid ping displays unavailable");
        ping.Put(controller + offsets::Identity, std::uintptr_t{});
        Check(ReadControllerPing(ping.memory, controller) == -1, "missing controller identity clears ping");
        Check(ReadControllerPing(ping.memory, 0) == -1, "missing controller does not fabricate latency");
    }
    {
        Fixture preview;
        FrameSnapshot snapshot;
        ReadReport reads;
        ReadFrame(preview.memory, preview.globals, snapshot, reads);
        PreviewPose pose;
        Check(!ReadPreviewPose(preview.memory, Fixture::Scene(0), snapshot.entities[0], pose),
              "short preview array rejected");
        preview.Put(Fixture::Scene(0) + offsets::ModelState + offsets::BoneCount, std::uint16_t{94});
        for (int i = 0; i < 25; ++i)
            preview.Put(Fixture::Bones(0) + i * offsets::BoneStride,
                        BoneTransform{{10.f, 20.f, 30.f + float(i) * 5}, 1, {0, 0, 0, 1}});
        Check(ReadPreviewPose(preview.memory, Fixture::Scene(0), snapshot.entities[0], pose) && pose.valid,
              "complete validated pose copied for the live preview");
        preview.Put(Fixture::Bones(0) + 9 * offsets::BoneStride, BoneTransform{{10, 20, 5000}, 1, {0, 0, 0, 1}});
        Check(!ReadPreviewPose(preview.memory, Fixture::Scene(0), snapshot.entities[0], pose) && !pose.valid,
              "corrupt live pose clears previous pose for fallback");
    }
    FrameSnapshot frame;
    ReadReport report;
    Check(ReadFrame(data.memory, data.globals, frame, report), "read fixture frame");
    Check(report.matrixValid && report.cameraValid && report.listValid, "globals validated");
    Check(report.controllers == 3 && report.pawns == 3 && report.failedReads == 0, "controller and pawn counts");
    Check(frame.entityCount == 3 && frame.entities[2].id == 602, "slot 64 and second entity chunk included");
    Check(frame.localEntityId == 600 && frame.localTeam == 2, "local identity and team");
    Check(std::abs(frame.cameraOrigin.x - 10) < .001f && std::abs(frame.cameraOrigin.y - 20) < .001f &&
              std::abs(frame.cameraOrigin.z + 5) < .001f,
          "camera derived from perspective matrix");
    Check(std::strcmp(frame.entities[1].name, "Player 1") == 0, "controller name copied");
    Check(frame.entities[1].health == 80 && frame.entities[1].team == 3, "pawn health and team fields");
    Check(frame.entities[1].mins.x == -16 && frame.entities[1].maxs.z == 72, "collision bounds read");
    Check(!EntityAt(data.memory, Fixture::list, 0xFFFFFFFFu), "invalid entity handle");
    Check(!EntityAt(data.memory, Fixture::list, 0), "null entity index");
    {
        Fixture skeletal;
        for (auto selected : TargetBones) {
            ReadFrame(skeletal.memory, skeletal.globals, frame, report, selected);
            const int index = static_cast<int>(selected);
            const auto point = ResolveTargetBone(frame.bones[1], selected);
            Check(point && point->z == 30.f + index * 8.f && frame.bones[1].validMask == (1u << index),
                  "selected engine array index resolves in world space without stale nodes");
            Check(frame.entities[1].origin.z == 30 && report.bonePositions == 3,
                  "bone selection leaves scene origin and HUD bounds unchanged");
        }
        skeletal.Put(Fixture::Scene(1) + offsets::ModelState + offsets::BoneCount, std::uint16_t{7});
        ReadFrame(skeletal.memory, skeletal.globals, frame, report, TargetBone::Head);
        Check(!frame.bones[1].validMask && frame.entityCount == 3 && report.failedBoneReads == 1,
              "short array skips tracking bone without removing HUD entity");
        skeletal.Put(Fixture::Scene(1) + offsets::ModelState + offsets::BoneCount, std::uint16_t{8});
        BoneTransform bad{{NAN, 0, 0}, 1, {0, 0, 0, 1}};
        skeletal.Put(Fixture::Bones(1) + 7 * offsets::BoneStride, bad);
        ReadFrame(skeletal.memory, skeletal.globals, frame, report);
        Check(!frame.bones[1].validMask, "nonfinite bone position clears previous target");
        bad.position = {50000, 0, 0};
        skeletal.Put(Fixture::Bones(1) + 7 * offsets::BoneStride, bad);
        ReadFrame(skeletal.memory, skeletal.globals, frame, report);
        Check(!frame.bones[1].validMask, "unrelated or corrupted bone pose is rejected");
        EntityBones output;
        output.validMask = 255;
        Check(!ReadTargetBone(skeletal.memory, Fixture::Scene(0), frame.entities[0], static_cast<TargetBone>(-1),
                              output) &&
                  !output.validMask,
              "invalid index clears output without an out-of-range read");
        skeletal.Put(Fixture::Scene(0) + offsets::ModelState + offsets::BoneArray, std::uintptr_t{});
        Check(!ReadTargetBone(skeletal.memory, Fixture::Scene(0), frame.entities[0], TargetBone::Head, output),
              "null bone array fails safely");
    }
    const auto services = Fixture::base + 0x60000, weapon = Fixture::base + 0x64000;
    data.Put(Fixture::Pawn(1) + offsets::WeaponServices, services);
    data.Put(services + offsets::ActiveWeapon, std::uint32_t{0x80000u + 700});
    data.Entity(700, weapon);
    data.Put(weapon + offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, std::uint16_t{7});
    ReadFrame(data.memory, data.globals, frame, report);
    Check(frame.weaponDefinitionIndices[1] == 7, "active AK-47 definition read through weapon services");
    data.Put(weapon + offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, std::uint16_t{9});
    ReadFrame(data.memory, data.globals, frame, report);
    Check(frame.weaponDefinitionIndices[1] == 9, "weapon switch is visible on the next read without a delay");
    data.Put(services + offsets::ActiveWeapon, std::uint32_t{0xFFFFFFFFu});
    ReadFrame(data.memory, data.globals, frame, report);
    Check(frame.weaponDefinitionIndices[1] == 0 && frame.entityCount == 3,
          "dropped weapon clears icon without hiding player");
    data.Put(services + offsets::ActiveWeapon, std::uint32_t{0x80000u + 700});
    data.Put(weapon + offsets::Identity, std::uintptr_t{});
    ReadFrame(data.memory, data.globals, frame, report);
    Check(frame.weaponDefinitionIndices[1] == 0 && frame.entityCount == 3,
          "invalid weapon identity cannot retain a stale icon");
    {
        Fixture alternate;
        alternate.Put(Fixture::base + 0x30000 + offsets::Team, std::uint8_t{1});
        ReadFrame(alternate.memory, alternate.globals, frame, report);
        Check(frame.localTeam == 2, "spectator controller falls back to local pawn team");
        alternate.Put(Fixture::Pawn(0) + offsets::Team, std::uint8_t{1});
        ReadFrame(alternate.memory, alternate.globals, frame, report);
        Check(frame.localTeam == 0, "spectator team remains unknown for team filtering");
        alternate.Put(Fixture::base + 0x32000 + offsets::ControllerPawn, std::uint32_t{0xFFFFFFFFu});
        alternate.Put(Fixture::base + 0x34000 + offsets::ControllerPawn, std::uint32_t{0x80000u + 600});
        ReadFrame(alternate.memory, alternate.globals, frame, report);
        Check(report.inactivePawns == 1 && report.duplicatePawns == 1 && report.failedReads == 0 &&
                  frame.entityCount == 1,
              "unassigned and duplicate pawns are classified without false read errors");
    }
    data.Put(Fixture::Scene(1) + offsets::Dormant, std::uint8_t{1});
    data.Put(Fixture::Pawn(2) + offsets::LifeState, std::uint8_t{1});
    ReadFrame(data.memory, data.globals, frame, report);
    Check(frame.entities[1].dormant == 1 && frame.entities[2].health == 0,
          "dormant and dead flags for render filtering");
    data.Put(Fixture::Pawn(0) + offsets::Identity, std::uintptr_t{});
    ReadFrame(data.memory, data.globals, frame, report);
    Check(frame.entityCount == 2 && report.failedReads == 1, "identity backlink mismatch rejected");
    data.Put(Fixture::Pawn(0) + offsets::Identity, data.Entry(600));
    data.Put(data.Entry(600) + 0x10, std::uint32_t{601});
    Check(!EntityAt(data.memory, Fixture::list, 600), "stored handle index mismatch rejected");
    data.Put(data.Entry(600) + 0x10, std::uint32_t{600 + 0x80000});
    data.Put(Fixture::Pawn(0) + offsets::Health, std::int32_t{-10});
    ReadFrame(data.memory, data.globals, frame, report);
    Check(frame.entityCount == 2, "negative health rejected");
    data.Put(Fixture::Pawn(0) + offsets::Health, std::int32_t{100});
    data.Put(Fixture::Pawn(0) + offsets::Collision, std::uintptr_t{});
    data.Put(Fixture::Pawn(0) + offsets::EmbeddedCollision + offsets::Mins, Vector3{-10, -10, 0});
    data.Put(Fixture::Pawn(0) + offsets::EmbeddedCollision + offsets::Maxs, Vector3{10, 10, 50});
    ReadFrame(data.memory, data.globals, frame, report);
    Check(frame.entityCount == 3 && frame.entities[0].maxs.z == 50, "embedded collision fallback");
    data.Put(Fixture::Pawn(0) + offsets::EmbeddedCollision + offsets::Maxs, Vector3{-10, -10, 0});
    ReadFrame(data.memory, data.globals, frame, report);
    Check(frame.entityCount == 2, "degenerate bounds rejected");
    data.Put(data.globals.entitySlot, std::uintptr_t{});
    Check(!ReadFrame(data.memory, data.globals, frame, report) && frame.entityCount == 0 && !report.listValid,
          "missing list clears old frame");
    Matrix4x4 matrix{};
    data.Put(data.globals.matrix, matrix);
    Check(!ReadFrame(data.memory, data.globals, frame, report) && !report.cameraValid, "singular camera rejected");
    matrix.m[0][0] = std::numeric_limits<float>::quiet_NaN();
    data.Put(data.globals.matrix, matrix);
    Check(!ReadFrame(data.memory, data.globals, frame, report) && !report.matrixValid, "nonfinite matrix rejected");
    int value = 42;
    Check(!data.memory.Read(std::uintptr_t{1}, value) && value == 0, "unreadable memory rejected and output cleared");
    value = 42;
    Check(!data.memory.Field((std::numeric_limits<std::uintptr_t>::max)(), 1, value) && value == 0,
          "field address overflow rejected");
    const Memory partial{nullptr, [](void *, std::uintptr_t, void *target, std::size_t length) noexcept {
                             std::memset(target, 0x7f, length / 2);
                             return false;
                         }};
    value = 42;
    Check(!partial.Read(std::uintptr_t{1}, value) && value == 0, "partial failed read never publishes partial bytes");
    bool called = false;
    const Memory overflow{&called, [](void *context, std::uintptr_t, void *, std::size_t) noexcept {
                              *static_cast<bool *>(context) = true;
                              return true;
                          }};
    value = 42;
    Check(!overflow.Read((std::numeric_limits<std::uintptr_t>::max)() - 1, value) && value == 0 && !called,
          "whole read range checked before calling backend");
    Memory missing;
    value = 42;
    Check(!missing.Read(std::uintptr_t{1}, value) && value == 0, "missing read callback clears output");
    std::array<char, 64> partialName{};
    Check(!partial.Read(std::uintptr_t{1}, partialName) && partialName == std::array<char, 64>{},
          "failed array reads clear all output bytes");
    {
        Fixture bones;
        FrameSnapshot snapshot;
        ReadReport boneReport;
        ReadFrame(bones.memory, bones.globals, snapshot, boneReport);
        const auto &entity = snapshot.entities[1];
        bones.Put(Fixture::Scene(1) + offsets::ModelState + offsets::BoneCount, std::uint16_t{25});
        for (unsigned i = 0; i < skeleton::JointCount; ++i)
            bones.Put(Fixture::Bones(1) + i * offsets::BoneStride,
                      BoneTransform{entity.origin + Vector3{0, 0, float(i + 1)}, 1, {0, 0, 0, 1}});
        skeleton::Pose pose;
        Check(ReadSkeletonPose(bones.memory, Fixture::Scene(1), entity, pose) && pose.entity == entity.id &&
                  skeleton::Segment(pose, 17, 18) && skeleton::Segment(pose, 10, 11),
              "skeleton publishes validated arm and leg joints from a bounded array read");
        bones.Put(Fixture::Bones(1) + 11 * offsets::BoneStride, BoneTransform{{}, 0, {0, 0, 0, 0}});
        Check(ReadSkeletonPose(bones.memory, Fixture::Scene(1), entity, pose) && !skeleton::Segment(pose, 10, 11) &&
                  skeleton::Segment(pose, 17, 18),
              "unavailable wrist skips only affected skeleton edges");
        bones.Put(Fixture::Bones(1) + 18 * offsets::BoneStride,
                  BoneTransform{entity.origin + Vector3{0, 0, 2000}, 1, {0, 0, 0, 1}});
        Check(ReadSkeletonPose(bones.memory, Fixture::Scene(1), entity, pose) && !skeleton::Segment(pose, 17, 18),
              "outlying joint cannot draw a screen-spanning skeleton line");
        auto dead = entity;
        dead.health = 0;
        Check(!ReadSkeletonPose(bones.memory, Fixture::Scene(1), dead, pose) && !pose.validMask,
              "dead entities clear the skeletal publication");
        bones.Put(Fixture::Scene(1) + offsets::ModelState + offsets::BoneCount, std::uint16_t{8});
        Check(!ReadSkeletonPose(bones.memory, Fixture::Scene(1), entity, pose),
              "short skeleton arrays cannot be overread");
        bones.Put(Fixture::Scene(1) + offsets::ModelState + offsets::BoneCount, std::uint16_t{25});
        struct Moving {
            Fixture *fixture;
            unsigned pointerReads{};
        } moving{&bones};
        Memory changed{&moving, [](void *ctx, std::uintptr_t address, void *out, std::size_t size) noexcept {
                           auto &m = *static_cast<Moving *>(ctx);
                           if (address == Fixture::Scene(1) + offsets::ModelState + offsets::BoneArray &&
                               ++m.pointerReads == 2) {
                               const std::uintptr_t replacement = Fixture::Bones(2);
                               std::memcpy(out, &replacement, size);
                               return true;
                           }
                           return Fixture::Read(m.fixture, address, out, size);
                       }};
        Check(!ReadSkeletonPose(changed, Fixture::Scene(1), entity, pose) && !pose.validMask,
              "bone array replacement during sampling discards the entire pose");
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
