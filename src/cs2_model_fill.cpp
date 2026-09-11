#include "cs2_model_fill.hpp"
#include "cs2_model_draw.hpp"
#include "build_verification.hpp"
#include "runtime_support.hpp"
#include <MinHook.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <memory>
#include <string>

namespace awareness::cs2 {
namespace {
using Draw = void *(*)(void *, void *, const model::Packet *, int, void *, void *, void *);
struct Material {
    // Build 14181 GetContext tests bit 0 before decoding a pooled allocation
    // index. A standalone root must set it; a zeroed root reads before itself.
    struct alignas(16) KeyValues {
        std::uint64_t flags{1}, value{};
    } kv;
    void *binding{};
    std::uintptr_t object{};
    std::string text;
    unsigned parseStage{}, exception{};
    std::uintptr_t fault{}, access{};
};
struct State {
    std::atomic<std::shared_ptr<const model::Selection>> selection;
    std::array<std::shared_ptr<model::Selection>, 3> pool;
    std::atomic<bool> enabled{};
    std::atomic<ULONGLONG> deadline{};
    std::atomic<unsigned> inFlight{}, draws{};
    Draw original{};
    void *address{};
    HMODULE scene{}, material{};
    Material materials[2];
    HRESULT startup{E_PENDING};
    bool attempted{};
    std::atomic<bool> installed{};
} state;

bool ModuleMatches(HMODULE module, std::uintptr_t timestamp, std::uintptr_t imageSize) noexcept {
    LocalMemory local;
    const Memory memory{&local, LocalMemory::Read};
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    return memory.Read(base, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
           dos.e_lfanew < 0x100000 && memory.Field(base, dos.e_lfanew, nt) && nt.Signature == IMAGE_NT_SIGNATURE &&
           nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 && nt.FileHeader.TimeDateStamp == timestamp &&
           nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC && nt.OptionalHeader.SizeOfImage == imageSize;
}
bool CodeMatches(std::uintptr_t base, std::uintptr_t rva, std::span<const unsigned char> expected) noexcept {
    LocalMemory local;
    std::array<unsigned char, 64> found{};
    return expected.size() <= found.size() && LocalMemory::Read(&local, base + rva, found.data(), expected.size()) &&
           std::memcmp(found.data(), expected.data(), expected.size()) == 0;
}
bool ValidateLayout() noexcept {
    const auto engine = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"engine2.dll"));
    LocalMemory local;
    const Memory memory{&local, LocalMemory::Read};
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!memory.Read(engine, dos) || dos.e_lfanew < 0 || !memory.Field(engine, dos.e_lfanew, nt))
        return false;
    const auto build = VerifyEngineBuild(memory, engine, nt.OptionalHeader.SizeOfImage);
    if (build.check != BuildCheck::Verified || build.build != offsets::ExpectedBuild ||
        build.build != offsets::RenderLayoutBuild)
        return false;
    if (!ModuleMatches(state.scene, offsets::SceneTimestamp, offsets::SceneImageSize) ||
        !ModuleMatches(state.material, offsets::MaterialTimestamp, offsets::MaterialImageSize))
        return false;
    const unsigned char drawBytes[]{0x48, 0x8b, 0xc4, 0x53, 0x57, 0x41, 0x54, 0x48, 0x81,
                                    0xec, 0xd0, 0,    0,    0,    0x49, 0x63, 0xf9};
    const unsigned char materialBytes[]{0x48, 0x89, 0x5c, 0x24, 8,    0x48, 0x89, 0x6c, 0x24, 0x10,
                                        0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20,
                                        0x41, 0x56, 0x48, 0x81, 0xec, 0x10, 1,    0,    0};
    const auto scene = reinterpret_cast<std::uintptr_t>(state.scene);
    std::uintptr_t slot{};
    return CodeMatches(scene, offsets::RenderCallback, drawBytes) &&
           CodeMatches(reinterpret_cast<std::uintptr_t>(state.material), offsets::CreateMaterial, materialBytes) &&
           memory.Field(scene, offsets::RenderVtableSlot, slot) && slot == scene + offsets::RenderCallback;
}
struct KvId {
    const char *name;
    std::uint64_t first, second;
};
using LoadKv = bool (*)(void *, void *, const char *, const KvId *, const char *, unsigned);
using CreateMaterial = void *(*)(void *, void **, const char *, void *, int, int);
int ParseFault(EXCEPTION_POINTERS *info, Material *material) noexcept {
    material->fault = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    material->access = info->ExceptionRecord->NumberParameters > 1 ? info->ExceptionRecord->ExceptionInformation[1] : 0;
    return EXCEPTION_EXECUTE_HANDLER;
}
// Keep SEH in a leaf without C++ objects needing stack unwinding.
bool ParseMaterial(LoadKv load, CreateMaterial create, Material *material, const char *name) noexcept {
    const KvId id{"generic", 0x469806e97412167cull, 0xe73790b53ee6f2afull};
    __try {
        material->parseStage = 1;
        if (!load(&material->kv, nullptr, material->text.c_str(), &id, nullptr, 0))
            return false;
        material->parseStage = 2;
        create(nullptr, &material->binding, name, &material->kv, 0, 1);
        material->parseStage = 3;
        return material->binding != nullptr;
    } __except (ParseFault(GetExceptionInformation(), material)) {
        material->exception = GetExceptionCode();
        return false;
    }
}
bool CreateMaterials() {
    const auto tier0 = GetModuleHandleW(L"tier0.dll");
    const auto load = reinterpret_cast<LoadKv>(
        GetProcAddress(tier0, "?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEBDAEBUKV3ID_t@@2I@Z"));
    const auto create =
        reinterpret_cast<CreateMaterial>(reinterpret_cast<std::uintptr_t>(state.material) + offsets::CreateMaterial);
    if (!load)
        return false;
    LocalMemory local;
    const Memory memory{&local, LocalMemory::Read};
    for (unsigned i = 0; i < 2; ++i) {
        auto &mat = state.materials[i];
        if (mat.object)
            continue;
        // Blend mode 1 is already translucent. F_TRANSLUCENT is only valid with
        // mode 2 (alpha test); combining them requests an invalid shader feature set.
        mat.text = "<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} "
                   "format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->\n{\n"
                   "shader = \"csgo_unlitgeneric.vfx\"\nF_DISABLE_Z_PREPASS = 1\nF_RENDER_BACKFACES = 0\n"
                   "F_DISABLE_Z_BUFFERING = " +
                   std::to_string(i) +
                   "\nF_BLEND_MODE = 1\ng_bFogEnabled = false\ng_vColorTint = [1.0, 1.0, 1.0]\n"
                   "g_tColor = resource:\"materials/dev/primary_white_color_tga_21186c76.vtex\"\n}\n";
        if (!ParseMaterial(load, create, &mat, i ? "awareness_hidden_14181_v318" : "awareness_visible_14181_v318") ||
            !memory.Read(reinterpret_cast<std::uintptr_t>(mat.binding), mat.object) || !mat.object) {
            char message[256]{};
            std::snprintf(
                message, sizeof(message),
                "Model fill: material %u stage=%u exception=%08X binding=%p object=%p fault=%p access=%p kv=%p.", i,
                mat.parseStage, mat.exception, mat.binding, reinterpret_cast<void *>(mat.object),
                reinterpret_cast<void *>(mat.fault), reinterpret_cast<void *>(mat.access), &mat.kv);
            OverlayLog(message);
            return false;
        }
        std::uintptr_t vtable{};
        const auto base = reinterpret_cast<std::uintptr_t>(state.material);
        if (!memory.Read(mat.object, vtable) || vtable < base || vtable >= base + offsets::MaterialImageSize) {
            char message[256]{};
            std::snprintf(
                message, sizeof(message), "Model fill: material %u object=%p vtable=%p outside material module=%p.", i,
                reinterpret_cast<void *>(mat.object), reinterpret_cast<void *>(vtable), reinterpret_cast<void *>(base));
            OverlayLog(message);
            return false;
        }
    }
    return true;
}
void *DrawHook(void *descriptor, void *context, const model::Packet *packets, int count, void *viewData, void *passData,
               void *stats) noexcept {
    state.inFlight.fetch_add(1, std::memory_order_acquire);
    struct Guard {
        ~Guard() { state.inFlight.fetch_sub(1, std::memory_order_release); }
    } guard;
    const auto original = state.original;
    const auto passthrough = [&] { return original(descriptor, context, packets, count, viewData, passData, stats); };
    if (!state.enabled.load(std::memory_order_acquire) || GetTickCount64() > state.deadline.load() || !packets ||
        count < 1 || count > 8192)
        return passthrough();
    LocalMemory local;
    const Memory memory{&local, LocalMemory::Read};
    std::uint32_t pass{};
    if (!memory.Field(reinterpret_cast<std::uintptr_t>(passData), offsets::ViewPass, pass) || !model::ColorPass(pass))
        return passthrough();
    const auto selection = state.selection.load(std::memory_order_acquire);
    if (!selection || selection->entries.empty())
        return passthrough();
    // Common batches use stack storage; no TLS heap/destructors survive DLL unload.
    model::DrawBuffer buffer;
    thread_local bool collecting{};
    if (collecting)
        return passthrough();
    collecting = true;
    struct ScratchGuard {
        ~ScratchGuard() { collecting = false; }
    } scratchGuard;
    const model::Target *lastTarget{};
    std::uintptr_t lastMesh{};
    bool owned{}, player{};
    try {
        for (int i = 0; i < count; ++i) {
            model::Packet packet;
            if (!memory.Read(reinterpret_cast<std::uintptr_t>(packets + i), packet))
                continue;
            const auto scene = packet.Get<std::uintptr_t>(offsets::PacketSceneObject);
            const auto *target = selection->Find(scene);
            if (!target)
                continue;
            if (target != lastTarget) {
                lastTarget = target;
                owned = model::StillOwned(memory, *target);
            }
            if (!owned)
                continue;
            const auto mesh = packet.Get<std::uintptr_t>(0);
            if (mesh != lastMesh) {
                lastMesh = mesh;
                player = model::PlayerModel(memory, packet);
            }
            if (player)
                buffer.Push(packet, target->opacity, static_cast<std::uint32_t>(i));
        }
    } catch (...) {
        return passthrough(); // Allocation failed before submitting any part of the batch.
    }
    const auto items = buffer.Items();
    if (items.empty() || !state.enabled.load(std::memory_order_acquire))
        return passthrough();
    void *result{};
    const auto submit = [&](std::span<const model::Packet> layer) {
        result = original(descriptor, context, layer.data(), static_cast<int>(layer.size()), viewData, passData, stats);
    };
    if (model::CanCompose(items, selection->config, selection->shaded)) {
        // Preserve every unselected packet. Selected visible models replace their original
        // color pass instead of drawing the original plus another complete visible layer.
        std::array<model::Packet, 128> localPackets;
        std::vector<model::Packet> large;
        if (count > static_cast<int>(localPackets.size())) {
            try {
                large.resize(count);
            } catch (...) {
                return passthrough();
            }
        }
        auto *base = large.empty() ? localPackets.data() : large.data();
        if (!LocalMemory::Read(&local, reinterpret_cast<std::uintptr_t>(packets), base, count * sizeof(model::Packet)))
            return passthrough();
        for (const auto &item : items)
            base[item.sourceIndex] = model::VisiblePacket(item.packet, item.opacity, selection->config,
                                                          state.materials[0].object, selection->shaded);
        auto hidden = selection->config;
        hidden.materialColor =
            hidden.visibility == EffectVisibility::TwoColor ? hidden.glowColor : hidden.materialColor;
        hidden.visibility = EffectVisibility::OccludedOnly;
        model::DrawBatch(items, hidden, state.materials[0].object, state.materials[1].object, submit,
                         [&] { submit({base, static_cast<std::size_t>(count)}); });
    } else if (selection->config.visibility == EffectVisibility::AlwaysVisible) {
        // Transparent single color is applied to this frame's animated model.
        result = passthrough();
        auto layer = selection->config;
        layer.visibility = EffectVisibility::OccludedOnly;
        model::DrawBatch(items, layer, state.materials[0].object, state.materials[1].object, submit, [] {});
    } else {
        model::DrawBatch(items, selection->config, state.materials[0].object, state.materials[1].object, submit,
                         [&] { result = passthrough(); });
    }
    state.draws.fetch_add(static_cast<unsigned>(items.size()), std::memory_order_relaxed);
    return result;
}
} // namespace

