#include "cs2_glow.hpp"
#include "cs2_source.hpp"
#include <cstdio>
#include <cstring>
using namespace awareness;
using namespace awareness::cs2;
struct Fixture {
    LocalMemory memory;
    unsigned char *base =
        static_cast<unsigned char *>(VirtualAlloc(nullptr, 0x40000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    int calls{}, failAt{};
    template <class T> void Put(std::uintptr_t at, T value) {
        std::memcpy(reinterpret_cast<void *>(at), &value, sizeof(value));
    }
    template <class T> T Get(std::uintptr_t at) {
        T value{};
        std::memcpy(&value, reinterpret_cast<void *>(at), sizeof(value));
        return value;
    }
    std::uintptr_t List() const { return reinterpret_cast<std::uintptr_t>(base) + 0x1000; }
    std::uintptr_t Chunk() const { return reinterpret_cast<std::uintptr_t>(base) + 0x2000; }
    std::uintptr_t Pawn(unsigned i) const { return reinterpret_cast<std::uintptr_t>(base) + 0x10000 + i * 0x3000; }
    static bool Read(void *ctx, std::uintptr_t at, void *out, std::size_t size) noexcept {
        return LocalMemory::Read(&static_cast<Fixture *>(ctx)->memory, at, out, size);
    }
    static ExchangeResult Exchange(void *ctx, std::uintptr_t at, std::uint32_t before, std::uint32_t after,
                                   std::uint8_t size) noexcept {
        auto &f = *static_cast<Fixture *>(ctx);
        if (++f.calls == f.failAt)
            return ExchangeResult::Unavailable;
        return ExchangeGlowField(nullptr, at, before, after, size);
    }
    GlowAccess Access() { return {{this, Read}, Exchange}; }
    Fixture() {
        if (!base)
            throw 1;
        Put(List() + offsets::EntityTable, Chunk());
        for (unsigned i = 0; i < 3; ++i) {
            auto identity = Chunk() + (i + 2) * offsets::EntityStride;
            Put(identity, Pawn(i));
            Put(identity + 0x10, std::uint32_t(0x8000 + i + 2));
            Put(Pawn(i) + offsets::Identity, identity);
            Put(Pawn(i) + offsets::Health, std::int32_t(100));
            Put(Pawn(i) + offsets::Team, std::uint8_t(3));
            Put(Pawn(i) + offsets::Glow + offsets::GlowType, std::uint32_t(1));
            Put(Pawn(i) + offsets::Glow + offsets::GlowOverride, std::uint32_t(0x12345678));
        }
    }
    ~Fixture() { VirtualFree(base, 0, MEM_RELEASE); }
};
int main() {
    int failed{}, checks{};
    const auto check = [&](bool ok, const char *why) {
        ++checks;
        if (!ok) {
            ++failed;
            std::printf("FAIL: %s\n", why);
        }
    };
    Fixture f;
    ModelHighlight highlight;
    FrameSnapshot frame;
    frame.entityCount = 3;
    frame.localTeam = 2;
    for (unsigned i = 0; i < 3; ++i) {
        auto &e = frame.entities[i];
        e.valid = 1;
        e.id = i + 2;
        e.team = 3;
        e.health = 100;
        e.origin = {5.f + static_cast<float>(i), 0, 0};
    }
    Configuration config;
    config.opacity = 1;
    config.teamFilter = TeamFilter::OpponentsOnly;
    EffectsConfiguration effects;
    effects.materialEnabled = effects.glowEnabled = 1;
    effects.materialColor = {1, .5f, .25f, .8f};
    std::array<unsigned char, 0x58> original{};
    std::memcpy(original.data(), reinterpret_cast<void *>(f.Pawn(0) + offsets::Glow), original.size());
    auto update = highlight.Update(f.Access(), f.List(), frame, config, effects, true);
    check(update.applied == 3 && !update.failed, "eligible player model components enabled");
    check(f.Get<std::uint32_t>(f.Pawn(0) + offsets::Glow + offsets::GlowOverride) == 0xCC4080FF,
          "RGBA opacity and channel packing");
    check(f.Get<std::uint32_t>(f.Pawn(0) + offsets::Glow + offsets::GlowType) == 3 &&
              f.Get<std::uint8_t>(f.Pawn(0) + offsets::Glow + offsets::GlowEnabled) == 1,
          "native model mode and activation");
    const int calls = f.calls;
    highlight.Update(f.Access(), f.List(), frame, config, effects, true);
    check(f.calls == calls, "unchanged engine fields are not rewritten each Present");
    effects.materialColor = {.2f, .3f, .4f, .5f};
    highlight.Update(f.Access(), f.List(), frame, config, effects, true);
    check(f.Get<std::uint32_t>(f.Pawn(0) + offsets::Glow + offsets::GlowOverride) == 0x80664D33, "live color update");
    config.enabled = 0;
    update = highlight.Update(f.Access(), f.List(), frame, config, effects, true);
    check(update.applied == 0 && update.restored &&
              !std::memcmp(original.data(), reinterpret_cast<void *>(f.Pawn(0) + offsets::Glow), original.size()),
          "master disable restores original component bytes");
    config.enabled = 1;
    frame.entities[1].dormant = 1;
    frame.entities[2].health = 0;
    update = highlight.Update(f.Access(), f.List(), frame, config, effects, true);
    check(update.applied == 1, "dead/dormant entities skipped");
    frame.localTeam = 3;
    update = highlight.Update(f.Access(), f.List(), frame, config, effects, true);
    check(update.applied == 0 && f.Get<std::uint8_t>(f.Pawn(0) + offsets::Glow + offsets::GlowEnabled) == 0,
          "team filter restores previous target");
    frame.localTeam = 2;
    frame.localEntityId = 2;
    update = highlight.Update(f.Access(), f.List(), frame, config, effects, true);
    check(!update.applied, "local pawn skipped");
    frame.localEntityId = 99;
    frame.entities[0].origin = {1000, 0, 0};
    update = highlight.Update(f.Access(), f.List(), frame, config, effects, true);
    check(!update.applied, "out of range skipped");
    frame.entities[0].origin = {5, 0, 0};
    highlight.Update(f.Access(), f.List(), frame, config, effects, true);
    f.Put(f.Pawn(0) + offsets::Glow + offsets::GlowType, std::uint32_t(2));
    check(highlight.Clear(f.Access()) && f.Get<std::uint32_t>(f.Pawn(0) + offsets::Glow + offsets::GlowType) == 2,
          "restoration preserves intervening engine mode changes");
    f.Put(f.Pawn(0) + offsets::Glow + offsets::GlowType, std::uint32_t(1));
    f.failAt = f.calls + 5;
    update = highlight.Update(f.Access(), f.List(), frame, config, effects, true);
    check(update.failed == 1 &&
              !std::memcmp(original.data(), reinterpret_cast<void *>(f.Pawn(0) + offsets::Glow), original.size()),
          "partial write failure rolls back all owned fields");
    f.failAt = 0;
    highlight.Update(f.Access(), f.List(), frame, config, effects, true);
    update = highlight.Update(f.Access(), f.List(), frame, config, effects, false);
    check(!update.applied && update.restored &&
              !std::memcmp(original.data(), reinterpret_cast<void *>(f.Pawn(0) + offsets::Glow), original.size()),
          "lost ready frame restores native highlight");
    highlight.Update(f.Access(), f.List(), frame, config, effects, true);
    f.Put(f.Chunk() + 2 * offsets::EntityStride + 0x10, std::uint32_t(0x10002));
    const int beforeReuse = f.calls;
    check(highlight.Clear(f.Access()) && f.calls == beforeReuse,
          "recycled full handle is never written during restore");
    DWORD previous{};
    const auto field = f.Pawn(1) + offsets::Glow + offsets::GlowOverride;
    VirtualProtect(reinterpret_cast<void *>(f.Pawn(1)), 0x2000, PAGE_READONLY, &previous);
    check(ExchangeGlowField(nullptr, field, 0x12345678, 1, 4) == ExchangeResult::Unavailable,
          "read-only field rejected");
    VirtualProtect(reinterpret_cast<void *>(f.Pawn(1)), 0x2000, PAGE_READWRITE | PAGE_GUARD, &previous);
    check(ExchangeGlowField(nullptr, field, 0x12345678, 1, 4) == ExchangeResult::Unavailable, "guarded field rejected");
    MEMORY_BASIC_INFORMATION info{};
    VirtualQuery(reinterpret_cast<void *>(field), &info, sizeof(info));
    check((info.Protect & PAGE_GUARD) != 0, "guard flag preserved");
    VirtualProtect(reinterpret_cast<void *>(f.Pawn(1)), 0x2000, PAGE_READWRITE, &previous);
    check(ExchangeGlowField(nullptr, field + 1, 0, 1, 4) == ExchangeResult::Unavailable, "unaligned field rejected");
    check(ExchangeGlowField(nullptr, field, 77, 1, 4) == ExchangeResult::Conflict,
          "compare exchange preserves concurrent value");
    Source source;
    EffectsState status;
    source.UpdateHighlight(frame, config, effects, true, status);
    check(status.status == EffectsStatus::NoGeometry && source.ClearHighlight(),
          "unverified source cannot enable native models");
    std::printf("CS2 native model highlight: %d checks, %d failures\n", checks, failed);
    return failed ? 1 : 0;
}
