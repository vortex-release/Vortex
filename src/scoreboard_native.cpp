#include "scoreboard_native.hpp"
#include "scoreboard_reader.hpp"
#include "game_frame.hpp"
#include "runtime_support.hpp"
#include "local_memory.hpp"
#include <Psapi.h>
#include <atomic>
#include <mutex>
namespace awareness::cs2 {
namespace {
constexpr std::uintptr_t PanoramaSize = 0x5B0000, PanoramaTime = 1788980809, RunRva = 0xB6B50;
constexpr std::uintptr_t InterfaceVtable = 0x451E18, GetEngineRva = 0x6DA30, HudSlot = 0x243D0B8;
constexpr std::array<unsigned char, 24> RunBytes{0x4C, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x89, 0x44,
                                                 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55,
                                                 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41};
using CreateInterface = void *(*)(const char *, int *);
using RunScript = std::int64_t (*)(void *, void *, const char *, const char *, std::int64_t);
struct State {
    std::mutex optionsMutex;
    scoreboard::Options options;
    scoreboard::Frame last;
    scoreboard::Options lastOptions;
    std::uintptr_t client{}, panorama{}, interface{}, lastPanel{};
    RunScript run{};
    bool verified{}, rejected{};
    ULONGLONG next{}, retry{}, heartbeat{};
    std::atomic<ULONGLONG> deadline{};
    std::atomic<bool> connected{}, active{}, stopping{};
    std::atomic<DWORD> thread{};
    std::atomic<unsigned> inFlight{};
    std::atomic<std::uint64_t> updates{}, failures{};
} state;
struct TickGuard {
    TickGuard() noexcept { state.inFlight.fetch_add(1, std::memory_order_acq_rel); }
    ~TickGuard() { state.inFlight.fetch_sub(1, std::memory_order_acq_rel); }
};
bool Image(const Memory &m, std::uintptr_t base, std::uintptr_t time, std::uintptr_t size) noexcept {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    MODULEINFO info{};
    return base && GetModuleInformation(GetCurrentProcess(), reinterpret_cast<HMODULE>(base), &info, sizeof(info)) &&
           info.SizeOfImage == size && m.Read(base, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
           dos.e_lfanew < 4096 && m.Read(base + dos.e_lfanew, nt) && nt.Signature == IMAGE_NT_SIGNATURE &&
           nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
           nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC && nt.FileHeader.TimeDateStamp == time &&
           nt.OptionalHeader.SizeOfImage == size;
}
void *Create(CreateInterface fn) noexcept {
    __try {
        return fn("PanoramaUIEngine001", nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}
bool Execute(std::uintptr_t engine, std::uintptr_t panel, const char *script) noexcept {
    __try {
        state.run(reinterpret_cast<void *>(engine), reinterpret_cast<void *>(panel), script,
                  "panorama/vortex_equipment.js", 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool Initialize(const Memory &m, ULONGLONG now) noexcept {
    if (state.verified)
        return true;
    if (state.rejected || now < state.retry)
        return false;
    state.retry = now + 1000;
    state.client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    state.panorama = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"panorama.dll"));
    if (!state.client || !state.panorama)
        return false;
    std::array<unsigned char, RunBytes.size()> bytes{};
    if (!frame::Diagnostics().connected ||
        !Image(m, state.client, trajectory_offsets::ClientTimestamp, trajectory_offsets::ClientImageSize) ||
        !Image(m, state.panorama, PanoramaTime, PanoramaSize) || !m.Read(state.panorama + RunRva, bytes) ||
        bytes != RunBytes) {
        state.rejected = true;
        return false;
    }
    auto factory = GetProcAddress(reinterpret_cast<HMODULE>(state.panorama), "CreateInterface");
    if (reinterpret_cast<std::uintptr_t>(factory) != state.panorama + 0x37E760) {
        state.rejected = true;
        return false;
    }
    state.interface = reinterpret_cast<std::uintptr_t>(Create(reinterpret_cast<CreateInterface>(factory)));
    std::uintptr_t vt{}, get{};
    if (!m.Read(state.interface, vt) || vt != state.panorama + InterfaceVtable || !m.Read(vt + 13 * 8, get) ||
        get != state.panorama + GetEngineRva)
        return false;
    state.run = reinterpret_cast<RunScript>(state.panorama + RunRva);
    state.verified = true;
    state.connected = true;
    return true;
}
HudRead Context(const Memory &m, std::uintptr_t &engine, std::uintptr_t &panel) noexcept {
    ScoreboardHud hud;
    const auto stateOfHud = ReadScoreboardHud(m, state.client + HudSlot, hud);
    if (stateOfHud != HudRead::Present)
        return stateOfHud;
    std::uintptr_t iv{}, ev{}, fn{}, pv{};
    // Reacquire both objects on every update. No cached panel is dereferenced.
    if (!m.Read(state.interface, iv) || iv != state.panorama + InterfaceVtable ||
        !m.Field(state.interface, 0x28, engine) || !m.Read(engine, ev) || ev < state.panorama ||
        ev > state.panorama + PanoramaSize - 78 * 8 || !m.Read(ev + 77 * 8, fn) || fn != state.panorama + RunRva ||
        !m.Read(hud.panel, pv) || pv < state.panorama || pv >= state.panorama + PanoramaSize)
        return HudRead::Unreadable;
    panel = hud.panel;
    return HudRead::Present;
}
void Update(bool forceClear) {
    const auto now = GetTickCount64();
    if (!forceClear && now < state.next)
        return;
    state.next = now + 100;
    scoreboard::Options options;
    {
        std::unique_lock lock(state.optionsMutex, std::try_to_lock);
        if (!lock.owns_lock() && !forceClear)
            return;
        if (lock.owns_lock())
            options = state.options;
    }
    DWORD process{};
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    const bool wanted = !forceClear && !state.stopping.load() && options.enabled && now <= state.deadline.load() &&
                        process == GetCurrentProcessId() && (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
    if (!wanted && !state.active.load())
        return;
    LocalMemory local;
    const Memory m{&local, LocalMemory::Read};
    if (!Initialize(m, now))
        return;
    std::uintptr_t engine{}, panel{};
    const auto context = Context(m, engine, panel);
    if (context != HudRead::Present) {
        if (context == HudRead::Gone) {
            state.active = false;
            state.lastPanel = 0;
        }
        return; // A transient read failure retains the cleanup obligation.
    }
    scoreboard::Frame current;
    std::uintptr_t list{}, controller{};
    const bool data = wanted && !state.stopping.load() && m.Read(state.client + offsets::LocalController, controller) &&
                      controller && m.Read(state.client + offsets::EntityList, list) &&
                      ReadScoreboard(m, list, current);
    if (data && state.active && panel == state.lastPanel && current == state.last && options == state.lastOptions &&
        now < state.heartbeat)
        return;
    const auto script = scoreboard::Script(current, options, !data);
    if (Execute(engine, panel, script.c_str())) {
        state.active = data;
        state.lastPanel = panel;
        state.last = current;
        state.lastOptions = options;
        state.heartbeat = now + 1000;
        state.updates.fetch_add(1, std::memory_order_relaxed);
    } else {
        state.failures.fetch_add(1, std::memory_order_relaxed);
        state.active = false;
        state.rejected = true;
        state.verified = false;
        state.connected = false;
    }
}
} // namespace
void StartScoreboard() noexcept {
    state.stopping = false;
    state.deadline = 0;
    state.active = false;
    state.connected = false;
    state.verified = state.rejected = false;
    state.client = state.panorama = state.interface = state.lastPanel = 0;
    state.next = state.retry = state.heartbeat = 0;
    state.run = nullptr;
    state.last = {};
    state.updates = state.failures = 0;
    state.thread = 0;
}
void ConfigureScoreboard(const scoreboard::Options &options, bool fresh) noexcept {
    std::scoped_lock lock(state.optionsMutex);
    if (state.stopping || !fresh || !scoreboard::Valid(options)) {
        state.deadline = 0;
        return;
    }
    state.options = options;
    state.deadline = options.enabled ? GetTickCount64() + 250 : 0;
}
void TickScoreboard(int stage) noexcept {
    TickGuard guard;
    if (stage != static_cast<int>(frame::Stage::PostDataEnd) && !state.stopping.load())
        return;
    state.thread = GetCurrentThreadId();
    try {
        Update(state.stopping.load());
    } catch (...) {
        state.failures.fetch_add(1, std::memory_order_relaxed);
    }
}
HRESULT StopScoreboard() noexcept {
    state.stopping = true;
    state.deadline = 0;
    // Read-only null-HUD proof is safe even after networking stops at map leave.
    const auto client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    if (client) {
        LocalMemory local;
        const Memory m{&local, LocalMemory::Read};
        ScoreboardHud hud;
        if (ReadScoreboardHud(m, client + HudSlot, hud) == HudRead::Gone)
            state.active = false;
    }
    if (GetCurrentThreadId() == state.thread.load())
        TickScoreboard(static_cast<int>(frame::Stage::PostDataEnd));
    const auto until = GetTickCount64() + 1000;
    while (state.active.load(std::memory_order_acquire) || state.inFlight.load(std::memory_order_acquire)) {
        if (GetTickCount64() > until)
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        Sleep(1);
    }
    return S_OK;
}
ScoreboardStatus GetScoreboardStatus() noexcept {
    return {state.connected.load(), state.active.load(), state.updates.load(), state.failures.load()};
}
} // namespace awareness::cs2
