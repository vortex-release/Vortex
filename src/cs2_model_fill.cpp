#include "cs2_model_fill.hpp"
#include "cs2_model_draw.hpp"
#include "native_model_mask.hpp"
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
using NativeDraw = void (*)(void *, void *, const model::Packet *, int, void *, void *, void *);
using QueueDraw = void (*)(void *, UINT, UINT, UINT, UINT, UINT, INT, UINT);
using Generate = void *(*)(void *, void *, void *, void *);
constexpr std::uintptr_t GeneratePrimitivesRva = 0x70520, GenerateVtableSlot = 0x5d9798;
struct Material {
    // Build 14181 GetContext tests bit 0 before decoding a pooled allocation
    // index. A standalone root must set it; a zeroed root reads before itself.
    struct alignas(16) KeyValues {
        std::uint64_t flags{1}, value{};
    } kv;
    void *binding{};
    std::uintptr_t object{};
    std::atomic<std::uintptr_t> forwardVariant{};
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
    Generate original{};
    NativeDraw originalDraw{};
    QueueDraw originalQueue{};
    void *queueAddress{};
    std::atomic<bool> queueInstalled{};
    void *drawAddress{};
    std::atomic<bool> drawInstalled{};
    std::uintptr_t softwareVtable{};
    void *address{};
    HMODULE scene{}, material{};
    Material materials[2];
    std::atomic<unsigned> callbacks{}, selectedScenes{}, rejectedOwnership{}, rejectedModels{}, generated{};
    HRESULT startup{E_PENDING};
    ULONGLONG materialPoll{};
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
    std::uintptr_t slot{}, generateSlot{};
    const unsigned char generateBytes[]{0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 8,    0x48, 0x89, 0x50, 0x10,
                                        0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
    return CodeMatches(scene, offsets::RenderCallback, drawBytes) &&
           CodeMatches(reinterpret_cast<std::uintptr_t>(state.material), offsets::CreateMaterial, materialBytes) &&
           memory.Field(scene, offsets::RenderVtableSlot, slot) && slot == scene + offsets::RenderCallback &&
           CodeMatches(scene, GeneratePrimitivesRva, generateBytes) &&
           memory.Field(scene, GenerateVtableSlot, generateSlot) && generateSlot == scene + GeneratePrimitivesRva;
}
// Build-guarded software replay ABI, verified in the matching installed PE and
// saved dump. Tags live outside engine memory; no command opcode/count is changed.
bool ValidateReplay() noexcept {
    const auto module = GetModuleHandleW(L"rendersystemdx11.dll");
    if (!ModuleMatches(module, 0x6aa1ae0a, 0x4ad000))
        return false;
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    constexpr unsigned char bytes[]{0x8b, 0x46, 0x10, 0x4c, 0x8d, 0x46, 0xfc, 0x0f, 0xb7, 0x76, 0xfe, 0x49, 0x8b, 0x8e,
                                    0xe0, 0x03, 0,    0,    0x49, 0x03, 0xf0, 0x45, 0x8b, 0x48, 0x0c, 0x41, 0x8b, 0x17,
                                    0x89, 0x44, 0x24, 0x28, 0x41, 0x8b, 0x40, 0x10, 0x4c, 0x8b, 0x11, 0x45, 0x8b, 0x40,
                                    0x08, 0x89, 0x44, 0x24, 0x20, 0x41, 0xff, 0x92, 0xa0, 0,    0,    0};
    LocalMemory local;
    const Memory memory{&local, LocalMemory::Read};
    std::uintptr_t entry{};
    constexpr unsigned char queueBytes[]{0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89,
                                         0x74, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xec, 0x30, 0x41, 0x8b, 0xf1};
    if (!CodeMatches(base, 0x26be0, queueBytes) || !CodeMatches(base, 0x5f03d, bytes) ||
        !memory.Field(base, 0x3e7d90 + 0x2f0, entry) || entry != base + 0x26be0)
        return false;
    state.softwareVtable = base + 0x3e7d90;
    state.queueAddress = reinterpret_cast<void *>(base + 0x26be0);
    native_mask::SetReplaySite(base + 0x5f073);
    return true;
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
// GeneratePrimitives resolves material vtable slot 6 with the pass token at
// +0x30 before writing packet+0x28 (build 14181, scenesystem+0x71005).
std::uintptr_t ResolveForwardVariant(std::uintptr_t object) noexcept {
    const auto module = reinterpret_cast<std::uintptr_t>(state.material);
    __try {
        auto *table = *reinterpret_cast<std::uintptr_t **>(object);
        const auto entry = table[6];
        if (entry < module || entry >= module + offsets::MaterialImageSize)
            return 0;
        auto token = model::PassToken("CsgoForward");
        using Resolve = std::uintptr_t (*)(void *, const std::uint32_t *);
        const auto getPass = table[5];
        if (getPass < module || getPass >= module + offsets::MaterialImageSize ||
            !reinterpret_cast<Resolve>(getPass)(reinterpret_cast<void *>(object), &token))
            return 0; // Slot 6 alone returns the base material even for a missing pass.
        return reinterpret_cast<Resolve>(entry)(reinterpret_cast<void *>(object), &token);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
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
    for (unsigned i = 0; i < std::size(state.materials); ++i) {
        auto &mat = state.materials[i];
        if (mat.object && mat.forwardVariant)
            continue;
        // Blend mode 1 is already translucent. F_TRANSLUCENT is only valid with
        // mode 2 (alpha test); combining them requests an invalid shader feature set.
        mat.text = "<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} "
                   "format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->\n{\n"
                   "shader = \"csgo_unlitgeneric.vfx\"\nF_DISABLE_Z_PREPASS = 1\nF_RENDER_BACKFACES = 0\n"
                   "F_DISABLE_Z_BUFFERING = 0\nF_BLEND_MODE = 1\nF_PAINT_VERTEX_COLORS = 1\ng_bFogEnabled = false\n"
                   "g_vColorTint = [1.0, 1.0, 1.0]\n"
                   "g_tColor = resource:\"materials/dev/primary_white_color_tga_21186c76.vtex\"\n}\n";
        if (i == 1) {
            // Lit counterpart retains shape lighting while replacing the game's
            // texture; tinting a white original packet alone is a no-op.
            const auto shader = mat.text.find("csgo_unlitgeneric.vfx");
            mat.text.replace(shader, std::strlen("csgo_unlitgeneric.vfx"), "csgo_complex.vfx");
            mat.text.insert(mat.text.rfind('}'),
                            "g_tNormal = resource:\"materials/default/default_normal_tga_7652cb.vtex\"\n"
                            "g_flModelTintAmount = 1.0\n");
        }
        if (!ParseMaterial(load, create, &mat, i ? "vortex_lit_14181_v326" : "vortex_flat_14181_v326") ||
            !memory.Read(reinterpret_cast<std::uintptr_t>(mat.binding), mat.object) || !mat.object) {
            char message[256]{};
            std::snprintf(
                message, sizeof(message),
                "Model fill: material %u stage=%u exception=%08X binding=%p object=%p fault=%p access=%p kv=%p.", i,
                mat.parseStage, mat.exception, mat.binding, reinterpret_cast<void *>(mat.object),
                reinterpret_cast<void *>(mat.fault), reinterpret_cast<void *>(mat.access), &mat.kv);
            OverlayLog(message);
            if (i == 0)
                return false;
            continue;
        }
        std::uintptr_t vtable{};
        const auto base = reinterpret_cast<std::uintptr_t>(state.material);
        if (!memory.Read(mat.object, vtable) || vtable < base || vtable >= base + offsets::MaterialImageSize) {
            char message[256]{};
            std::snprintf(
                message, sizeof(message), "Model fill: material %u object=%p vtable=%p outside material module=%p.", i,
                reinterpret_cast<void *>(mat.object), reinterpret_cast<void *>(vtable), reinterpret_cast<void *>(base));
            OverlayLog(message);
            if (i == 0)
                return false;
            continue;
        }
        mat.forwardVariant = ResolveForwardVariant(mat.object);
        if (!mat.forwardVariant) {
            OverlayLog("Model fill: CsgoForward material pass is pending; effect remains inactive until it is ready.");
        }
    }
    return true;
}
// Preserve Source's glow overlay; replacing its material would consume the
// separate outline pass. Current GetName is materialsystem2+0xB400 -> object+0x10.
bool GlowMaterial(const Memory &memory, std::uintptr_t object) noexcept {
    std::uintptr_t table{}, getter{}, name{};
    std::array<char, 128> text{};
    if (!memory.Read(object, table) || !memory.Read(table, getter) ||
        getter != reinterpret_cast<std::uintptr_t>(state.material) + 0xb400 || !memory.Field(object, 0x10, name) ||
        !memory.Read(name, text))
        return false;
    return std::string_view{text.data(), text.size()}.find("glowproperty") != std::string_view::npos;
}
bool SoftwareCursor(const Memory &memory, std::uintptr_t context, std::uintptr_t &cursor,
                    std::uintptr_t &end) noexcept {
    std::uintptr_t table{};
    return state.softwareVtable && memory.Read(context, table) && table == state.softwareVtable &&
           memory.Field(context, 0x420, cursor) && memory.Field(context, 0x428, end) && end >= 0x8000 &&
           cursor >= end - 0x8000 && cursor <= end;
}
// Every indexed command record passes here, including non-player replacements.
// This prevents a recycled command address with identical draw arguments from
// inheriting the selection of a canceled earlier command in the same frame.
void QueueHook(void *context, UINT topology, UINT first, UINT count, UINT instances, UINT vertices, INT base,
               UINT startInstance) noexcept {
    state.inFlight.fetch_add(1, std::memory_order_acquire);
    struct Guard {
        ~Guard() { state.inFlight.fetch_sub(1, std::memory_order_release); }
    } guard;
    const auto original = state.originalQueue;
    if (!native_mask::Enabled()) {
        original(context, topology, first, count, instances, vertices, base, startInstance);
        return;
    }
    // Page metadata is reused during this frame; the underlying reads still use
    // LocalMemory's SEH copy guard. Do not zero a 2 KiB cache or VirtualQuery the
    // same software context/command block on every world draw.
    thread_local LocalMemory local;
    thread_local std::uint64_t generation{};
    const auto current = native_mask::Generation();
    if (generation != current) {
        local.Reset();
        generation = current;
    }
    const Memory memory{&local, LocalMemory::Read};
    const auto address = reinterpret_cast<std::uintptr_t>(context);
    std::uintptr_t before{}, table{};
    const bool readable =
        memory.Read(address, table) && table == state.softwareVtable && memory.Field(address, 0x420, before);
    original(context, topology, first, count, instances, vertices, base, startInstance);
    if (!readable)
        return;
    std::uintptr_t after{}, end{};
    std::array<UINT, 6> command{};
    if (!SoftwareCursor(memory, address, after, end) || after == before || after < end - 0x8000 + sizeof(command) ||
        !memory.Read(after - sizeof(command), command) || command[0] != 0x0018801f)
        return;
    const auto commandAddress = after - sizeof(command);
    native_mask::Forget(commandAddress);
    const std::array<UINT, 5> args{count, instances, first, static_cast<UINT>(base), startInstance};
    if (command[1] != count || command[2] != instances || command[3] != first ||
        command[4] != static_cast<UINT>(base) || command[5] != startInstance || !count || !instances)
        return;
    const auto color = native_mask::SelectedColor();
    if (color >> 24)
        native_mask::Annotate(commandAddress, args, color);
}
void DrawHook(void *descriptor, void *context, const model::Packet *packets, int count, void *view, void *pass,
              void *extra) noexcept {
    state.inFlight.fetch_add(1, std::memory_order_acquire);
    struct Guard {
        ~Guard() { state.inFlight.fetch_sub(1, std::memory_order_release); }
    } guard;
    const auto original = state.originalDraw;
    const auto forward = [&](int first, int n) {
        original(descriptor, context, packets + first, n, view, pass, extra);
    };
    if (!state.enabled.load(std::memory_order_acquire) || !native_mask::Enabled() || !packets || count < 1 ||
        count > 4096 || GetTickCount64() > state.deadline.load(std::memory_order_acquire)) {
        original(descriptor, context, packets, count, view, pass, extra);
        return;
    }
    const auto selection = state.selection.load(std::memory_order_acquire);
    if (!selection || !selection->config.materialEnabled ||
        selection->config.visibility == EffectVisibility::AlwaysVisible) {
        forward(0, count);
        return;
    }
    LocalMemory local;
    const Memory memory{&local, LocalMemory::Read};
    std::uint32_t token{};
    if (!memory.Field(reinterpret_cast<std::uintptr_t>(pass), offsets::ViewPass, token) || !model::ColorPass(token)) {
        forward(0, count);
        return;
    }
    thread_local bool drawing{};
    if (drawing) {
        forward(0, count);
        return;
    }
    drawing = true;
    struct Recursion {
        ~Recursion() { drawing = false; }
    } recursion;
    int pending{};
    for (int i = 0; i < count; ++i) {
        model::Packet packet;
        if (!memory.Read(reinterpret_cast<std::uintptr_t>(packets + i), packet))
            continue;
        const auto *target = selection->Find(packet.Get<std::uintptr_t>(offsets::PacketSceneObject));
        if (!target || !std::isfinite(target->opacity) || target->opacity <= 0 || !model::StillOwned(memory, *target) ||
            !model::PlayerModel(memory, packet) ||
            GlowMaterial(memory, packet.Get<std::uintptr_t>(offsets::PacketMaterial)) ||
            GlowMaterial(memory, packet.Get<std::uintptr_t>(0x28)))
            continue;
        const auto color = model::PackColor(selection->config.glowColor, target->opacity);
        if (!(color >> 24))
            continue;
        if (i > pending)
            forward(pending, i - pending);
        {
            native_mask::Scope scope(color);
            forward(i, 1);
        }
        pending = i + 1;
    }
    if (pending < count)
        forward(pending, count - pending);
}
void *GenerateHook(void *descriptor, void *scene, void *view, void *output) noexcept {
    state.inFlight.fetch_add(1, std::memory_order_acquire);
    struct Guard {
        ~Guard() { state.inFlight.fetch_sub(1, std::memory_order_release); }
    } guard;
    const auto original = state.original;
    const auto passthrough = [&] { return original(descriptor, scene, view, output); };
    if (!state.enabled.load(std::memory_order_acquire) || GetTickCount64() > state.deadline.load() || !scene || !output)
        return passthrough();
    state.callbacks.fetch_add(1, std::memory_order_relaxed);
    const auto selection = state.selection.load(std::memory_order_acquire);
    if (!selection || !model::HasVisibleTint(selection->config))
        return passthrough();
    const auto *target = selection->Find(reinterpret_cast<std::uintptr_t>(scene));
    if (!target || !std::isfinite(target->opacity) || target->opacity <= 0)
        return passthrough();
    state.selectedScenes.fetch_add(1, std::memory_order_relaxed);
    LocalMemory local;
    const Memory memory{&local, LocalMemory::Read};
    std::uintptr_t passAddress{};
    std::uint32_t pass{};
    if (!memory.Field(reinterpret_cast<std::uintptr_t>(view), 0x10, passAddress) ||
        !memory.Field(passAddress, offsets::ViewPass, pass) || !model::ColorPass(pass))
        return passthrough();
    if (!model::StillOwned(memory, *target)) {
        state.rejectedOwnership.fetch_add(1, std::memory_order_relaxed);
        return passthrough();
    }
    thread_local bool generating{};
    if (generating)
        return passthrough();
    generating = true;
    struct RecursionGuard {
        ~RecursionGuard() { generating = false; }
    } recursionGuard;
    model::PrimitiveBuffer before{}, after{};
    const auto address = reinterpret_cast<std::uintptr_t>(output);
    if (!model::ReadPrimitiveBuffer(memory, address, before))
        return passthrough();
    // Original executes exactly once. Color is changed before the engine sorts
    // primitives and uploads their per-instance data, not during DrawArray.
    void *result = passthrough();
    local.Reset();
    if (!model::ReadPrimitiveBuffer(memory, address, after) || !state.enabled.load(std::memory_order_acquire))
        return result;
    const auto &material = state.materials[selection->shaded && state.materials[1].forwardVariant ? 1 : 0];
    if (!material.forwardVariant.load(std::memory_order_acquire))
        return result;
    unsigned changed{};
    model::ForEachAppended(before, after, [&](std::uintptr_t packetAddress) {
        model::Packet packet;
        if (!memory.Read(packetAddress, packet) ||
            packet.Get<std::uintptr_t>(offsets::PacketSceneObject) != target->scene)
            return;
        state.generated.fetch_add(1, std::memory_order_relaxed);
        if (!model::PlayerModel(memory, packet)) {
            state.rejectedModels.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (GlowMaterial(memory, packet.Get<std::uintptr_t>(offsets::PacketMaterial)) ||
            GlowMaterial(memory, packet.Get<std::uintptr_t>(0x28)))
            return;
        const auto color = model::PackColor(selection->config.materialColor, target->opacity);
        if (!(color >> 24))
            return;
        packet.Set(offsets::PacketMaterial, material.object);
        packet.Set(0x28, material.forwardVariant.load(std::memory_order_acquire));
        packet.Set(offsets::PacketColor, color);
        changed += model::WritePrimitive(packetAddress, packet);
    });
    state.draws.fetch_add(changed, std::memory_order_relaxed);
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
            OverlayLog("Model fill: the game could not create the depth-tested material.");
            return state.startup;
        }
        state.address = reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(state.scene) + GeneratePrimitivesRva);
        if (MH_CreateHook(state.address, reinterpret_cast<void *>(&GenerateHook),
                          reinterpret_cast<void **>(&state.original)) != MH_OK)
            return state.startup;
        if (MH_EnableHook(state.address) != MH_OK) {
            MH_RemoveHook(state.address);
            state.original = nullptr;
            return state.startup;
        }
        state.installed = true;
        if (ValidateReplay()) {
            state.drawAddress =
                reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(state.scene) + offsets::RenderCallback);
            if (MH_CreateHook(state.drawAddress, reinterpret_cast<void *>(&DrawHook),
                              reinterpret_cast<void **>(&state.originalDraw)) == MH_OK) {
                if (MH_EnableHook(state.drawAddress) == MH_OK) {
                    state.drawInstalled = true;
                    if (MH_CreateHook(state.queueAddress, reinterpret_cast<void *>(&QueueHook),
                                      reinterpret_cast<void **>(&state.originalQueue)) == MH_OK) {
                        if (MH_EnableHook(state.queueAddress) == MH_OK)
                            state.queueInstalled = true;
                        else {
                            MH_RemoveHook(state.queueAddress);
                            state.originalQueue = nullptr;
                        }
                    }
                } else {
                    MH_RemoveHook(state.drawAddress);
                    state.originalDraw = nullptr;
                }
            }
        }
        if (!state.drawInstalled || !state.queueInstalled)
            OverlayLog("Model fill: hidden replay bridge unavailable; visible material remains active.");
        state.startup = S_OK;
        OverlayLog("Model fill: verified primitive generator and cached flat/lit materials connected; awaiting "
                   "eligible players.");
        return S_OK;
    } catch (...) {
        return state.startup;
    }
}
void PauseModelFill() noexcept {
    state.enabled.store(false, std::memory_order_release);
    state.deadline = 0;
    state.draws = 0;
    native_mask::Discard();
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
    // Shader resources can finish asynchronously after material creation. Poll
    // only unresolved handles, at most once a second; never recreate a material
    // from Present or a render worker.
    const auto now = GetTickCount64();
    if (now >= state.materialPoll) {
        state.materialPoll = now + 1000;
        for (auto &material : state.materials)
            if (material.object && !material.forwardVariant.load(std::memory_order_acquire))
                material.forwardVariant.store(ResolveForwardVariant(material.object), std::memory_order_release);
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
    output.depthAvailable = state.drawInstalled && state.queueInstalled && native_mask::Enabled();
    if (effects.visibility != EffectVisibility::AlwaysVisible && targets.count && !output.depthAvailable)
        output.status = EffectsStatus::DepthUnavailable;
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
ModelFillDiagnostics GetModelFillDiagnostics() noexcept {
    ModelFillDiagnostics result;
    const auto selection = state.selection.load(std::memory_order_acquire);
    result.selectedObjects = selection ? static_cast<unsigned>(selection->entries.size()) : 0;
    result.callbacks = state.callbacks.load(std::memory_order_relaxed);
    result.selectedCallbacks = state.selectedScenes.load(std::memory_order_relaxed);
    result.rejectedOwnership = state.rejectedOwnership.load(std::memory_order_relaxed);
    result.rejectedModels = state.rejectedModels.load(std::memory_order_relaxed);
    result.generatedPackets = state.generated.load(std::memory_order_relaxed);
    result.flatReady = state.installed && state.materials[0].forwardVariant;
    result.litReady = state.installed && state.materials[1].forwardVariant;
    result.hiddenBridgeReady = state.installed && state.drawInstalled && state.queueInstalled;
    const auto hidden = native_mask::GetDiagnostics();
    result.hiddenQueued = hidden.queued;
    result.hiddenMatched = hidden.matched;
    result.hiddenCaptured = hidden.captured;
    result.hiddenDropped = hidden.dropped;
    return result;
}
HRESULT StopModelFill() noexcept {
    PauseModelFill();
    if (!state.installed)
        return S_OK;
    const auto result = MH_DisableHook(state.address);
    if (result != MH_OK && result != MH_ERROR_DISABLED)
        return E_FAIL;
    if (state.drawInstalled) {
        const auto r = MH_DisableHook(state.drawAddress);
        if (r != MH_OK && r != MH_ERROR_DISABLED)
            return E_FAIL;
    }
    if (state.queueInstalled) {
        const auto r = MH_DisableHook(state.queueAddress);
        if (r != MH_OK && r != MH_ERROR_DISABLED)
            return E_FAIL;
    }
    const auto deadline = GetTickCount64() + 5000;
    while (state.inFlight.load(std::memory_order_acquire)) {
        if (GetTickCount64() > deadline)
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        Sleep(1);
    }
    if (state.queueInstalled) {
        if (MH_RemoveHook(state.queueAddress) != MH_OK)
            return E_FAIL;
        state.queueInstalled = false;
        state.originalQueue = nullptr;
        state.queueAddress = nullptr;
    }
    if (state.drawInstalled) {
        if (MH_RemoveHook(state.drawAddress) != MH_OK)
            return E_FAIL;
        state.drawInstalled = false;
        state.originalDraw = nullptr;
        state.drawAddress = nullptr;
    }
    if (MH_RemoveHook(state.address) != MH_OK)
        return E_FAIL;
    state.installed = false;
    state.original = nullptr;
    state.address = nullptr;
    state.selection.store(nullptr, std::memory_order_release);

    // Named materials belong to Source 2's resource cache. Keep the material binding
    // for this process instead of repeatedly allocating them on feature toggles.
    native_mask::SetReplaySite(0);
    native_mask::Release();
    state.attempted = false;
    return S_OK;
}
} // namespace awareness::cs2