HRESULT StartModelFill() noexcept {
    if (state.installed)
        return S_OK;
    if (state.attempted)
        return state.startup;
    state.attempted = true;
    state.startup = HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    try {
        state.scene = GetModuleHandleW(L"scenesystem.dll");
        state.material = GetModuleHandleW(L"materialsystem2.dll");
        if (!ValidateLayout()) {
            OverlayLog("Model fill: rendering layout does not match the verified build.");
            return state.startup;
        }
        state.startup = E_FAIL;
        if (!CreateMaterials()) {
            OverlayLog("Model fill: the game could not create the two materials.");
            return state.startup;
        }
        state.address =
            reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(state.scene) + offsets::RenderCallback);
        if (MH_CreateHook(state.address, reinterpret_cast<void *>(&DrawHook),
                          reinterpret_cast<void **>(&state.original)) != MH_OK)
            return state.startup;
        if (MH_EnableHook(state.address) != MH_OK) {
            MH_RemoveHook(state.address);
            state.original = nullptr;
            return state.startup;
        }
        state.installed = true;
        state.startup = S_OK;
        OverlayLog("Model fill: CsgoForward callback and two materials connected; awaiting eligible draws.");
        return S_OK;
    } catch (...) {
        return state.startup;
    }
}
void PauseModelFill() noexcept {
    state.enabled.store(false, std::memory_order_release);
    state.deadline = 0;
    state.draws = 0;
}
bool UpdateModelFill(const FrameSnapshot &, const Configuration &config, const EffectsConfiguration &effects,
                     bool verified, EffectsState &output, bool shaded, const model::Targets *cached) noexcept {
    if (!verified || !config.enabled || !(effects.materialEnabled || effects.glowEnabled)) {
        PauseModelFill();
        return false;
    }
    if (!state.installed) {
        PauseModelFill();
        return false;
    }
    // Entity/scene enumeration is done by the snapshot worker. This function only
    // publishes an immutable selection for draw callbacks.
    if (!cached) {
        PauseModelFill();
        return false;
    }
    const auto &targets = *cached;
    if (targets.count > targets.entries.size() || !ValidEffectsConfiguration(effects)) {
        PauseModelFill();
        output = {};
        output.status = EffectsStatus::Failed;
        output.result = E_INVALIDARG;
        return false;
    }
    const auto draws = state.draws.exchange(0);
    output = {};
    output.meshCount = draws;
    output.status = !targets.count ? EffectsStatus::NoGeometry
                    : draws        ? EffectsStatus::NativeMaterialReady
                                   : EffectsStatus::NativeMaterialPending;
    output.depthAvailable = draws ? 1u : 0u;
    output.result = draws ? S_OK : S_FALSE;
    try {
        std::shared_ptr<model::Selection> next;
        for (auto &slot : state.pool) {
            if (!slot) {
                slot = std::make_shared<model::Selection>(model::Targets{}, effects, shaded);
                slot->entries.reserve(model::MaxModels);
            }
            // A published selection and in-flight draw callbacks own references.
            if (slot.use_count() == 1) {
                next = slot;
                break;
            }
        }
        if (next) {
            next->entries.assign(targets.entries.begin(), targets.entries.begin() + targets.count);
            std::sort(next->entries.begin(), next->entries.end(),
                      [](const auto &a, const auto &b) { return a.scene < b.scene; });
            next->config = effects;
            next->shaded = shaded;
            state.selection.store(std::move(next), std::memory_order_release);
        }
    } catch (...) {
        PauseModelFill();
        output.status = EffectsStatus::Failed;
        output.result = E_OUTOFMEMORY;
        return false;
    }
    // A stalled/minimized Present must never leave old pawn selection active.
    state.deadline = GetTickCount64() + 250;
    state.enabled.store(targets.count != 0, std::memory_order_release);
    return true;
}
HRESULT StopModelFill() noexcept {
    PauseModelFill();
    if (!state.installed)
        return S_OK;
    const auto result = MH_DisableHook(state.address);
    if (result != MH_OK && result != MH_ERROR_DISABLED)
        return E_FAIL;
    const auto deadline = GetTickCount64() + 5000;
    while (state.inFlight.load(std::memory_order_acquire)) {
        if (GetTickCount64() > deadline)
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        Sleep(1);
    }
    if (MH_RemoveHook(state.address) != MH_OK)
        return E_FAIL;
    state.installed = false;
    state.original = nullptr;
    state.address = nullptr;
    state.selection.store(nullptr, std::memory_order_release);
    // Named materials belong to Source 2's resource cache. Keep the two bindings
    // for this process instead of repeatedly allocating them on feature toggles.
    state.attempted = false;
    return S_OK;
}
} // namespace awareness::cs2
