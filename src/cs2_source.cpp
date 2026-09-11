#include "cs2_source.hpp"
#include <Psapi.h>
#include <cstdio>
#include "local_memory.hpp"

namespace awareness::cs2 {
namespace {
bool InModule(HMODULE module, std::uintptr_t offset, std::size_t bytes, std::uintptr_t &result) noexcept {
    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)) || offset > info.SizeOfImage ||
        bytes > info.SizeOfImage - offset)
        return false;
    result = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll) + offset;
    return true;
}
} // namespace
Source::~Source() {
    ClearHighlight();
    if (client_)
        FreeLibrary(client_);
    if (engine_)
        FreeLibrary(engine_);
}
bool Source::ReadPreview(const FrameSnapshot &frame, PreviewPose &out) const noexcept {
    out = {};
    if (!status_.ready || !status_.buildVerified || status_.gameBuild != offsets::ExpectedBuild)
        return false;
    const Memory memory{&sampleMemory_, LocalMemory::Read};
    std::uintptr_t list{};
    if (!memory.Read(globals_.entitySlot, list))
        return false;
    // Prefer the local player, then an alive player if spectating.
    for (int pass = 0; pass < 2; ++pass)
        for (std::uint32_t i = 0; i < std::min(frame.entityCount, MaxEntities); ++i) {
            const auto &entity = frame.entities[i];
            if ((entity.id == frame.localEntityId) != (pass == 0))
                continue;
            const auto pawn = EntityAt(memory, list, entity.id);
            std::uintptr_t scene{};
            if (memory.Field(pawn, offsets::SceneNode, scene) && ReadPreviewPose(memory, scene, entity, out)) {
                out.weapon = frame.weaponDefinitionIndices[i];
                return true;
            }
        }
    return false;
}
bool Source::ClearHighlight() noexcept {
    LocalMemory memory;
    return highlight_.Clear({{&memory, LocalMemory::Read}});
}
void Source::UpdateHighlight(const FrameSnapshot &frame, const Configuration &config,
                             const EffectsConfiguration &effects, bool fresh, EffectsState &output) noexcept {
    LocalMemory local;
    const GlowAccess access{{&local, LocalMemory::Read}};
    std::uintptr_t list{};
    const bool ready = fresh && status_.ready && status_.buildVerified && status_.gameBuild == offsets::ExpectedBuild &&
                       access.memory.Read(globals_.entitySlot, list) && list;
    const auto result = highlight_.Update(access, list, frame, config, effects, ready);
    output = {};
    output.meshCount = result.applied;
    const bool enabled = config.enabled && (effects.materialEnabled || effects.glowEnabled);
    output.status = !enabled         ? EffectsStatus::Disabled
                    : result.applied ? EffectsStatus::NativeReady
                                     : EffectsStatus::NoGeometry;
    output.result = result.applied ? S_OK : S_FALSE;
    if (output.status == EffectsStatus::NativeReady && effects.visibility != EffectVisibility::AlwaysVisible) {
        output.status = EffectsStatus::NativeSingleColor;
        output.result = S_FALSE;
    }
    if (!result.restored || (result.failed && !result.applied)) {
        output.status = EffectsStatus::Failed;
        output.result = E_ACCESSDENIED;
    }
}
bool Source::Update(FrameSnapshot &frame, TargetBone selected) noexcept {
    const auto now = GetTickCount64();
    frame = {};
    status_.ready = false;
    status_.reads = {};
    // Reuse validated regions across this sample's player, utility, and spectator
    // readers; begin with fresh permissions on the next worker tick.
    sampleMemory_.Reset();
    const Memory memory{&sampleMemory_, LocalMemory::Read};
    if (!discovered_) {
        if (now < nextAttempt_)
            return false;
        nextAttempt_ = now + 2000;
        status_.usingPatterns = false;
        if (!client_ && !GetModuleHandleExW(0, L"client.dll", &client_)) {
            std::snprintf(status_.message, sizeof(status_.message), "Waiting for client.dll");
            return false;
        }
        if (!engine_)
            GetModuleHandleExW(0, L"engine2.dll", &engine_);
        MODULEINFO engineInfo{};
        if (!engine_ || !GetModuleInformation(GetCurrentProcess(), engine_, &engineInfo, sizeof(engineInfo))) {
            std::snprintf(status_.message, sizeof(status_.message), "Waiting for engine2.dll");
            return false;
        }
        if (!buildChecked_) {
            const auto evidence = VerifyEngineBuild(memory, reinterpret_cast<std::uintptr_t>(engineInfo.lpBaseOfDll),
                                                    engineInfo.SizeOfImage);
            status_.buildVerified = evidence.check == BuildCheck::Verified;
            status_.rawBuildValue = evidence.bundledAddressValue;
            status_.gameBuild = status_.buildVerified ? evidence.build : 0;
            status_.buildRva =
                status_.buildVerified ? evidence.address - reinterpret_cast<std::uintptr_t>(engineInfo.lpBaseOfDll) : 0;
            buildChecked_ = true;
        }
        if (!status_.buildVerified) {
            std::snprintf(status_.message, sizeof(status_.message),
                          "Engine build cannot be verified. Refresh offsets with CS2 running.");
            return false;
        }
        if (status_.gameBuild != offsets::ExpectedBuild) {
            std::snprintf(status_.message, sizeof(status_.message),
                          "Game updated to %u. Refresh the %u offset snapshot and rebuild.", status_.gameBuild,
                          offsets::ExpectedBuild);
            return false;
        }
        Globals candidate;
        if (!InModule(client_, offsets::EntityList, sizeof(std::uintptr_t), candidate.entitySlot) ||
            !InModule(client_, offsets::ViewMatrix, sizeof(Matrix4x4), candidate.matrix) ||
            !InModule(client_, offsets::LocalController, sizeof(std::uintptr_t), candidate.localControllerSlot) ||
            !InModule(client_, offsets::LocalPawn, sizeof(std::uintptr_t), candidate.localPawnSlot)) {
            std::snprintf(status_.message, sizeof(status_.message), "Bundled offsets do not fit this client.dll");
            return false;
        }
        status_.usingPatterns = false;
        // Never combine schema fields from one build with globals from another.
        globals_ = candidate;
        // Camera availability is optional; a bad angle slot does not disable the read-only overlay.
        InModule(client_, offsets::ViewAngles, sizeof(NativeViewAngles), viewAngles_);
        discovered_ = true;
    }
    if (!ReadFrame(memory, globals_, frame, status_.reads, selected)) {
        std::snprintf(status_.message, sizeof(status_.message), "%s",
                      !status_.reads.matrixValid || !status_.reads.cameraValid ? "Waiting for an active camera / match"
                                                                               : "Waiting for the entity list");
        // Module RVAs remain stable during map changes. Read their current values
        // next Present without rescanning megabytes of engine code on the render thread.
        return false;
    }
    status_.ready = true;
    sampledAt_ = GetTickCount64();
    std::snprintf(status_.message, sizeof(status_.message), "%s",
                  status_.reads.pawns         ? "Live entity data connected"
                  : status_.reads.controllers ? "Controllers found; pawn layout or session is not ready"
                                              : "Waiting for players / observer session");
    return true;
}
} // namespace awareness::cs2
