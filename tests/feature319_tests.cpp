#include "world_visuals_reader.hpp"
#include "world_visuals_draw.hpp"
#include "camera_control.hpp"
#include <cstdio>
#include <vector>
using namespace awareness;
using namespace awareness::cs2;
int failures{}, checks{};
void Check(bool value, const char *message) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s\n", message);
    }
}
bool Near(float a, float b) {
    return std::abs(a - b) < .001f;
}
struct Fixture {
    static constexpr std::uintptr_t base = 0x10000000, list = base + 0x1000, chunk = base + 0x10000;
    std::vector<std::byte> data = std::vector<std::byte>(0x100000);
    static bool Read(void *ctx, std::uintptr_t at, void *to, std::size_t n) noexcept {
        auto &f = *static_cast<Fixture *>(ctx);
        if (at < base || at - base > f.data.size() || n > f.data.size() - (at - base))
            return false;
        std::memcpy(to, f.data.data() + at - base, n);
        return true;
    }
    template <class T> void Put(std::uintptr_t at, T value) {
        std::memcpy(data.data() + at - base, &value, sizeof(value));
    }
    void Entity(unsigned index, std::uintptr_t at, const char *name) {
        const auto identity = chunk + index * offsets::EntityStride, text = base + 0xf0000 + index * 64;
        Put(identity, at);
        Put(identity + 0x10, index + 0x80000u);
        Put(at + offsets::Identity, identity);
        Put(identity + offsets::DesignerName, text);
        std::memcpy(data.data() + text - base, name, std::strlen(name) + 1);
    }
    Memory memory{this, Read};
    Fixture() { Put(list + offsets::EntityTable, chunk); }
};
int main() {
    TrackingConfiguration base;
    base.enabled = 1;
    base.fovDegrees = 6;
    base.interpolationSpeed = 20;
    tracking::Options profiles;
    for (const auto &gun : WeaponIcons)
        Check(tracking::WeaponGroup(gun.id) != tracking::Group::Default, "every firearm has a group");
    profiles.groups[3] = {1, 1, 3, 40};
    profiles.groups[4] = {1, 0, 1, 80};
    Check(tracking::Resolve(base, profiles, 7, true).fovDegrees == 3, "rifle FOV switches");
    Check(tracking::Resolve(base, profiles, 60, true).interpolationSpeed == 40, "silenced rifle inherits rifles");
    Check(!tracking::Resolve(base, profiles, 9, true).enabled, "sniper can disable independently");
    Check(tracking::Resolve(base, profiles, 4, true).fovDegrees == 6, "uncustomized pistols use Default");
    Check(!tracking::Resolve(base, profiles, 44, true).enabled, "grenade never uses previous firearm");
    Check(!tracking::Resolve(base, profiles, 0, true).enabled, "failed weapon read pauses native tracking");
    Check(tracking::Resolve(base, profiles, 0, false).enabled, "host API keeps default without weapon metadata");
    profiles.weapons[tracking::WeaponSlot(7)] = {1, 0, 2, 50};
    Check(!tracking::Resolve(base, profiles, 7, true).enabled, "per-gun switch overrides enabled group");
    Check(tracking::Resolve(base, profiles, 16, true).enabled, "per-gun switch leaves other rifles enabled");
    profiles.weapons[tracking::WeaponSlot(7)] = {1, 1, 2, 50};
    Check(tracking::Resolve(base, profiles, 7, true).fovDegrees == 2, "per-gun FOV overrides group");
    base.enabled = 0;
    Check(!tracking::Resolve(base, profiles, 7, true).enabled, "master switch overrides groups");
    profiles.compensation = 1;
    profiles.latencyMs = 30;
    profiles.maxPredictionMs = 80;
    tracking::Motion motion{2, 0x80002, {100, 0, 0}, true};
    Check(Near(tracking::Predict({}, motion, .02, profiles).x, 5), "prediction combines age and configured latency");
    profiles.maxPredictionMs = 40;
    Check(Near(tracking::Predict({}, motion, .02, profiles).x, 4), "prediction time cap");
    profiles.strength = .5f;
    Check(Near(tracking::Predict({}, motion, .02, profiles).x, 2), "prediction strength");
    Check(tracking::Predict({}, motion, .2, profiles).x == 0, "stale samples are not extrapolated");
    Check(tracking::Predict({}, motion, -.01, profiles).x == 0, "future samples are not extrapolated");
    motion.velocity = {};
    Check(tracking::Predict({}, motion, .02, profiles).x == 0, "stopped target does not drift");
    profiles.strength = 1;
    profiles.maxPredictionMs = 200;
    profiles.latencyMs = 200;
    motion.velocity = {1000, 0, 0};
    Check(Near(tracking::Predict({}, motion, .02, profiles).x, 32), "prediction displacement cap");
    motion.velocity.x = 2000;
    Check(tracking::Predict({}, motion, .02, profiles).x == 0, "teleport velocity rejected");
    motion.velocity.x = std::numeric_limits<float>::quiet_NaN();
    Check(tracking::Predict({}, motion, .02, profiles).x == 0, "nonfinite velocity rejected");
    Check(Near(tracking::ResponseSpeed(tracking::SmoothMilliseconds(20)), 20), "smoothness conversion round trip");

    Fixture f;
    const auto pawn = Fixture::base + 0x30000, gun = Fixture::base + 0x40000, scene = Fixture::base + 0x50000;
    const auto service = Fixture::base + 0x60000, collision = Fixture::base + 0x70000,
               pawnScene = Fixture::base + 0x80000;
    f.Entity(1, pawn, "cs_player_pawn");
    f.Entity(2, gun, "weapon_ak47");
    f.Put(pawn + offsets::LifeState, std::uint8_t{0});
    f.Put(pawn + offsets::WeaponServices, service);
    f.Put(service + offsets::ActiveWeapon, 0x80002u);
    f.Put(gun + offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, std::uint16_t{7});
    tracking::Sample sample;
    Check(ReadTrackingWeapon(f.memory, Fixture::list, pawn, sample) && sample.weapon == 7,
          "weapon profiles do not depend on recoil reads");
    f.Put(service + offsets::ActiveWeapon, 0x100002u);
    Check(!ReadTrackingWeapon(f.memory, Fixture::list, pawn, sample), "stale active weapon handle rejected");
    f.Put(service + offsets::ActiveWeapon, 0x80002u);
    f.Put(gun + offsets::EntityOwner, 0xffffffffu);
    f.Put(gun + offsets::SceneNode, scene);
    f.Put(scene + offsets::Origin, Vector3{100, 200, 10});
    f.Put(scene + offsets::Dormant, std::uint8_t{0});
    f.Put(scene + offsets::AbsRotation, Vector3{0, 90, 0});
    f.Put(scene + offsets::AbsScale, 1.f);
    f.Put(gun + offsets::Collision, collision);
    f.Put(collision + offsets::Mins, Vector3{-10, -2, -1});
    f.Put(collision + offsets::Maxs, Vector3{10, 2, 1});
    worldvisuals::DroppedWeapon drop;
    Check(ReadDroppedWeapon(f.memory, Fixture::list, 0x80002, drop) && drop.bounds, "unowned firearm read with bounds");
    Check(Near(drop.corners[0].x, 102) && Near(drop.corners[0].y, 190), "bounds rotate with weapon yaw");
    DroppedReader reader;
    worldvisuals::Drops drops;
    Check(reader.Update(f.memory, Fixture::list, 10, drops) && drops.count == 1,
          "cached discovery finds dropped firearm");
    f.Put(gun + offsets::EntityOwner, 0x80001u);
    Check(!ReadDroppedWeapon(f.memory, Fixture::list, 0x80002, drop), "held firearm excluded");
    reader.Update(f.memory, Fixture::list, 10.01, drops);
    Check(!drops.count, "pickup removes cached item immediately");
    f.Put(gun + offsets::EntityOwner, 0xffffffffu);
    reader.Update(f.memory, Fixture::list, 10.02, drops);
    Check(drops.count == 1, "dropping known firearm restores it");
    f.Put(Fixture::chunk + 2 * offsets::EntityStride + 0x10, 0x100002u);
    Check(!ReadDroppedWeapon(f.memory, Fixture::list, 0x80002, drop), "recycled dropped handle rejected");
    f.Put(Fixture::chunk + 2 * offsets::EntityStride + 0x10, 0x80002u);
    f.Put(pawn + offsets::Team, std::uint8_t{3});
    f.Put(pawn + offsets::SceneNode, pawnScene);
    f.Put(pawnScene + offsets::Origin, Vector3{0, 0, .5f});
    f.Put(pawnScene + offsets::Dormant, std::uint8_t{0});
    worldvisuals::Footstep step;
    Check(ReadFootstep(f.memory, Fixture::list, pawn, 10, step) && step.handle == 0x80001,
          "footstep resolves pawn identity");
    f.Put(pawn + offsets::LifeState, std::uint8_t{1});
    Check(!ReadFootstep(f.memory, Fixture::list, pawn, 10, step), "dead pawn cannot emit footstep");
    worldvisuals::Footsteps steps;
    step = {0x80001, 3, {0, 0, .5f}, 10};
    Check(steps.Add(step) && !steps.Add(step), "duplicate footstep callbacks merge");
    worldvisuals::Options options;
    options.footRadius = .4f;
    options.dropIcons = 0;
    options.dropNames = 1;
    Check(worldvisuals::ShowFootstep(step, 9, 2, options, 10.2), "enemy step visible");
    Check(!worldvisuals::ShowFootstep(step, 1, 2, options, 10.2), "local footsteps hidden");
    Check(!worldvisuals::ShowFootstep(step, 9, 3, options, 10.2), "team filter honored");
    Check(!worldvisuals::ShowFootstep(step, 9, 2, options, 11), "footstep lifetime honored");
    auto invalid = options;
    invalid.footDuration = 0;
    Check(!worldvisuals::Valid(invalid), "invalid duration rejected");

    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = {800, 600};
    io.DeltaTime = .016f;
    io.Fonts->AddFontDefault();
    unsigned char *pixels{};
    int width{}, height{};
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    auto &draw = *ImGui::GetBackgroundDrawList();
    FrameSnapshot frame;
    frame.viewProjection = Matrix4x4::Identity();
    frame.viewport = {0, 0, 800, 600};
    frame.localEntityId = 9;
    frame.localTeam = 2;
    Configuration config;
    config.worldUnitsPerMeter = 1;
    worldvisuals::Draw(draw, io.Fonts->Fonts[0], {}, frame, frame.viewport, config, options, steps, {}, 10.4);
    Check(draw.VtxBuffer.Size > 50, "footstep rings produce draw geometry");
    const auto before = draw.VtxBuffer.Size;
    worldvisuals::Draw(draw, io.Fonts->Fonts[0], {}, frame, frame.viewport, config, options, steps, {}, 15);
    Check(draw.VtxBuffer.Size == before, "expired rings emit no geometry");
    drops = {};
    drops.time = 10;
    drops.count = 1;
    drops.values[0].definition = 7;
    drops.values[0].position = {0, 0, .5f};
    drops.values[0].bounds = true;
    for (unsigned i = 0; i < 8; ++i)
        drops.values[0].corners[i] = {i & 1 ? .4f : -.4f, i & 2 ? .2f : -.2f, i & 4 ? .6f : .4f};
    worldvisuals::Draw(draw, io.Fonts->Fonts[0], {}, frame, frame.viewport, config, options, {}, drops, 10.1);
    Check(draw.VtxBuffer.Size > before, "dropped bounds and labels produce geometry");
    const auto after = draw.VtxBuffer.Size;
    worldvisuals::Draw(draw, io.Fonts->Fonts[0], {}, frame, frame.viewport, config, options, {}, drops, 11);
    Check(draw.VtxBuffer.Size == after, "stale dropped snapshots disappear");
    for (const auto &v : draw.VtxBuffer)
        Check(std::isfinite(v.pos.x) && std::isfinite(v.pos.y), "world vertices finite");
    ImGui::EndFrame();
    ImGui::DestroyContext();
    std::printf("Feature checks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
