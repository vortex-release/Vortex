#include "world_native.hpp"
#include "world_filter.hpp"
#include "cs2_model_draw.hpp"
#include "local_memory.hpp"
#include "runtime_support.hpp"
#include <MinHook.h>
#include <atomic>
#include <memory>
#include <vector>
namespace awareness::cs2 {
namespace {
namespace abi = trajectory_offsets;
using Draw = void (*)(void *, void *, const model::Packet *, int, void *, void *, void *);
struct Selection {
    combat::Options settings;
    combat::WorldSnapshot world;
};
struct State {
    std::uintptr_t scene{}, particles{};
    Draw particle{};
    bool particleInstalled{};
    std::atomic<unsigned> inFlight{};
    std::atomic<ULONGLONG> deadline{};
    std::atomic<std::shared_ptr<const Selection>> selection;
    std::array<std::shared_ptr<Selection>, 3> pool;
} state;
struct Guard {
    Guard() { ++state.inFlight; }
    ~Guard() { --state.inFlight; }
};
template <std::size_t N>
bool Verify(std::uintptr_t module, std::uintptr_t timestamp, std::uintptr_t size, std::uintptr_t entry,
            const unsigned char (&bytes)[N]) noexcept {
    LocalMemory local;
    Memory m{&local, LocalMemory::Read};
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::array<unsigned char, N> got{};
    return module && m.Read(module, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
           dos.e_lfanew < 4096 && m.Read(module + dos.e_lfanew, nt) && nt.Signature == IMAGE_NT_SIGNATURE &&
           nt.FileHeader.TimeDateStamp == timestamp && nt.OptionalHeader.SizeOfImage == size &&
           m.Read(module + entry, got) && !std::memcmp(got.data(), bytes, N);
}
bool Replace(const Memory &m, const model::Packet &packet, const Selection &s) noexcept {
    const auto collection = packet.Get<std::uintptr_t>(0);
    std::uintptr_t table{};
    if (!m.Read(collection, table) || table != state.particles + abi::CollectionVtable)
        return false;
    char path[260]{};
    const int type = ParticleResourceName(m, collection, path) ? ParticleType(path) : 0;
    if (!type)
        return false;
    if (type == 1 && !s.settings.fireArea || type == 2 && !s.settings.smokeArea || type == 3 && !s.settings.blastArea)
        return false;
    Vector3 low{}, high{};
    if (!m.Field(collection, abi::CollectionBoundsMin, low) || !m.Field(collection, abi::CollectionBoundsMax, high) ||
        !PlausiblePosition(low) || !PlausiblePosition(high))
        return false;
    const auto center = flight::Scale(low + high, .5f);
    for (std::size_t i = 0; i < s.world.areaCount; ++i) {
        const auto &a = s.world.areas[i];
        if (static_cast<int>(a.type) + 1 == type && Distance(a.center, center) < a.radius + a.height + 96)
            return true;
    }
    return false; // Do not hide an effect before its replacement is available.
}
void ParticleHook(void *self, void *ctx, const model::Packet *packets, int count, void *view, void *pass, void *stats) {
    Guard guard;
    const auto original = state.particle;
    const auto forward = [&] { original(self, ctx, packets, count, view, pass, stats); };
    if (!packets || count < 1 || count > 8192 || GetTickCount64() > state.deadline)
        return forward();
    const auto selection = state.selection.load();
    if (!selection || !selection->settings.areas || !selection->settings.hideParticles || !selection->world.areaCount)
        return forward();
    LocalMemory local;
    Memory m{&local, LocalMemory::Read};
    try {
        // Most particle batches fit in fixed scratch storage. Heap storage is
        // only needed for oversized batches, not on every render callback.
        std::array<model::Packet, 128> localPackets;
        std::vector<model::Packet> overflow;
        if (count > static_cast<int>(localPackets.size()))
            overflow.resize(count);
        auto *kept = overflow.empty() ? localPackets.data() : overflow.data();
        std::size_t keptCount{};
        bool removed{}, replace{};
        std::uintptr_t previous{};
        for (int i = 0; i < count; ++i) {
            model::Packet p;
            if (!m.Read(reinterpret_cast<std::uintptr_t>(packets + i), p))
                return forward();
            const auto collection = p.Get<std::uintptr_t>(0);
            if (!previous || collection != previous) {
                previous = collection;
                replace = Replace(m, p, *selection);
            }
            if (replace)
                removed = true;
            else
                kept[keptCount++] = p;
        }
        if (!removed)
            return forward();
        if (keptCount)
            original(self, ctx, kept, static_cast<int>(keptCount), view, pass, stats);
    } catch (...) {
        forward();
    }
}
bool Install(void *at, void *hook, Draw &original) noexcept {
    if (MH_CreateHook(at, hook, reinterpret_cast<void **>(&original)) != MH_OK)
        return false;
    if (MH_EnableHook(at) == MH_OK)
        return true;
    MH_RemoveHook(at);
    return false;
}
} // namespace
HRESULT StartWorldEffects() noexcept {
    if (state.particleInstalled)
        return S_OK;
    state.scene = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"scenesystem.dll"));
    state.particles = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"particles.dll"));
    if (Verify(state.particles, abi::ParticleTimestamp, abi::ParticleImageSize, abi::ParticleDraw,
               abi::ParticleDrawBytes))
        state.particleInstalled = Install(reinterpret_cast<void *>(state.particles + abi::ParticleDraw),
                                          reinterpret_cast<void *>(ParticleHook), state.particle);
    OverlayLog(state.particleInstalled ? "Particle replacement connected."
                                       : "Particle replacement layout unavailable.");
    return state.particleInstalled ? S_OK : S_FALSE;
}
void ConfigureWorldEffects(const combat::Options &s, const combat::WorldSnapshot &world, bool ready) noexcept {
    if (!ready || !(s.areas && s.hideParticles)) {
        PauseWorldEffects();
        return;
    }
    try {
        std::shared_ptr<Selection> next;
        for (auto &entry : state.pool) {
            if (!entry)
                entry = std::make_shared<Selection>();
            if (entry.use_count() == 1) {
                next = entry;
                break;
            }
        }
        if (!next)
            return;
        next->settings = s;
        next->world = world;
        state.selection = std::move(next);
        state.deadline = GetTickCount64() + 250;
    } catch (...) {
        PauseWorldEffects();
    }
}
void PauseWorldEffects() noexcept {
    state.deadline = 0;
}
HRESULT StopWorldEffects() noexcept {
    PauseWorldEffects();
    const auto stop = [](std::uintptr_t at, bool installed) {
        if (!installed)
            return true;
        auto r = MH_DisableHook(reinterpret_cast<void *>(at));
        return r == MH_OK || r == MH_ERROR_DISABLED;
    };
    if (!stop(state.particles + abi::ParticleDraw, state.particleInstalled))
        return E_FAIL;
    const auto until = GetTickCount64() + 5000;
    while (state.inFlight) {
        if (GetTickCount64() > until)
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        Sleep(1);
    }
    if (state.particleInstalled &&
        MH_RemoveHook(reinterpret_cast<void *>(state.particles + abi::ParticleDraw)) != MH_OK)
        return E_FAIL;
    state.particleInstalled = false;
    state.selection = nullptr;
    for (auto &entry : state.pool)
        entry.reset();
    return S_OK;
}
} // namespace awareness::cs2
