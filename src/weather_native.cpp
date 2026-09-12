#include "weather_native.hpp"
#include "weather_state.hpp"
#include "assist_reader.hpp"
#include "build_verification.hpp"
#include "game_frame.hpp"
#include "runtime_support.hpp"
#include <Psapi.h>
#include <atomic>
#include <mutex>

namespace awareness::cs2 {
namespace {
// Source identification: Anthony's MIT weather module and Jeremy's weather port.
// All addresses, structures and queue semantics below were checked against the
// installed 14181 images; see docs/weather-native.md. No custom assets are installed.
constexpr std::uintptr_t ManagerSlot = 0x20A9A78, ManagerVtable = 0x1AEF5D8;
constexpr std::uintptr_t ResourceSlot = 0x25F11A0, ResourceVtable = 0x5FA88;
constexpr std::uintptr_t CreateRva = 0x7A75E0, DestroyRva = 0x9A2DA0, CpRva = 0x9DE340;
constexpr std::uintptr_t PathRva = 0x184EBE0, QueueRva = 0x1A790, ReleaseRva = 0x1A920, StatusRva = 0x170B0;
constexpr std::uint32_t ResourceTimestamp = 0x6AA1ADB5, ResourceImageSize = 0x8E000;
constexpr std::uint32_t MaxDescriptors = 4096;
struct Entry {
    std::uintptr_t rva;
    std::array<unsigned char, 24> bytes;
};
constexpr Entry ClientEntries[]{{CreateRva, {0x4c, 0x8b, 0xdc, 0x53, 0x48, 0x81, 0xec, 0x90, 0,    0,    0,    0xf2,
                                             0x0f, 0x10, 0x05, 0xed, 0x41, 0x1e, 0x01, 0x48, 0x8b, 0xda, 0x48, 0x8b}},
                                {DestroyRva, {0x83, 0xfa, 0xff, 0x0f, 0x84, 0xd5, 0x01, 0,    0,    0x41, 0x54, 0x41,
                                              0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x89, 0x5c, 0x24, 0x60}},
                                {CpRva, {0x48, 0x83, 0xec, 0x58, 0xf3, 0x41, 0x0f, 0x10, 0x51, 0x04, 0xf3, 0x41,
                                         0x0f, 0x10, 0x09, 0xf3, 0x41, 0x0f, 0x10, 0x59, 0x08, 0x4c, 0x8d, 0x4c}},
                                {0x9A15B0, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89,
                                            0x7c, 0x24, 0x18, 0x41, 0x56, 0x48, 0x81, 0xec, 0x70, 0x01, 0,    0}},
                                {PathRva, {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x57,
                                           0x41, 0x56, 0x48, 0x83, 0xec, 0x30, 0x8b, 0x41, 0x04, 0x48, 0x8d, 0x79}}};
constexpr Entry ResourceEntries[]{
    {QueueRva, {0x41, 0xb0, 0x01, 0xe9, 0x08, 0,    0,    0,    0xcc, 0xcc, 0xcc, 0xcc,
                0xcc, 0xcc, 0xcc, 0xcc, 0x40, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec}},
    {ReleaseRva, {0x48, 0x85, 0xd2, 0x0f, 0x84, 0xaa, 0x01, 0,    0,    0x53, 0x41, 0x56,
                  0x48, 0x83, 0xec, 0x28, 0x48, 0x89, 0x74, 0x24, 0x50, 0x4c, 0x8b, 0xf2}},
    {StatusRva, {0x48, 0x8b, 0x01, 0x48, 0x8b, 0x92, 0xd0, 0,    0,    0,    0x48, 0xff,
                 0xa0, 0x90, 0x01, 0,    0,    0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc}}};
struct ResourcePath {
    std::uint32_t length{}, allocation{0xC00000C8};
    char data[0xC8]{};
    std::uint64_t id{}, hash{};
};
static_assert(sizeof(ResourcePath) == 0xE0 && offsetof(ResourcePath, id) == 0xD0);
struct ManifestRequest {
    std::int32_t count{1};
    std::uint32_t padding{};
    const char *const *paths{};
    std::uint8_t synchronous{}, keepResident{1};
    std::array<std::uint8_t, 6> padding2{};
    const char *reason{"Vortex weather"};
    std::uint8_t priority{1};
    std::array<std::uint8_t, 7> padding3{};
    void *callback{}, *context{};
};
static_assert(sizeof(ManifestRequest) == 0x38 && offsetof(ManifestRequest, synchronous) == 0x10 &&
              offsetof(ManifestRequest, priority) == 0x20);
using CreateFn = unsigned *(*)(void *, unsigned *, const char *, int, void *, void *, void *, int);
using DestroyFn = void (*)(void *, unsigned, unsigned char, unsigned char);
using CpFn = char (*)(void *, unsigned, unsigned, const Vector3 *, float);
using PathFn = bool (*)(ResourcePath *, const char *, std::uint64_t);
using QueueFn = void *(*)(void *, const ManifestRequest *);
using ReleaseFn = void (*)(void *, void *);
using StatusFn = int (*)(void *, const ResourcePath *);
using PurgeFn = void (*)(void *, int);
struct Asset {
    ResourcePath path;
    std::uintptr_t resource{}, ticket{};
    double requested{}, retry{};
    unsigned failures{};
    bool initialized{};
};
struct State {
    std::mutex lifecycle, frameMutex, optionsMutex;
    std::uintptr_t client{}, resources{}, manager{}, world{}, globals{};
    std::uint32_t counter{};
    float gameTime{};
    std::uint64_t epoch{1};
    CreateFn create{};
    DestroyFn destroy{};
    CpFn cp{};
    PathFn path{};
    QueueFn queue{};
    ReleaseFn release{};
    StatusFn status{};
    PurgeFn purge{};
    std::array<Asset, 3> assets;
    weather::Controller controller;
    weather::Options options;
    std::atomic<bool> started{}, stopping{}, quiescent{true};
    std::atomic<ULONGLONG> deadline{};
    std::atomic<unsigned> inFlight{};
    std::atomic<weather::Status> reportStatus{weather::Status::Disabled};
    std::atomic<std::uint32_t> active{}, created{}, retired{}, failures{};
} state;
template <std::size_t N>
bool VerifyImage(const Memory &m, std::uintptr_t module, std::uint32_t stamp, std::uint32_t size,
                 const Entry (&entries)[N]) noexcept {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    MODULEINFO info{};
    if (!module || !GetModuleInformation(GetCurrentProcess(), reinterpret_cast<HMODULE>(module), &info, sizeof(info)) ||
        info.SizeOfImage != size || !m.Read(module, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        dos.e_lfanew > 4096 || !m.Read(module + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != stamp || nt.OptionalHeader.SizeOfImage != size)
        return false;
    for (const auto &entry : entries) {
        std::array<unsigned char, 24> got{};
        if (entry.rva + got.size() > size || !m.Read(module + entry.rva, got) || got != entry.bytes)
            return false;
    }
    return true;
}
bool BuildPath(ResourcePath &path, const char *name) noexcept {
    __try {
        return state.path(&path, name, 0x66637076);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
int ResourceStatus(std::uintptr_t resource, const ResourcePath &path) noexcept {
    __try {
        return state.status(reinterpret_cast<void *>(resource), &path);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
std::uintptr_t Queue(std::uintptr_t resource, const char *path) noexcept {
    ManifestRequest request;
    request.paths = &path;
    __try {
        return reinterpret_cast<std::uintptr_t>(state.queue(reinterpret_cast<void *>(resource), &request));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
bool Release(std::uintptr_t resource, std::uintptr_t ticket) noexcept {
    __try {
        state.release(reinterpret_cast<void *>(resource), reinterpret_cast<void *>(ticket));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void Purge(ResourcePath &path) noexcept {
    __try {
        state.purge(&path, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    path = {};
}
bool SetCp(std::uintptr_t manager, std::uint32_t index, Vector3 point) noexcept {
    __try {
        return state.cp(reinterpret_cast<void *>(manager), index, 0, &point, 0) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool Destroy(std::uintptr_t manager, std::uint32_t index) noexcept {
    __try {
        state.destroy(reinterpret_cast<void *>(manager), index, 1, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool Create(std::uintptr_t manager, const char *path, std::uint32_t &index) noexcept {
    __try {
        state.create(reinterpret_cast<void *>(manager), &index, path, 8, nullptr, nullptr, nullptr, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool ResourceInterface(const Memory &m, std::uintptr_t &resource) noexcept {
    std::uintptr_t table{}, queue{}, release{}, status{};
    return m.Field(state.client, ResourceSlot, resource) && resource && m.Read(resource, table) &&
           table == state.resources + ResourceVtable && m.Field(table, 22 * sizeof(void *), queue) &&
           queue == state.resources + QueueRva && m.Field(table, 25 * sizeof(void *), release) &&
           release == state.resources + ReleaseRva && m.Field(table, 51 * sizeof(void *), status) &&
           status == state.resources + StatusRva;
}
struct Backend {
    LocalMemory local;
    Memory memory{&local, LocalMemory::Read};
    std::uintptr_t manager{}, resource{};
    bool managerReadable{};
    weather::Context ReadContext() noexcept {
        weather::Context context;
        context.epoch = state.epoch;
        std::uintptr_t table{}, world{}, global{};
        std::uint32_t counter{};
        float time{};
        managerReadable = memory.Field(state.client, ManagerSlot, manager);
        if (!managerReadable)
            return context;
        const bool managerOk = manager && memory.Read(manager, table) && table == state.client + ManagerVtable &&
                               memory.Field(manager, 0xE0, counter) && counter < 0x7FFFFFFFu;
        const bool worldOk = memory.Field(state.client, offsets::EntityList, world) && world &&
                             memory.Field(state.client, offsets::GlobalVars, global) && global &&
                             memory.Field(global, trajectory_offsets::GlobalCurrentTime, time) && std::isfinite(time) &&
                             time >= 0;
        if (manager != state.manager || (managerOk && counter < state.counter) ||
            (worldOk && (world != state.world || global != state.globals || time + .1f < state.gameTime))) {
            ++state.epoch;
            context.epoch = state.epoch;
            state.manager = manager;
        }
        if (managerOk)
            state.counter = counter;
        if (worldOk) {
            state.world = world;
            state.globals = global;
            state.gameTime = time;
        }
        context.managerValid = managerOk;
        if (!managerOk || !worldOk)
            return context;
        // Derive the current camera, including spectator cameras. The connected
        // controller is identity-checked; no cached pawn or entity index is reused.
        std::uintptr_t controller{}, after{};
        std::uint32_t owner{}, checked{};
        Matrix4x4 matrix{};
        context.originValid =
            memory.Field(state.client, offsets::LocalController, controller) && FullHandle(memory, controller, owner) &&
            EntityAt(memory, world, owner) == controller && memory.Field(state.client, offsets::ViewMatrix, matrix) &&
            CameraFromMatrix(matrix, context.origin) && memory.Field(state.client, offsets::LocalController, after) &&
            after == controller && FullHandle(memory, controller, checked) && checked == owner;
        return context;
    }
    weather::Ownership Owns(weather::Token &token) noexcept {
        if (token.epoch != state.epoch || !managerReadable || !manager || manager != state.manager)
            return managerReadable ? weather::Ownership::Gone : weather::Ownership::Unreadable;
        std::uintptr_t current{}, table{}, entries{};
        std::int32_t count{};
        std::uint32_t counter{};
        if (!memory.Field(state.client, ManagerSlot, current) || current != manager || !memory.Read(manager, table) ||
            table != state.client + ManagerVtable || !memory.Field(manager, 0xE0, counter) || counter <= token.index ||
            counter < state.counter || !memory.Field(manager, 0x98, count) || count < 0 || count > MaxDescriptors ||
            !memory.Field(manager, 0xA0, entries))
            return weather::Ownership::Unreadable;
        if (!count)
            return weather::Ownership::Gone;
        if (!entries)
            return weather::Ownership::Unreadable;
        const auto match = [&](std::uint32_t i) {
            std::uintptr_t descriptor{};
            std::uint32_t index{};
            if (!memory.Field(entries, i * sizeof(void *), descriptor) || !descriptor ||
                !memory.Field(descriptor, 0x38, index))
                return -1;
            if (index != token.index)
                return 0;
            if (token.descriptor && descriptor != token.descriptor)
                return 0;
            token.descriptor = descriptor;
            token.hint = i;
            return 1;
        };
        if (token.hint < static_cast<std::uint32_t>(count) && match(token.hint) == 1)
            return weather::Ownership::Owned;
        // Reordering follows deletion; the cached entry normally avoids a scan.
        // Scan only a bounded native vector, never an entire entity list.
        bool unreadable{};
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(count); ++i) {
            const int result = match(i);
            if (result == 1)
                return weather::Ownership::Owned;
            unreadable |= result < 0;
        }
        return unreadable ? weather::Ownership::Unreadable : weather::Ownership::Gone;
    }
    weather::Residency Asset(std::uint32_t kind, double now) noexcept {
        if (kind >= state.assets.size() || !ResourceInterface(memory, resource))
            return weather::Residency::Failed;
        auto &asset = state.assets[kind];
        if (asset.resource && asset.resource != resource) {
            // A replaced ResourceSystem owns any old tickets. Never release one
            // through the new interface; discard only our bookkeeping.
            asset.ticket = 0;
            asset.retry = 0;
            asset.failures = 0;
        }
        asset.resource = resource;
        if (!asset.initialized) {
            if (!BuildPath(asset.path, weather::Paths[kind])) {
                Purge(asset.path);
                return weather::Residency::Failed;
            }
            asset.initialized = true;
            state.quiescent = false;
        }
        const auto status = ResourceStatus(resource, asset.path);
        if (status == 3 && asset.ticket)
            return weather::Residency::Ready;
        if (asset.ticket && now - asset.requested < 15)
            return weather::Residency::Pending;
        if (asset.ticket) {
            if (!Release(resource, asset.ticket))
                return weather::Residency::Failed;
            asset.ticket = 0;
            asset.retry = now + (std::min)(30., 2. * static_cast<double>(++asset.failures));
        }
        if (now < asset.retry || status < 0)
            return weather::Residency::Failed;
        // This is the verified asynchronous manifest queue, NOT BlockingLoad.
        // It copies the request's stack path array into engine-owned storage.
        asset.ticket = Queue(resource, weather::Paths[kind]);
        asset.requested = now;
        if (!asset.ticket) {
            asset.retry = now + (std::min)(30., 2. * static_cast<double>(++asset.failures));
            return weather::Residency::Failed;
        }
        state.quiescent = false;
        return weather::Residency::Pending;
    }
    bool Create(std::uint32_t kind, Vector3 point, weather::Token &token) noexcept {
        if (!resource || kind >= state.assets.size() || ResourceStatus(resource, state.assets[kind].path) != 3)
            return false;
        std::uint32_t index{weather::InvalidIndex};
        if (!cs2::Create(manager, weather::Paths[kind], index) || index == weather::InvalidIndex)
            return false;
        // The native monotonically allocated ID belongs to this transaction.
        // Capture the descriptor before publishing a token to the controller.
        token = {state.epoch, 0, index, 0};
        local.Reset();
        const auto ownership = Owns(token);
        if (ownership == weather::Ownership::Gone) {
            token = {};
            return false;
        }
        // Retain a newly allocated ID until its descriptor can be read; never
        // orphan an effect and allocate a replacement after a transient read failure.
        token.placed = ownership == weather::Ownership::Owned && SetCp(manager, index, point);
        state.quiescent = false;
        return true;
    }
    bool Move(weather::Token &token, Vector3 point) noexcept {
        token.placed = Owns(token) == weather::Ownership::Owned && SetCp(manager, token.index, point);
        return token.placed;
    }
    bool Destroy(weather::Token &token) noexcept {
        if (Owns(token) != weather::Ownership::Owned)
            return false;
        const bool result = cs2::Destroy(manager, token.index);
        local.Reset();
        return result && Owns(token) == weather::Ownership::Gone;
    }
    bool ReleaseAssets() noexcept {
        bool complete = true;
        std::uintptr_t current{};
        const bool interfaceValid = ResourceInterface(memory, current);
        for (auto &asset : state.assets) {
            if (asset.ticket) {
                if (!interfaceValid) {
                    complete = false;
                    continue;
                }
                if (current == asset.resource && !Release(current, asset.ticket)) {
                    complete = false;
                    continue;
                }
                asset.ticket = 0;
            }
            if (asset.initialized)
                Purge(asset.path);
            asset = {};
        }
        return complete;
    }
};
void Publish(weather::Diagnostics diagnostics) noexcept {
    state.active = diagnostics.active;
    state.created = diagnostics.created;
    state.retired = diagnostics.retired;
    state.failures = diagnostics.failures;
    state.reportStatus = diagnostics.status;
}
} // namespace
HRESULT StartWeather() noexcept {
    std::scoped_lock lifecycle(state.lifecycle);
    if (state.started)
        return state.stopping ? HRESULT_FROM_WIN32(ERROR_BUSY) : S_OK;
    std::scoped_lock frameLock(state.frameMutex);
    LocalMemory local;
    const Memory m{&local, LocalMemory::Read};
    state.client = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll"));
    state.resources = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"resourcesystem.dll"));
    const auto engine = GetModuleHandleW(L"engine2.dll");
    MODULEINFO engineInfo{};
    const auto tier = GetModuleHandleW(L"tier0.dll");
    state.purge = tier ? reinterpret_cast<PurgeFn>(GetProcAddress(tier, "?Purge@CBufferString@@QEAAXH@Z")) : nullptr;
    const bool engineReadable =
        engine && GetModuleInformation(GetCurrentProcess(), engine, &engineInfo, sizeof(engineInfo));
    const auto build = engineReadable
                           ? VerifyEngineBuild(m, reinterpret_cast<std::uintptr_t>(engine), engineInfo.SizeOfImage)
                           : BuildEvidence{};
    if (build.check != BuildCheck::Verified || build.build != offsets::ExpectedBuild ||
        !VerifyImage(m, state.client, trajectory_offsets::ClientTimestamp, trajectory_offsets::ClientImageSize,
                     ClientEntries) ||
        !VerifyImage(m, state.resources, ResourceTimestamp, ResourceImageSize, ResourceEntries) || !state.purge) {
        state.reportStatus = weather::Status::Unsupported;
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    }
    state.create = reinterpret_cast<CreateFn>(state.client + CreateRva);
    state.destroy = reinterpret_cast<DestroyFn>(state.client + DestroyRva);
    state.cp = reinterpret_cast<CpFn>(state.client + CpRva);
    state.path = reinterpret_cast<PathFn>(state.client + PathRva);
    state.queue = reinterpret_cast<QueueFn>(state.resources + QueueRva);
    state.release = reinterpret_cast<ReleaseFn>(state.resources + ReleaseRva);
    state.status = reinterpret_cast<StatusFn>(state.resources + StatusRva);
    state.manager = state.world = state.globals = 0;
    state.counter = 0;
    state.gameTime = 0;
    ++state.epoch;
    state.controller = {};
    state.assets = {};
    state.deadline = 0;
    state.stopping = false;
    state.quiescent = true;
    Publish({});
    state.started.store(true, std::memory_order_release);
    return S_OK;
}
void ConfigureWeather(const weather::Options &options, bool fresh) noexcept {
    std::scoped_lock lock(state.optionsMutex);
    state.options = weather::Valid(options) ? options : weather::Options{};
    state.deadline = fresh && weather::Valid(options) && options.enabled ? GetTickCount64() + 500 : 0;
}
void TickWeather(int stage) noexcept {
    if (!state.started.load(std::memory_order_acquire))
        return;
    const bool networkEnd = stage == static_cast<int>(frame::Stage::NetworkEnd);
    const auto nowMs = GetTickCount64();
    const bool cleanup = state.stopping.load() || nowMs > state.deadline.load();
    if (!weather::NeedsTick(networkEnd, cleanup, state.quiescent.load())) {
        if (cleanup)
            state.reportStatus = weather::Status::Disabled;
        return;
    }
    struct Guard {
        Guard() { ++state.inFlight; }
        ~Guard() { --state.inFlight; }
    } guard;
    if (!state.started.load(std::memory_order_acquire))
        return;
    std::unique_lock frameLock(state.frameMutex, std::try_to_lock);
    if (!frameLock.owns_lock())
        return;
    weather::Options options;
    if (networkEnd && !state.stopping && nowMs <= state.deadline) {
        std::unique_lock lock(state.optionsMutex, std::try_to_lock);
        if (!lock.owns_lock())
            return;
        options = state.options;
    }
    Backend backend;
    const auto context = backend.ReadContext();
    state.controller.Update(context, options, nowMs * .001, backend);
    auto report = state.controller.Report();
    if (!options.enabled && state.controller.Empty()) {
        const bool clean = backend.ReleaseAssets();
        state.quiescent.store(clean, std::memory_order_release);
        if (!clean)
            report.status = weather::Status::Stopping;
    }
    Publish(report);
}
HRESULT StopWeather() noexcept {
    std::scoped_lock lifecycle(state.lifecycle);
    if (!state.started)
        return S_OK;
    state.stopping = true;
    state.deadline = 0;
    const auto until = GetTickCount64() + 1500;
    while (!state.quiescent.load(std::memory_order_acquire) || state.inFlight.load(std::memory_order_acquire)) {
        if (GetTickCount64() >= until)
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        Sleep(1);
    }
    std::scoped_lock frameLock(state.frameMutex);
    if (!state.quiescent || !state.controller.Empty())
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    state.started.store(false, std::memory_order_release);
    state.reportStatus = weather::Status::Disabled;
    return S_OK;
}
weather::Diagnostics ReadWeatherDiagnostics() noexcept {
    return {state.reportStatus.load(), state.active.load(), state.created.load(), state.retired.load(),
            state.failures.load()};
}
} // namespace awareness::cs2
