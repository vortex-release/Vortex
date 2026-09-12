#include "sky_tint.hpp"
#include "player_decoration.hpp"
#include "presets.hpp"
#include <vector>
#include <cstdio>
using namespace awareness;
using namespace awareness::cs2;
struct Fixture {
    static constexpr std::uintptr_t base = 0x10000000, list = base + 0x1000, chunk = base + 0x10000,
                                    pawn = base + 0x40000, name = base + 0x80000;
    std::vector<unsigned char> bytes = std::vector<unsigned char>(0x100000);
    unsigned writes{}, failAt{}, reads{};
    static bool Read(void *self, std::uintptr_t at, void *out, std::size_t n) noexcept {
        auto &f = *static_cast<Fixture *>(self);
        ++f.reads;
        if (at < base || at - base > f.bytes.size() || n > f.bytes.size() - (at - base))
            return false;
        std::memcpy(out, f.bytes.data() + at - base, n);
        return true;
    }
    static ExchangeResult Exchange(void *self, std::uintptr_t at, std::uint32_t before, std::uint32_t after,
                                   std::uint8_t n) noexcept {
        auto &f = *static_cast<Fixture *>(self);
        if (++f.writes == f.failAt)
            return ExchangeResult::Unavailable;
        std::uint32_t value{};
        if (!Read(self, at, &value, n))
            return ExchangeResult::Unavailable;
        if (value != before)
            return ExchangeResult::Conflict;
        std::memcpy(f.bytes.data() + at - base, &after, n);
        return ExchangeResult::Applied;
    }
    template <class T> void Put(std::uintptr_t at, T v) { std::memcpy(bytes.data() + at - base, &v, sizeof(v)); }
    template <class T> T Get(std::uintptr_t at) {
        T v{};
        Read(this, at, &v, sizeof(v));
        return v;
    }
    GlowAccess Access() { return {{this, Read}, Exchange}; }
    Fixture() {
        Put(list + offsets::EntityTable, chunk);
        Put(list + offsets::HighestEntity, 4);
        Put(chunk + 4 * offsets::EntityStride, pawn);
        Put(pawn + offsets::Identity, chunk + 4 * offsets::EntityStride);
        Put(chunk + 4 * offsets::EntityStride + 0x10, 0x80004u);
        Put(chunk + 4 * offsets::EntityStride + offsets::DesignerName, name);
        std::array<char, 16> type{};
        std::memcpy(type.data(), "env_sky", 8);
        Put(name, type);
        Put(pawn + offsets::SkyTint, std::array<unsigned char, 4>{80, 160, 240, 255});
        Put(pawn + offsets::SkyTint + 4, 0x12345678u);
        Put(pawn + offsets::SkyBrightness, .8f);
    }
};
int main() {
    unsigned failures{}, checks{};
    const auto check = [&](bool ok, const char *label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("FAIL: %s\n", label);
        }
    };
    styling::Sky sky{1, 0, {0, 0, 0, 1}};
    Fixture f;
    SkyTint tint;
    check(tint.Update(f.Access(), Fixture::list, sky, 1, true).applied == 1 &&
              f.Get<unsigned char>(Fixture::pawn + offsets::SkyTint) == 0 &&
              f.Get<float>(Fixture::pawn + offsets::SkyBrightness) == 0,
          "midnight changes only the discovered sky");
    const auto writes = f.writes;
    tint.Update(f.Access(), Fixture::list, sky, 1.1, true);
    check(writes == f.writes, "unchanged sky fields skip writes");
    check(f.Get<unsigned char>(Fixture::pawn + offsets::SkyTint + 3) == 255 &&
              f.Get<unsigned>(Fixture::pawn + offsets::SkyTint + 4) == 0x12345678u,
          "alpha and lighting-only tint remain untouched");
    sky = {1, 2, {.5f, .25f, 1, 1}};
    tint.Update(f.Access(), Fixture::list, sky, 1.2, true);
    check(f.Get<unsigned char>(Fixture::pawn + offsets::SkyTint) == 40 &&
              f.Get<unsigned char>(Fixture::pawn + offsets::SkyTint + 1) == 40 &&
              std::abs(f.Get<float>(Fixture::pawn + offsets::SkyBrightness) - 1.6f) < .001f,
          "changed sky uses original channels rather than accumulating multipliers");
    f.Put(Fixture::pawn + offsets::SkyTint, std::uint8_t{201});
    check(tint.Clear(f.Access()) && f.Get<unsigned char>(Fixture::pawn + offsets::SkyTint) == 201 &&
              f.Get<unsigned char>(Fixture::pawn + offsets::SkyTint + 1) == 160 &&
              f.Get<float>(Fixture::pawn + offsets::SkyBrightness) == .8f,
          "clear restores owned fields and preserves engine updates");
    Fixture failure;
    SkyTint rollback;
    failure.failAt = 2;
    check(rollback.Update(failure.Access(), Fixture::list, sky, 1, true).failed == 1 &&
              failure.Get<unsigned char>(Fixture::pawn + offsets::SkyTint) == 80,
          "partial failure rolls back earlier sky writes");
    tint.Update(f.Access(), Fixture::list, sky, 2, true);
    tint.Update(f.Access(), Fixture::list, sky, 2.1, false);
    check(f.Get<float>(Fixture::pawn + offsets::SkyBrightness) == .8f, "stale or disabled request restores the sky");
    tint.Update(f.Access(), Fixture::list, sky, 3, true);
    f.Put(Fixture::chunk + 4 * offsets::EntityStride + 0x10, 0x100004u);
    f.Put(Fixture::pawn + offsets::SkyTint, std::uint8_t{77});
    const auto previous = f.writes;
    tint.Clear(f.Access());
    check(f.writes == previous && f.Get<unsigned char>(Fixture::pawn + offsets::SkyTint) == 77,
          "recycled sky identity never receives old restoration");
    {
        Fixture sparse;
        constexpr std::uint32_t slot = 32 * 512 + 5;
        constexpr auto clientChunk = Fixture::base + 0x20000;
        constexpr auto identity = clientChunk + 5 * offsets::EntityStride;
        sparse.Put(Fixture::chunk + 4 * offsets::EntityStride, std::uintptr_t{});
        sparse.Put(Fixture::list + offsets::EntityTable + 32 * sizeof(std::uintptr_t), clientChunk);
        sparse.Put(identity, Fixture::pawn);
        sparse.Put(identity + 0x10, 0x80000u + slot);
        sparse.Put(identity + offsets::DesignerName, Fixture::name);
        sparse.Put(Fixture::pawn + offsets::Identity, identity);
        sparse.Put(Fixture::list + offsets::HighestEntity, 4);
        SkyTint distantSky;
        const auto first = distantSky.Update(sparse.Access(), Fixture::list, sky, 1, true);
        check(!first.applied && sparse.reads < 150, "sky discovery bounds each worker sample to 128 allocated slots");
        unsigned applied{};
        for (unsigned i = 1; i < 8 && !applied; ++i)
            applied = distantSky.Update(sparse.Access(), Fixture::list, sky, 1 + i * .01, true).applied;
        check(applied == 1 && sparse.Get<unsigned char>(Fixture::pawn + offsets::SkyTint) == 40,
              "sky discovery reaches sparse client-only chunks beyond reported highest entity");
        check(distantSky.Clear(sparse.Access()) && sparse.Get<float>(Fixture::pawn + offsets::SkyBrightness) == .8f,
              "sparse client sky retains exact ownership restoration");
    }
    {
        Fixture missingHighest;
        missingHighest.Put(Fixture::list + offsets::HighestEntity, 0);
        SkyTint independent;
        check(independent.Update(missingHighest.Access(), Fixture::list, sky, 2, true).applied == 1,
              "missing highest-entity metadata does not disable validated sky discovery");
        independent.Clear(missingHighest.Access());
    }
    styling::Player style;
    style.tintSaturation = 0;
    const auto grey = styling::Tint(Color{.2f, .6f, .8f, .3f}, style);
    check(grey.r == grey.g && grey.g == grey.b && grey.a == .3f, "model tint saturation preserves alpha");
    style.haloPulse = .8f;
    check(std::abs(styling::Pulse(style, .25) - .2f) < .001f && std::abs(styling::Pulse(style, .75) - 1) < .001f,
          "halo pulse stays within the selected strength range");
    for (int look = 0; look < 3; ++look) {
        Configuration c;
        VisualOptions v;
        EffectsConfiguration effects;
        v.assists.shoot = 1;
        v.sky.enabled = 1;
        c.maxDistanceMeters = 231;
        ApplyPlayerLook(look, c, v, effects);
        check(ValidVisualOptions(v) && v.assists.shoot && v.sky.enabled && c.maxDistanceMeters == 231,
              "player look preserves input and world settings");
    }
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {800, 600};
    io.DeltaTime = .016f;
    unsigned char *pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    ImGui::NewFrame();
    auto &draw = *ImGui::GetBackgroundDrawList();
    style = {};
    style.gradientFill = 1;
    styling::Fill(draw, {{100, 100}, {200, 300}}, {1, 1, 1, 1}, .5f, style);
    unsigned top{}, bottom{};
    for (const auto &vertex : draw.VtxBuffer) {
        const auto alpha = (vertex.col >> IM_COL32_A_SHIFT) & 255;
        if (vertex.pos.y == 100)
            top = std::max(top, alpha);
        if (vertex.pos.y == 300)
            bottom = std::max(bottom, alpha);
    }
    check(top == 0 && bottom > 100, "gradient fill has independent top and bottom opacity");
    style.boxRounding = 8;
    const int roundedStart = draw.VtxBuffer.Size;
    styling::Fill(draw, {{100, 100}, {200, 300}}, {1, 1, 1, 1}, .5f, style);
    bool sharpCorner = false;
    unsigned roundedTop = 0, roundedBottom = 0;
    for (int i = roundedStart; i < draw.VtxBuffer.Size; ++i) {
        const auto &v = draw.VtxBuffer[i];
        sharpCorner |= v.pos.x == 100 && v.pos.y == 100;
        const unsigned alpha = (v.col >> IM_COL32_A_SHIFT) & 255;
        if (v.pos.y < 102)
            roundedTop = std::max(roundedTop, alpha);
        if (v.pos.y > 298)
            roundedBottom = std::max(roundedBottom, alpha);
    }
    check(!sharpCorner && roundedTop < 4 && roundedBottom > 100,
          "rounded gradients keep curved corners and interpolate alpha");
    const int before = draw.VtxBuffer.Size;
    style.boxGlow = .5f;
    styling::Border(draw, {{100, 100}, {200, 300}}, {.3f, 1, .6f, 1}, {0, 0, 0, 1}, 1, 1.5f, true, style);
    check(draw.VtxBuffer.Size > before, "corner glow emits bounded decoration geometry");
    bool finite = true;
    for (const auto &v : draw.VtxBuffer)
        finite &= std::isfinite(v.pos.x) && std::isfinite(v.pos.y) && v.pos.x > 80 && v.pos.x < 220;
    check(finite, "decorations remain finite and close to their box");
    ImGui::EndFrame();
    ImGui::DestroyContext();
    std::printf("Visual style checks: %u checks, %u failures.\n", checks, failures);
    return failures ? 1 : 0;
}
