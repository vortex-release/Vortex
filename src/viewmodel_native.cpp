#include "viewmodel_native.hpp"
#include "assist_reader.hpp"
#include "build_verification.hpp"
#include "runtime_support.hpp"
#include <MinHook.h>
#include <Psapi.h>
#include <atomic>
#include <mutex>

namespace awareness::cs2 {
namespace {
// Independently checked in the current on-disk client image. Anthony's MIT
// source identifies the function; its three-argument output ABI was verified
// from current machine code before adapting the frame-local customization.
constexpr std::uintptr_t EntryRva = 0x8A66D0;
constexpr std::array<unsigned char, 24> EntryBytes{0x40, 0x55, 0x53, 0x56, 0x41, 0x56, 0x41, 0x57,
                                                   0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x20, 0x4D,
                                                   0x8B, 0xF8, 0x4C, 0x8B, 0xF2, 0x48, 0x8B, 0xF1};
using Function = void (*)(void *pawn, float *xyz, float *fov);
struct State {
    std::mutex lifecycle, optionsMutex;
    std::uintptr_t client{};
    Function original{};
    bool created{}, enabled{};
    std::atomic<bool> available{};
    std::atomic<unsigned> inFlight{};
    std::atomic<ULONGLONG> deadline{};
    camera_visuals::Options options;
} state;
struct Guard {
    Guard() noexcept { state.inFlight.fetch_add(1, std::memory_order_acq_rel); }
    ~Guard() { state.inFlight.fetch_sub(1, std::memory_order_acq_rel); }
};
bool ValidImage(std::uintptr_t client) noexcept {
    LocalMemory local;
    const Memory m{&local, LocalMemory::Read};
    const auto engine = GetModuleHandleW(L"engine2.dll");
    MODULEINFO engineInfo{}, clientInfo{};
    if (!engine || !GetModuleInformation(GetCurrentProcess(), engine, &engineInfo, sizeof(engineInfo)) ||
        !GetModuleInformation(GetCurrentProcess(), reinterpret_cast<HMODULE>(client), &clientInfo, sizeof(clientInfo)))
        return false;
    const auto build = VerifyEngineBuild(m, reinterpret_cast<std::uintptr_t>(engine), engineInfo.SizeOfImage);
    if (build.check != BuildCheck::Verified || build.build != offsets::ExpectedBuild)
        return false;
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!m.Read(client, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 || dos.e_lfanew > 4096 ||
        !m.Read(client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != trajectory_offsets::ClientTimestamp ||
        nt.OptionalHeader.SizeOfImage != trajectory_offsets::ClientImageSize ||
        clientInfo.SizeOfImage != trajectory_offsets::ClientImageSize ||
        EntryRva + EntryBytes.size() > clientInfo.SizeOfImage)
        return false;
    std::array<unsigned char, EntryBytes.size()> actual{};
    return m.Read(client + EntryRva, actual) && actual == EntryBytes;
}
bool CommitOutputs(float *xyz, float *fov, Vector3 expected, float expectedFov, Vector3 desired,
                   float desiredFov) noexcept {
    __try {
        if (std::memcmp(xyz, &expected, sizeof(expected)) || *fov != expectedFov)
            return false;
        // These are the verified function's temporary output arguments, never
        // pawn fields. Original regenerates them on the next invocation.
        std::memcpy(xyz, &desired, sizeof(desired));
        *fov = desiredFov;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void Hook(void *self, float *xyz, float *fov) {
    Guard guard;
    state.original(self, xyz, fov);
    if (!state.available.load(std::memory_order_acquire) || GetTickCount64() > state.deadline.load() || !xyz || !fov)
        return;
    try {
        camera_visuals::Options options;
        {
            std::unique_lock lock(state.optionsMutex, std::try_to_lock);
            if (!lock.owns_lock())
                return;
            options = state.options;
        }
        if (!options.viewmodelEnabled && !options.hideScoped)
            return;
        LocalMemory local;
        const Memory m{&local, LocalMemory::Read};
        std::uintptr_t pawn{}, list{}, after{};
        std::uint32_t identity{}, checked{};
        std::uint8_t team{}, scoped{};
        if (!m.Field(state.client, offsets::LocalPawn, pawn) || pawn != reinterpret_cast<std::uintptr_t>(self) ||
            !AssistPawn(m, pawn, identity, team) || !m.Field(state.client, offsets::EntityList, list) ||
            EntityAt(m, list, identity) != pawn || !m.Field(pawn, offsets::IsScoped, scoped) || scoped > 1)
            return;
        Vector3 before{};
        float beforeFov{};
        if (!m.Read(reinterpret_cast<std::uintptr_t>(xyz), before) || !Finite(before) ||
            !m.Read(reinterpret_cast<std::uintptr_t>(fov), beforeFov) || !std::isfinite(beforeFov))
            return;
        auto desired = before;
        auto desiredFov = beforeFov;
        if (!camera_visuals::ViewmodelOutputs(options, scoped != 0, desired, desiredFov) ||
            !m.Field(state.client, offsets::LocalPawn, after) || after != pawn || !FullHandle(m, pawn, checked) ||
            checked != identity || !state.available.load(std::memory_order_acquire) ||
            GetTickCount64() > state.deadline.load())
            return;
        CommitOutputs(xyz, fov, before, beforeFov, desired, desiredFov);
    } catch (...) {
    }
}
} // namespace
HRESULT StartViewmodel() noexcept {
    std::scoped_lock lifecycle(state.lifecycle);
    if (state.created)
        return state.enabled ? S_OK : HRESULT_FROM_WIN32(ERROR_BUSY);
    state.available = false;
    state.deadline = 0;
    const auto module = GetModuleHandleW(L"client.dll");
    if (!module)
        return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    state.client = reinterpret_cast<std::uintptr_t>(module);
    if (!ValidImage(state.client)) {
        OverlayLog("Viewmodel customization unavailable: client build or function validation failed.");
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    }
    const auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
        return E_FAIL;
    auto *entry = reinterpret_cast<void *>(state.client + EntryRva);
    if (MH_CreateHook(entry, reinterpret_cast<void *>(&Hook), reinterpret_cast<void **>(&state.original)) != MH_OK)
        return E_FAIL;
    state.created = true;
    if (MH_EnableHook(entry) != MH_OK) {
        const auto removed = MH_RemoveHook(entry);
        if (removed == MH_OK || removed == MH_ERROR_NOT_CREATED) {
            state.created = false;
            state.original = nullptr;
        }
        return E_FAIL;
    }
    state.enabled = true;
    state.available.store(true, std::memory_order_release);
    OverlayLog("Viewmodel customization connected (verified output-only hook).");
    return S_OK;
}
void ConfigureViewmodel(const camera_visuals::Options &options, bool fresh) noexcept {
    std::scoped_lock lock(state.optionsMutex);
    if (!fresh || !camera_visuals::Valid(options)) {
        state.deadline = 0;
        return;
    }
    state.options = options;
    state.deadline = (options.viewmodelEnabled || options.hideScoped) ? GetTickCount64() + 250 : 0;
}
HRESULT StopViewmodel() noexcept {
    std::scoped_lock lifecycle(state.lifecycle);
    state.available.store(false, std::memory_order_release);
    state.deadline = 0;
    if (!state.created)
        return S_OK;
    auto *entry = reinterpret_cast<void *>(state.client + EntryRva);
    if (state.enabled) {
        const auto result = MH_DisableHook(entry);
        if (result != MH_OK && result != MH_ERROR_DISABLED)
            return E_FAIL;
        state.enabled = false;
    }
    const auto until = GetTickCount64() + 5000;
    while (state.inFlight.load(std::memory_order_acquire)) {
        if (GetTickCount64() > until)
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        Sleep(1);
    }
    const auto removed = MH_RemoveHook(entry);
    if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
        return E_FAIL;
    state.created = false;
    state.original = nullptr;
    state.client = 0;
    // MinHook is shared with the other subsystems; its global owner uninitializes it.
    return S_OK;
}
} // namespace awareness::cs2
