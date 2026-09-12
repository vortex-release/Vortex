#include "game_frame.hpp"
#include "build_verification.hpp"
#include "runtime_support.hpp"
#include <MinHook.h>
#include "local_memory.hpp"
#include <Psapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
namespace awareness::cs2::frame {
namespace {
constexpr std::uintptr_t Entry = 0xB29E60, TableSlot = 0x1B20E18;
constexpr std::array<unsigned char, 24> Bytes{0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x57, 0x48,
                                              0x83, 0xEC, 0x40, 0x48, 0x8B, 0xF9, 0x33, 0xED, 0x48, 0x8B, 0x89, 0x40};
using Original = void (*)(void *, int);
struct State {
    std::mutex lifecycle;
    std::array<Callback, 8> callbacks{};
    std::size_t count{};
    std::uintptr_t client{};
    Original original{};
    bool created{}, enabled{};
    std::atomic<bool> available{};
    std::atomic<unsigned> inFlight{};
    std::atomic<std::uint32_t> failed{};
    std::atomic<std::uint64_t> calls{};
    std::atomic_flag dispatching = ATOMIC_FLAG_INIT;
} state;
struct Guard {
    Guard() noexcept { state.inFlight.fetch_add(1, std::memory_order_acq_rel); }
    ~Guard() { state.inFlight.fetch_sub(1, std::memory_order_acq_rel); }
};
bool Verified(std::uintptr_t client) noexcept {
    LocalMemory local;
    const Memory m{&local, LocalMemory::Read};
    const auto engine = GetModuleHandleW(L"engine2.dll");
    MODULEINFO ci{}, ei{};
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::array<unsigned char, Bytes.size()> bytes{};
    std::uintptr_t slot{};
    if (!engine || !GetModuleInformation(GetCurrentProcess(), engine, &ei, sizeof(ei)) ||
        !GetModuleInformation(GetCurrentProcess(), reinterpret_cast<HMODULE>(client), &ci, sizeof(ci)))
        return false;
    const auto b = VerifyEngineBuild(m, reinterpret_cast<std::uintptr_t>(engine), ei.SizeOfImage);
    return b.check == BuildCheck::Verified && b.build == offsets::ExpectedBuild && m.Read(client, dos) &&
           dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 && dos.e_lfanew < 4096 &&
           m.Read(client + dos.e_lfanew, nt) && nt.Signature == IMAGE_NT_SIGNATURE &&
           nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
           nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
           nt.FileHeader.TimeDateStamp == trajectory_offsets::ClientTimestamp &&
           nt.OptionalHeader.SizeOfImage == trajectory_offsets::ClientImageSize &&
           ci.SizeOfImage == trajectory_offsets::ClientImageSize && m.Read(client + Entry, bytes) && bytes == Bytes &&
           m.Read(client + TableSlot, slot) && slot == client + Entry;
}
// Isolate a failing extension; the original game's call is never inside this SEH boundary.
bool Invoke(Callback fn, int stage) noexcept {
    __try {
        fn(stage);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void Hook(void *self, int stage) {
    Guard guard;
    state.original(self, stage);
    if (!state.available.load(std::memory_order_acquire) || state.dispatching.test_and_set(std::memory_order_acquire))
        return;
    if (state.available.load(std::memory_order_acquire)) {
        for (std::size_t i = 0; i < state.count; ++i) {
            const auto bit = std::uint32_t{1} << i;
            if (!(state.failed.load(std::memory_order_relaxed) & bit) && !Invoke(state.callbacks[i], stage))
                state.failed.fetch_or(bit, std::memory_order_relaxed);
        }
        state.calls.fetch_add(1, std::memory_order_relaxed);
    }
    state.dispatching.clear(std::memory_order_release);
}
} // namespace
HRESULT Start(std::span<const Callback> callbacks) noexcept {
    if (callbacks.empty() || callbacks.size() > state.callbacks.size())
        return E_INVALIDARG;
    for (auto c : callbacks)
        if (!c)
            return E_INVALIDARG;
    std::scoped_lock lock(state.lifecycle);
    if (state.created)
        return state.enabled ? S_OK : HRESULT_FROM_WIN32(ERROR_BUSY);
    const auto client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    if (!Verified(client))
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    const auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
        return E_FAIL;
    state.client = client;
    state.count = callbacks.size();
    std::copy(callbacks.begin(), callbacks.end(), state.callbacks.begin());
    state.failed = 0;
    state.calls = 0;
    state.dispatching.clear();
    auto entry = reinterpret_cast<void *>(client + Entry);
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
    OverlayLog("Native extensions connected to the verified frame dispatcher.");
    return S_OK;
}
HRESULT Stop() noexcept {
    std::scoped_lock lock(state.lifecycle);
    state.available.store(false, std::memory_order_release);
    if (!state.created)
        return S_OK;
    auto entry = reinterpret_cast<void *>(state.client + Entry);
    if (state.enabled) {
        const auto disabled = MH_DisableHook(entry);
        if (disabled != MH_OK && disabled != MH_ERROR_DISABLED)
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
    state.callbacks = {};
    state.count = 0;
    state.client = 0;
    return S_OK;
}
Status Diagnostics() noexcept {
    return {state.available.load(), state.failed.load(), state.calls.load()};
}
} // namespace awareness::cs2::frame
