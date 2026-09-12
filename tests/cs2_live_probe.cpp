// Optional read-only compatibility check. Never loads the overlay into the game.
#include "cs2_reader.hpp"
#include "build_verification.hpp"
#include "cs2_model_draw.hpp"
#include "trajectory_reader.hpp"
#include "assist_reader.hpp"
#include "world_visuals_reader.hpp"
#include <Psapi.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
using namespace awareness;
namespace {
struct Handle {
    HANDLE value{};
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
struct Module {
    std::uintptr_t base{};
    DWORD size{};
};
bool Read(void *process, std::uintptr_t address, void *output, std::size_t size) noexcept {
    SIZE_T copied{};
    return ReadProcessMemory(static_cast<HANDLE>(process), reinterpret_cast<void *>(address), output, size, &copied) &&
           copied == size;
}
std::uintptr_t Address(Module module, std::uintptr_t offset, std::size_t size) {
    return offset <= module.size && size <= module.size - offset ? module.base + offset : 0;
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) {
        std::fprintf(stderr, "Usage: awareness_cs2_live_probe <process-id> [preview-pose.bin]\n");
        return 1;
    }
    char *end{};
    const unsigned long parsed = std::strtoul(argv[1], &end, 10);
    if (!parsed || !end || *end) {
        std::puts("Invalid process identifier.");
        return 1;
    }
    const auto processId = static_cast<DWORD>(parsed);
    Handle process{OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId)};
    if (!process.value) {
        std::printf("Read-only process access failed: %lu\n", GetLastError());
        return 1;
    }
    HMODULE modules[1024]{};
    DWORD bytesNeeded{};
    if (!EnumProcessModulesEx(process.value, modules, sizeof(modules), &bytesNeeded, LIST_MODULES_64BIT) ||
        bytesNeeded > sizeof(modules)) {
        std::printf("Read-only module enumeration failed: %lu\n", GetLastError());
        return 1;
    }
    Module client{}, engine{};
    for (DWORD i = 0; i < bytesNeeded / sizeof(HMODULE); ++i) {
        wchar_t name[MAX_PATH]{};
        MODULEINFO info{};
        if (!GetModuleBaseNameW(process.value, modules[i], name, MAX_PATH) ||
            !GetModuleInformation(process.value, modules[i], &info, sizeof(info)))
            continue;
        const Module module{reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll), info.SizeOfImage};
        if (_wcsicmp(name, L"client.dll") == 0)
            client = module;
        if (_wcsicmp(name, L"engine2.dll") == 0)
            engine = module;
    }
    const cs2::Memory memory{process.value, Read};
    const auto evidence = cs2::VerifyEngineBuild(memory, engine.base, engine.size);
    if (evidence.check != cs2::BuildCheck::Verified) {
        std::puts("Engine build signature could not be verified.");
        return 1;
    }
    const auto gameBuild = evidence.build;
    std::printf("Game build: %u; bundled build: %u\n", gameBuild, cs2::offsets::ExpectedBuild);
    std::printf("Verified build RVA: 0x%llX; value at bundled RVA: %u\n",
                static_cast<unsigned long long>(evidence.address - engine.base), evidence.bundledAddressValue);
    if (gameBuild != cs2::offsets::ExpectedBuild)
        return 1;
    const cs2::Globals globals{Address(client, cs2::offsets::EntityList, sizeof(std::uintptr_t)),
                               Address(client, cs2::offsets::ViewMatrix, sizeof(Matrix4x4)),
                               Address(client, cs2::offsets::LocalController, sizeof(std::uintptr_t)),
                               Address(client, cs2::offsets::LocalPawn, sizeof(std::uintptr_t))};
    FrameSnapshot frame;
    cs2::ReadReport report;
    const bool ready = cs2::ReadFrame(memory, globals, frame, report);
    std::printf("Matrix valid: %d; camera valid: %d; entity list present: %d\n", report.matrixValid, report.cameraValid,
                report.listValid);
    std::printf("Controllers: %u; pawn records: %u; skipped reads: %u\n", report.controllers, report.pawns,
                report.failedReads);
    std::uintptr_t list{};
    memory.Read(globals.entitySlot, list);
    cs2::ProjectileFrame projectiles;
    const bool flightReady = cs2::ReadProjectiles(memory, list, projectiles);
    std::uintptr_t view{}, manager{}, physics{}, localPawn{};
    float fov{};
    Vector3 angles{};
    flight::Throw held;
    const bool viewReady = memory.Field(client.base, cs2::trajectory_offsets::ViewRenderSlot, view) &&
                           memory.Field(view, cs2::trajectory_offsets::ViewFov, fov) && std::isfinite(fov) && fov > 0 &&
                           fov < 179;
    const bool physicsReady = memory.Field(client.base, cs2::trajectory_offsets::TraceManagerSlot, manager) &&
                              memory.Read(manager, physics) && physics;
    const bool heldReady = memory.Read(globals.localPawnSlot, localPawn) &&
                           memory.Field(client.base, cs2::offsets::ViewAngles, angles) &&
                           cs2::ReadThrow(memory, list, localPawn, angles, held);
    std::printf("Projectile enumeration: %d; live utility: %u; view FOV: %.2f (valid %d); physics world present: %d; "
                "held utility: %d\n",
                flightReady, projectiles.count, fov, viewReady, physicsReady, heldReady);
    const assist::Addresses assistAddresses{globals.entitySlot, globals.localPawnSlot, globals.localControllerSlot,
                                            Address(client, cs2::offsets::ViewAngles, sizeof(cs2::NativeViewAngles))};
    assist::Sample inputSample;
    const bool inputReady = cs2::ReadAssistSample(memory, assistAddresses, inputSample);
    std::printf("Assist: sample=%d owner=%u weapon=%u handle=%u target=%u enemy=%d grounded=%d frozen=%d flags=0x%X "
                "moveType=%u\n",
                inputReady, inputSample.owner, inputSample.weapon, inputSample.weaponHandle, inputSample.target,
                inputSample.enemy, inputSample.grounded, inputSample.frozen, inputSample.flags, inputSample.moveType);
    std::printf("Assist readiness: weaponKnown=%d fieldsKnown=%d ready=%d reloading=%d empty=%d waitAttack=%d "
                "cooldown=%d tick=%u shots=%u\n",
                inputSample.weaponKnown, inputSample.readinessKnown, inputSample.weaponReady, inputSample.reloading,
                inputSample.empty, inputSample.waitingAttack, inputSample.cooldown, inputSample.tick,
                inputSample.shots);
    std::printf(
        "Assist movement: movementKnown=%d velocityKnown=%d anglesKnown=%d speed=%.2f water=%.3f yaw=%.2f pitch=%.2f\n",
        inputSample.movementKnown, inputSample.velocityKnown, inputSample.anglesKnown,
        std::hypot(inputSample.velocity.x, inputSample.velocity.y), inputSample.water, inputSample.yaw,
        inputSample.pitch);
    std::int32_t rawCrosshair{};
    const bool crosshairRead = memory.Field(localPawn, cs2::offsets::CrosshairIndex, rawCrosshair);
    std::printf("Crosshair read: valid=%d index=%d\n", crosshairRead, rawCrosshair);
    if (!inputReady) {
        std::uintptr_t controller{}, scene{};
        std::uint32_t pawnHandle{}, controllerHandle{}, controllerPawn{}, flags{};
        std::uint8_t team{}, move{}, life{}, dormant{};
        const bool pawnValid = cs2::AssistPawn(memory, localPawn, pawnHandle, team);
        const bool controllerValid = memory.Read(globals.localControllerSlot, controller) &&
                                     cs2::FullHandle(memory, controller, controllerHandle);
        const bool controllerPawnRead = memory.Field(controller, cs2::offsets::ControllerPawn, controllerPawn);
        const bool flagRead = memory.Field(localPawn, cs2::offsets::MovementFlags, flags);
        memory.Field(localPawn, cs2::offsets::LifeState, life);
        memory.Field(localPawn, cs2::offsets::ActualMoveType, move);
        memory.Field(localPawn, cs2::offsets::SceneNode, scene);
        memory.Field(scene, cs2::offsets::Dormant, dormant);
        std::printf("Assist baseline: pawnValid=%d pawnHandle=%u controllerValid=%d controllerHandle=%u "
                    "controllerPawnRead=%d controllerPawn=%u flagsRead=%d flags=0x%X life=%u dormant=%u move=%u\n",
                    pawnValid, pawnHandle, controllerValid, controllerHandle, controllerPawnRead, controllerPawn,
                    flagRead, flags, life, dormant, move);
    }
    {
        std::uintptr_t identity{}, namePointer{}, scene{};
        std::array<char, 64> designer{};
        std::int32_t health{};
        std::uint8_t team{}, life{}, dormant{};
        const bool identityRead = memory.Field(localPawn, cs2::offsets::Identity, identity);
        const bool nameRead =
            memory.Field(identity, cs2::offsets::DesignerName, namePointer) && memory.Read(namePointer, designer);
        designer.back() = 0;
        memory.Field(localPawn, cs2::offsets::Health, health);
        memory.Field(localPawn, cs2::offsets::Team, team);
        memory.Field(localPawn, cs2::offsets::LifeState, life);
        memory.Field(localPawn, cs2::offsets::SceneNode, scene);
        memory.Field(scene, cs2::offsets::Dormant, dormant);
        std::printf("Local pawn fields: identity=%d nameRead=%d designer=%s health=%d team=%u life=%u dormant=%u\n",
                    identityRead, nameRead, designer.data(), health, team, life, dormant);
    }
    unsigned footValid{}, movingFeet{};
    for (std::uint32_t i = 0; i < frame.entityCount; ++i) {
        const auto pawn = cs2::EntityAt(memory, list, frame.entities[i].id);
        char liveName[64]{};
        const bool liveNameRead = cs2::EntityName(memory, pawn, liveName);
        std::uintptr_t movement{};
        int stepSide = -1;
        const bool stepPhase = memory.Field(pawn, cs2::offsets::MovementServices, movement) && movement &&
                               memory.Field(movement, cs2::offsets::MovementStepSide, stepSide);
        std::printf("Pawn id=%u nameRead=%d designer=%s stepPhaseRead=%d side=%d\n", frame.entities[i].id, liveNameRead,
                    liveName, stepPhase, stepSide);
        worldvisuals::Footstep step;
        if (!cs2::ReadFootstep(memory, list, pawn, 0, step))
            continue;
        ++footValid;
        Vector3 velocity{};
        std::uint32_t flags{};
        if (memory.Field(pawn, cs2::offsets::AbsVelocity, velocity) && Finite(velocity) &&
            memory.Field(pawn, cs2::offsets::MovementFlags, flags) && (flags & 1) &&
            std::hypot(velocity.x, velocity.y) > 50)
            ++movingFeet;
    }
    std::printf("Footstep position compatibility: valid pawns=%u; moving grounded pawns=%u (event receipt is not "
                "verified by this read-only probe)\n",
                footValid, movingFeet);
    cs2::FootstepReader sampledSteps;
    for (unsigned poll = 0; poll < 50; ++poll) {
        FrameSnapshot sampledFrame;
        cs2::ReadReport sampledReport;
        if (cs2::ReadFrame(memory, globals, sampledFrame, sampledReport))
            sampledSteps.Update(memory, list, sampledFrame, GetTickCount64() * .001);
        Sleep(20);
    }
    std::printf("One-second read-only movement sample: footstep phase transitions accepted=%llu\n",
                static_cast<unsigned long long>(sampledSteps.Count()));
    std::uint32_t previews{};
    PreviewPose selectedPose;
    for (std::uint32_t i = 0; i < frame.entityCount; ++i) {
        const auto &entity = frame.entities[i];
        const auto pawn = cs2::EntityAt(memory, list, entity.id);
        std::uintptr_t scene{};
        PreviewPose pose;
        if (memory.Field(pawn, cs2::offsets::SceneNode, scene) && cs2::ReadPreviewPose(memory, scene, entity, pose)) {
            ++previews;
            pose.weapon = frame.weaponDefinitionIndices[i];
            if (!selectedPose.valid || entity.id == frame.localEntityId)
                selectedPose = pose;
        }
    }
    std::printf("Tracking nodes: %u; valid live previews: %u\n", report.bonePositions, previews);
    Configuration modelConfig;
    modelConfig.worldUnitsPerMeter = 39.3700787f;
    modelConfig.maxDistanceMeters = 200.f;
    modelConfig.fadeStartMeters = 100.f;
    const auto models = cs2::model::CollectTargets(memory, list, frame, modelConfig);
    unsigned owned{};
    for (std::size_t i = 0; i < models.count; ++i)
        owned += cs2::model::StillOwned(memory, models.entries[i]) ? 1u : 0u;
    std::printf("Eligible player scene objects: %zu; ownership revalidated: %u\n", models.count, owned);
    if (argc == 3 && selectedPose.valid) {
        std::ofstream file(argv[2], std::ios::binary);
        file.write(reinterpret_cast<const char *>(&selectedPose), sizeof(selectedPose));
        if (!file) {
            std::puts("Could not save the preview sample.");
            return 1;
        }
        std::puts("Saved one validated pose for offline renderer verification.");
    }
    std::puts("Read-only inspection completed; no DLL loading, hooks, input, or memory writes.");
    if (!ready || !report.pawns) {
        std::puts("Full pawn compatibility is unconfirmed in this session state.");
        return 2;
    }
    std::puts("Bundled globals and entity reader produced live pawn records.");
    return 0;
}
