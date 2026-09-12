#include "trajectory_reader.hpp"
#include "trajectory_draw.hpp"
#include <cstdio>
#include <limits>
#include <vector>
using namespace awareness;
using namespace awareness::flight;
namespace {
int failures{};
void Check(bool value, const char *message) {
    if (!value) {
        ++failures;
        std::printf("FAIL: %s\n", message);
    }
}
struct Room {
    float wall{10000};
    bool fail{};
    unsigned calls{};
};
bool Sweep(void *context, Vector3 a, Vector3 b, Collision &out) noexcept {
    auto &room = *static_cast<Room *>(context);
    ++room.calls;
    if (room.fail)
        return false;
    out = {};
    out.end = b;
    if (b.z < 2 && a.z >= 2) {
        out.fraction = (a.z - 2) / (a.z - b.z);
        out.normal = {0, 0, 1};
    }
    if (b.x > room.wall && a.x <= room.wall) {
        const float t = (room.wall - a.x) / (b.x - a.x);
        if (t < out.fraction) {
            out.fraction = t;
            out.normal = {-1, 0, 0};
        }
    }
    out.end = a + Scale(b - a, out.fraction);
    return true;
}
struct Fixture {
    std::vector<unsigned char> bytes = std::vector<unsigned char>(0x40000);
    unsigned nameReads{}, reads{};
    std::uintptr_t unreadable{};
    template <class T> void Set(std::uintptr_t at, T value) { std::memcpy(bytes.data() + at, &value, sizeof(value)); }
    static bool Read(void *self, std::uintptr_t at, void *out, std::size_t count) noexcept {
        auto &fixture = *static_cast<Fixture *>(self);
        ++fixture.reads;
        if (at == fixture.unreadable)
            return false;
        if (at == 0x14000)
            ++fixture.nameReads;
        auto &v = static_cast<Fixture *>(self)->bytes;
        if (at > v.size() || count > v.size() - at)
            return false;
        std::memcpy(out, v.data() + at, count);
        return true;
    }
};
} // namespace
int main() {
    Ring<int, 3> ring;
    for (int i = 0; i < 8; ++i)
        ring.Push(i);
    Check(ring.count == 3 && ring[0] == 5 && ring[2] == 7, "ring overwrites oldest points");
    Trails trails;
    Projectile p{1, Utility::HE, {0, 0, 2}};
    for (unsigned i = 0; i < 641; ++i) {
        p.position.x = static_cast<float>(i);
        trails.Update({&p, 1}, i / 64.);
    }
    const auto &path = trails.Paths()[0];
    Check(path.points.count == 193 && path.points[0].time == 7 && path.points[192].time == 10,
          "last three seconds at 64 Hz retained");
    trails.Update({}, 10.1);
    Check(!path.active && std::abs(Fade(12.5, path.lastSeen, 5) - .5f) < 1e-5f,
          "finished trail fades over five seconds");
    trails.Update({}, 15);
    Check(!path.handle, "expired trail removed");
    p.handle = 0x8001;
    trails.Update({&p, 1}, 16);
    p.handle = 0x10001;
    trails.Update({&p, 1}, 16.1);
    Check(trails.Paths()[0].handle != trails.Paths()[1].handle, "reused entity index keeps generations separate");
    Trails resumed;
    Projectile resumedProjectile{0x8004, Utility::HE, {1, 2, 3}};
    resumed.Update({&resumedProjectile, 1}, 20);
    resumedProjectile.position.x = 2;
    resumed.Update({&resumedProjectile, 1}, 20.02);
    resumed.Update({}, 20.03);
    resumedProjectile.position.x = 3;
    resumed.Update({&resumedProjectile, 1}, 20.04);
    Check(resumed.Paths()[0].points.count == 3 && resumed.Paths()[0].points[0].position.x == 1,
          "one missed sample preserves the earlier projectile path");
    resumed.Update({}, 20.05);
    resumed.Update({&resumedProjectile, 1}, 20.4);
    Check(resumed.Paths()[0].points.count == 1, "a long gap does not bridge an unknown projectile trajectory");
    resumedProjectile.type = Utility::Smoke;
    resumed.Update({&resumedProjectile, 1}, 20.42);
    Check(resumed.Paths()[0].points.count == 1 && resumed.Paths()[0].type == Utility::Smoke,
          "a changed projectile type cannot inherit another trajectory");
    Tracers traces;
    for (unsigned i = 0; i < 140; ++i)
        traces.Add({{0, 0, 0}, {float(i), 0, 0}, 1, 2, i * .001});
    Check(traces.Lines().count == 140 && traces.Lines()[0].end.x == 0,
          "tracer pool preserves bursts from multiple players");
    traces.Expire(.745);
    Check(traces.Lines().count == 0, "tracers expire after half a second");
    Shot shot{{}, {1, 0, 0}, 5, 2, 0};
    Check(Matches(shot, Shots::Local, 5, 3) && !Matches(shot, Shots::Local, 6, 2), "local shot filter");
    Check(Matches(shot, Shots::Opponents, 6, 3) && !Matches(shot, Shots::Opponents, 6, 0),
          "opponent filter rejects unknown team");
    Check(Matches(shot, Shots::Teammates, 6, 2) && Matches(shot, Shots::TeamT, 6, 3) &&
              !Matches(shot, Shots::TeamCT, 6, 3),
          "team shot filters");
    Room room;
    Throw input;
    input.type = Utility::Smoke;
    input.eye = {0, 0, 64};
    auto prediction = Predict(input, {&room, Sweep});
    Check(prediction.valid && prediction.finished && prediction.bounceCount > 0 &&
              std::abs(prediction.landing.z - 2) < .1f,
          "smoke preview bounces and settles on collision floor");
    const auto longThrow = prediction.landing.x;
    input.strength = 0;
    auto underhand = Predict(input, {&room, Sweep});
    Check(underhand.valid && underhand.landing.x < longThrow, "throw strength affects landing");
    room.wall = 100;
    input.strength = 1;
    auto wall = Predict(input, {&room, Sweep});
    Check(wall.valid && wall.bounceCount > 0 && wall.bounces[0].x <= 100.01f,
          "preview hits a wall rather than passing through");
    room.fail = true;
    Check(!Predict(input, {&room, Sweep}).valid, "failed collision query has no false landing");
    room.fail = false;
    room.wall = 10000;
    input.type = Utility::HE;
    auto he = Predict(input, {&room, Sweep});
    Check(he.valid && he.finished && he.count == 97, "HE timer ends at 1.5 seconds");
    input.type = Utility::Fire;
    auto fire = Predict(input, {&room, Sweep});
    Check(fire.finished && fire.count < he.count, "fire ignites on a suitable floor");
    input.pitch = std::numeric_limits<float>::quiet_NaN();
    Check(!Predict(input, {&room, Sweep}).valid, "nonfinite input rejected");
    Fov fov;
    Check(fov.Update(90, true, 140, 0) == 90, "FOV starts from game camera");
    const auto mid = fov.Update(90, true, 140, .1);
    Check(mid > 90 && mid < 140, "FOV interpolates");
    for (int i = 2; i <= 100; ++i)
        fov.Update(90, true, 140, i * .1);
    Check(fov.Update(90, true, 300, 10.1) <= 140, "FOV clamped");
    for (int i = 102; i < 200; ++i)
        fov.Update(80, false, 90, i * .1);
    Check(fov.Update(80, false, 90, 20) == 80, "FOV disable restores current engine FOV");
    Check(cs2::ProjectileType("prop_physics") == Utility::None, "non-projectile entities ignored");
    // Reader fixture: current schema, full identity validation, no live process required.
    Fixture fixture;
    cs2::Memory memory{&fixture, Fixture::Read};
    const std::uintptr_t list = 0x1000, chunk = 0x18000, entity = 0x10000, scene = 0x13000, name = 0x14000;
    fixture.Set(list + cs2::offsets::HighestEntity, 1);
    fixture.Set(list + cs2::offsets::EntityTable, chunk);
    fixture.Set(chunk + cs2::offsets::EntityStride, entity);
    fixture.Set(entity + cs2::offsets::Identity, chunk + cs2::offsets::EntityStride);
    fixture.Set(chunk + cs2::offsets::EntityStride + 0x10, std::uint32_t{0x8001});
    fixture.Set(chunk + cs2::offsets::EntityStride + cs2::offsets::DesignerName, name);
    std::memcpy(fixture.bytes.data() + name, "hegrenade_projectile", 21);
    fixture.Set(entity + cs2::offsets::SceneNode, scene);
    fixture.Set(scene + cs2::offsets::Origin, Vector3{50, 60, 70});
    cs2::ProjectileFrame frame;
    Check(cs2::ReadProjectiles(memory, list, frame) && frame.count == 1 && frame.values[0].handle == 0x8001,
          "native grenade reader resolves type and full handle");
    fixture.Set(entity + cs2::offsets::ProjectileExploded, std::uint8_t{1});
    Check(cs2::ReadProjectiles(memory, list, frame) && frame.count == 0, "detonated projectile ends its trail");
    fixture.Set(entity + cs2::offsets::ProjectileExploded, std::uint8_t{0});
    cs2::ProjectileTracker tracker;
    Check(tracker.Update(memory, list, 1, frame) && frame.count == 1, "tracker discovers a new throw immediately");
    const auto names = fixture.nameReads;
    fixture.Set(scene + cs2::offsets::Origin, Vector3{80, 60, 70});
    Check(tracker.Update(memory, list, 1.01, frame) && frame.count == 1 && frame.values[0].position.x == 80 &&
              fixture.nameReads == names,
          "known projectile moves every frame without rescanning designer names");
    fixture.unreadable = scene + cs2::offsets::Origin;
    Check(tracker.Update(memory, list, 1.015, frame) && frame.count == 0,
          "a failed position read never publishes a stale projectile");
    fixture.unreadable = 0;
    Check(tracker.Update(memory, list, 1.016, frame) && frame.count == 1 && frame.values[0].position.x == 80,
          "known projectile retries immediately before the next discovery scan");
    fixture.Set(chunk + cs2::offsets::EntityStride + 0x10, std::uint32_t{0x18001});
    Check(tracker.Update(memory, list, 1.02, frame) && frame.count == 0,
          "recycled projectile handle drops immediately");
    Check(tracker.Update(memory, list, 1.06, frame) && frame.count == 1 && fixture.nameReads > names,
          "new generations are rediscovered after the bounded scan interval");
    fixture.Set(entity + cs2::offsets::ProjectileExploded, std::uint8_t{1});
    Check(tracker.Update(memory, list, 1.07, frame) && frame.count == 0, "detonation drops on the next frame");
    Check(!tracker.Update(memory, list, std::numeric_limits<double>::quiet_NaN(), frame) && frame.count == 0,
          "invalid tracker time resets cached data");
    // Client-only entities live in sparse high chunks beyond the legacy highest field.
    fixture.Set(entity + cs2::offsets::ProjectileExploded, std::uint8_t{0});
    fixture.Set(chunk + cs2::offsets::EntityStride, std::uintptr_t{0});
    constexpr std::uintptr_t sparseChunk = 0x28000;
    constexpr std::uint32_t sparseIndex = 32 * 512 + 1, sparseHandle = 0x8000 + sparseIndex;
    fixture.Set(list + cs2::offsets::EntityTable + 32 * sizeof(std::uintptr_t), sparseChunk);
    const auto sparseIdentity = sparseChunk + cs2::offsets::EntityStride;
    fixture.Set(sparseIdentity, entity);
    fixture.Set(sparseIdentity + 0x10, sparseHandle);
    fixture.Set(sparseIdentity + cs2::offsets::DesignerName, name);
    fixture.Set(entity + cs2::offsets::Identity, sparseIdentity);
    Check(cs2::ReadProjectiles(memory, list, frame) && frame.count == 1 && frame.values[0].handle == sparseHandle,
          "one-shot utility scan includes client chunks above reported highest");
    tracker.Reset();
    unsigned peakReads{};
    for (int tick = 0; tick < 4; ++tick) {
        const auto beforeReads = fixture.reads;
        Check(tracker.Update(memory, list, 2 + tick * .016, frame), "sparse discovery advances");
        peakReads = std::max(peakReads, fixture.reads - beforeReads);
    }
    Check(frame.count == 1 && frame.values[0].handle == sparseHandle && peakReads < 400,
          "bounded discovery skips empty chunks and finds client-only utility in four ticks");
    fixture.Set(list + cs2::offsets::EntityTable + 32 * sizeof(std::uintptr_t), std::uintptr_t{0});
    Check(tracker.Update(memory, list, 2.1, frame) && !frame.count, "unloaded chunks drop cached utility");
    // Held-grenade reads reject recycled active-weapon handles and malformed state.
    const std::uintptr_t pawn = 0x20000, heldWeapon = 0x23000, pawnScene = 0x27000, services = 0x26000;
    const auto weaponIdentity = chunk + 2 * cs2::offsets::EntityStride;
    fixture.Set(weaponIdentity, heldWeapon);
    fixture.Set(weaponIdentity + 0x10, std::uint32_t{0x8002});
    fixture.Set(heldWeapon + cs2::offsets::Identity, weaponIdentity);
    fixture.Set(heldWeapon + cs2::offsets::AttributeManager + cs2::offsets::ItemView + cs2::offsets::ItemDefinition,
                std::uint16_t{44});
    fixture.Set(pawn + cs2::offsets::SceneNode, pawnScene);
    fixture.Set(pawnScene + cs2::offsets::Origin, Vector3{10, 20, 30});
    fixture.Set(pawn + cs2::offsets::ViewOffset, Vector3{0, 0, 64});
    fixture.Set(pawn + cs2::offsets::WeaponServices, services);
    fixture.Set(services + cs2::offsets::ActiveWeapon, std::uint32_t{0x8002});
    Throw held;
    Check(cs2::ReadThrow(memory, list, pawn, {0, 90, 0}, held) && held.type == Utility::HE && held.eye.z == 94 &&
              held.strength == 1,
          "equipped grenade previews before the pin is pulled");
    Check(cs2::ReadThrow(memory, list, pawn, {0, 450, 0}, held) && held.yaw == 90,
          "finite unwrapped yaw stays previewable after full camera rotations");
    fixture.Set(heldWeapon + cs2::offsets::PinPulled, std::uint8_t{1});
    fixture.Set(heldWeapon + cs2::offsets::ThrowStrength, .5f);
    Check(cs2::ReadThrow(memory, list, pawn, {0, 90, 0}, held) && held.strength == .5f,
          "held grenade preview uses the active throw strength");
    fixture.Set(weaponIdentity + 0x10, std::uint32_t{0x18002});
    Check(!cs2::ReadThrow(memory, list, pawn, {0, 90, 0}, held),
          "recycled weapon index cannot become the active grenade");
    fixture.Set(weaponIdentity + 0x10, std::uint32_t{0x8002});
    fixture.Set(heldWeapon + cs2::offsets::ThrowTime, std::numeric_limits<float>::quiet_NaN());
    Check(!cs2::ReadThrow(memory, list, pawn, {0, 90, 0}, held), "invalid throw time cannot produce a preview");
    fixture.Set(heldWeapon + cs2::offsets::ThrowTime, 1.f);
    Check(!cs2::ReadThrow(memory, list, pawn, {0, 90, 0}, held), "committed throws do not retain a held preview");
    fixture.Set(heldWeapon + cs2::offsets::ThrowTime, 0.f);
    Check(!cs2::ReadThrow(memory, list, pawn, {0, std::numeric_limits<float>::quiet_NaN(), 0}, held),
          "invalid view angles never enter collision prediction");
    // Exercise the production ImDrawList path with offscreen clipping and faded geometry.
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {800, 600};
    io.DeltaTime = 1.f / 60;
    io.Fonts->AddFontDefault();
    unsigned char *pixels{};
    int width{}, height{};
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    auto *draw = ImGui::GetBackgroundDrawList();
    Matrix4x4 matrix = Matrix4x4::Identity();
    traces.Clear();
    traces.Add({{-2, 0, .5}, {2, 0, .5}, 1, 2, 1});
    Draw(*draw, {}, {}, traces, false, false, true, Shots::All, 1, 2, matrix, {0, 0, 800, 600}, 1.25);
    Check(draw->VtxBuffer.Size > 0, "production tracer renderer emits geometry");
    bool valid = true;
    for (auto &v : draw->VtxBuffer)
        valid &= std::isfinite(v.pos.x) && std::isfinite(v.pos.y) && v.pos.x >= -3 && v.pos.x <= 803;
    Check(valid, "tracer vertices stay finite and within clipped viewport");
    const auto before = draw->VtxBuffer.Size;
    Draw(*draw, {}, {}, traces, false, false, true, Shots::All, 1, 2, matrix, {0, 0, 800, 600}, 1.8);
    Check(draw->VtxBuffer.Size == before, "expired tracers draw no geometry");
    Tracers centered;
    centered.Add({{0, 0, .1f}, {0, 0, .9f}, 1, 2, 2});
    Draw(*draw, {}, {}, centered, false, false, true, Shots::All, 1, 2, matrix, {0, 0, 800, 600}, 2);
    Check(draw->VtxBuffer.Size > before, "camera-aligned shots retain a visible impact marker");
    ImVec2 ca, cb;
    Check(ScreenLine({0, 0, -1}, {.5f, 0, .5f}, matrix, {0, 0, 800, 600}, ca, cb),
          "near-plane crossing tracer is clipped, not discarded");

    Tracers lasting;
    lasting.SetLifetime(3);
    lasting.Add({{-.5f, 0, .5f}, {.5f, 0, .5f}, 1, 2, 20});
    lasting.Add({{-.4f, 0, .5f}, {.4f, 0, .5f}, 1, 2, 21});
    lasting.Expire(22.99);
    Check(lasting.Lines().count == 2, "new shots retain earlier lines for the selected lifetime");
    lasting.Expire(23);
    Check(lasting.Lines().count == 1, "tracer expires at its own lifetime boundary");
    PathStyle longStyle;
    longStyle.shotLifetime = 3;
    auto longBefore = draw->VtxBuffer.Size;
    Draw(*draw, {}, {}, lasting, false, false, true, Shots::All, 1, 2, matrix, {0, 0, 800, 600}, 23, 1, longStyle);
    Check(draw->VtxBuffer.Size > longBefore, "long-lived tracer renders after the old half-second limit");
    longStyle.shotLifetime = .2f;
    longBefore = draw->VtxBuffer.Size;
    Draw(*draw, {}, {}, lasting, false, false, true, Shots::All, 1, 2, matrix, {0, 0, 800, 600}, 23, 1, longStyle);
    Check(draw->VtxBuffer.Size == longBefore, "shortening lifetime immediately hides older lines");
    PathStyle style;
    Check(!style.trailGlow && !style.shotGlow && !style.previewGlow && style.shotWidth == 1.15f &&
              style.shotLifetime == .5f && ValidPathStyle(style),
          "default trajectories use thin short strokes without glow");
    Prediction preview;
    preview.valid = true;
    preview.type = Utility::HE;
    preview.count = 2;
    preview.points[0] = {-.5f, 0, .5f};
    preview.points[1] = {.5f, 0, .5f};
    style.previewHE = {1, 0, 0, 1};
    style.shotStart = {0, 1, 0, 1};
    style.shotEnd = {0, 0, 1, 1};
    auto mark = draw->VtxBuffer.Size;
    Draw(*draw, {}, preview, {}, false, true, false, Shots::All, 1, 2, matrix, {0, 0, 800, 600}, 1, 1, style);
    Check(draw->VtxBuffer.Size - mark == 8 && draw->VtxBuffer[mark + 2].col == IM_COL32(255, 0, 0, 255),
          "plain preview uses its own color with only an antialiased core");
    style.previewGlow = 1;
    mark = draw->VtxBuffer.Size;
    Draw(*draw, {}, preview, {}, false, true, false, Shots::All, 1, 2, matrix, {0, 0, 800, 600}, 1, 1, style);
    Check(draw->VtxBuffer.Size - mark == 16 && !style.trailGlow && !style.shotGlow,
          "preview glow can be enabled independently");
    mark = draw->VtxBuffer.Size;
    Draw(*draw, {}, {}, traces, false, false, true, Shots::All, 1, 2, matrix, {0, 0, 800, 600}, 1, 1, style);
    Check(draw->VtxBuffer.Size - mark == 8 && draw->VtxBuffer[mark + 2].col == IM_COL32(0, 255, 0, 255) &&
              draw->VtxBuffer[mark + 3].col == IM_COL32(0, 0, 255, 255),
          "default bullet stroke keeps both endpoint colors without adding glow geometry");
    // Exercise the optional strong treatment explicitly; it is no longer the shipped default.
    style.shotGlow = style.trailGlow = 1;
    style.shotStrength = 2.4f;
    mark = draw->VtxBuffer.Size;
    Draw(*draw, {}, {}, traces, false, false, true, Shots::All, 1, 2, matrix, {0, 0, 800, 600}, 1, 1, style);
    Check(draw->VtxBuffer.Size - mark > 16 && draw->VtxBuffer[mark + 6].col == IM_COL32(0, 255, 0, 255) &&
              draw->VtxBuffer[mark + 7].col == IM_COL32(0, 0, 255, 255),
          "strong bullet glow keeps endpoint colors and adds its fine core");
    const auto haloAlpha = draw->VtxBuffer[mark + 4].col >> IM_COL32_A_SHIFT;
    Check(haloAlpha > 170 && haloAlpha < 200, "explicit strong bullet halo remains bright outside the core");
    style.shotGlow = 0;
    mark = draw->VtxBuffer.Size;
    Draw(*draw, {}, {}, traces, false, false, true, Shots::All, 1, 2, matrix, {0, 0, 800, 600}, 1, 1, style);
    Check(draw->VtxBuffer.Size - mark == 8 && style.previewGlow && style.trailGlow,
          "disabling bullet glow leaves the other glow choices intact");
    mark = draw->VtxBuffer.Size;
    ScreenPolyline curved(*draw, style.trailHE, 1, style.trailWidth, true, style.trailStrength);
    curved.Add({100, 250}, {200, 150});
    curved.Add({200, 150}, {300, 250});
    curved.Flush();
    bool shared = draw->VtxBuffer.Size - mark == 32;
    for (int band = 0; band < 8; ++band) {
        const auto a = draw->VtxBuffer[mark + band * 2 + 1].pos;
        const auto b = draw->VtxBuffer[mark + 16 + band * 2].pos;
        shared &= a.x == b.x && a.y == b.y;
    }
    Check(shared, "curved paths share every halo edge without cracks or overlapping strips");
    Trails live;
    Projectile moving{1, Utility::HE, {-.2f, 0, .5f}};
    live.Update({&moving, 1}, 2);
    moving.position = {.2f, 0, .5f};
    live.Update({&moving, 1}, 2.001);
    Check(live.Paths()[0].points.count == 1 && live.Paths()[0].head.x == .2f,
          "trail endpoint moves between ring samples");
    mark = draw->VtxBuffer.Size;
    Draw(*draw, live, {}, {}, true, false, false, Shots::All, 1, 2, matrix, {0, 0, 800, 600}, 2.001, 1, style);
    Check(draw->VtxBuffer.Size > mark, "live trail head connects before next history tick");
    ImGui::EndFrame();
    ImGui::DestroyContext();
    std::printf("Trajectory checks: %d failures\n", failures);
    return failures ? 1 : 0;
}
