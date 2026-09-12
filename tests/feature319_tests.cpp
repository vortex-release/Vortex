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
    for (const auto &gun : WeaponIcons)
        Check(worldvisuals::DropCategory(gun.id) < 5 && worldvisuals::DroppedName(gun.id),
              "every existing firearm retains a dropped-item category and label");
    Check(worldvisuals::DropCategory(4) == 0 && worldvisuals::DropCategory(17) == 1 &&
              worldvisuals::DropCategory(7) == 2 && worldvisuals::DropCategory(9) == 3 &&
              worldvisuals::DropCategory(25) == 4 && worldvisuals::DropCategory(14) == 4,
          "pistols, SMGs, rifles, snipers and heavy weapons have distinct groups");
    for (const auto definition : {31u, 43u, 44u, 45u, 46u, 47u, 48u, 49u, 57u, 42u, 525u})
        Check(worldvisuals::DropCategory(definition) == 5 && worldvisuals::DroppedName(definition),
              "utility and knife definitions have bounded names and the utility group");
    Check(worldvisuals::DropCategory(0) == worldvisuals::DropGroupCount &&
              worldvisuals::DropCategory(999) == worldvisuals::DropGroupCount && !worldvisuals::DroppedName(999),
          "unknown definitions are not misidentified as another category");
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
    f.Put(gun + offsets::Clip1, 17);
    worldvisuals::DroppedWeapon drop;
    Check(ReadDroppedWeapon(f.memory, Fixture::list, 0x80002, drop) && drop.bounds, "unowned firearm read with bounds");
    Check(drop.ammo == 17, "dropped firearm captures magazine ammunition");
    f.Put(gun + offsets::Clip1, -1);
    Check(ReadDroppedWeapon(f.memory, Fixture::list, 0x80002, drop) && drop.ammo == -1,
          "unknown ammo preserves weapon without fabricated empty magazine");
    f.Put(gun + offsets::Clip1, 0);
    Check(ReadDroppedWeapon(f.memory, Fixture::list, 0x80002, drop) && drop.ammo == 0,
          "empty magazine remains a known zero");
    f.Put(gun + offsets::Clip1, 251);
    Check(ReadDroppedWeapon(f.memory, Fixture::list, 0x80002, drop) && drop.ammo == -1, "implausible ammo ignored");
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
    const auto utility = Fixture::base + 0x90000, utilityScene = Fixture::base + 0xa0000;
    f.Entity(3, utility, "weapon_smokegrenade");
    f.Put(utility + offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, std::uint16_t{45});
    f.Put(utility + offsets::EntityOwner, 0xffffffffu);
    f.Put(utility + offsets::SceneNode, utilityScene);
    f.Put(utilityScene + offsets::Origin, Vector3{20, 30, 10});
    f.Put(utilityScene + offsets::Dormant, std::uint8_t{0});
    reader.Reset();
    Check(reader.Update(f.memory, Fixture::list, 10.1, drops) && drops.count == 2,
          "discovery includes an unowned utility item without needing a firearm icon");
    Check(ReadDroppedWeapon(f.memory, Fixture::list, 0x80003, drop) && drop.definition == 45,
          "utility item reader preserves the actual definition");
    f.Put(utility + offsets::EntityOwner, 0x80001u);
    Check(!ReadDroppedWeapon(f.memory, Fixture::list, 0x80003, drop), "held utility remains excluded");
    reader.Update(f.memory, Fixture::list, 10.11, drops);
    Check(drops.count == 1, "utility pickup disappears immediately from the cached snapshot");
    f.Put(utility + offsets::EntityOwner, 0xffffffffu);
    f.Put(utilityScene + offsets::Dormant, std::uint8_t{1});
    Check(!ReadDroppedWeapon(f.memory, Fixture::list, 0x80003, drop), "dormant utility cannot render");
    f.Put(utilityScene + offsets::Dormant, std::uint8_t{0});
    f.Put(utility + offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, std::uint16_t{999});
    Check(!ReadDroppedWeapon(f.memory, Fixture::list, 0x80003, drop), "unknown utility definitions are rejected");
    f.Put(pawn + offsets::Team, std::uint8_t{3});
    f.Put(pawn + offsets::SceneNode, pawnScene);
    f.Put(pawnScene + offsets::Origin, Vector3{0, 0, .5f});
    f.Put(pawnScene + offsets::Dormant, std::uint8_t{0});
    f.Entity(1, pawn, "c_cs_player_for_precache");
    f.Put(pawn + offsets::MovementServices, service);
    f.Put(service + offsets::MovementStepSide, 0);
    f.Put(pawn + offsets::MovementFlags, 1u);
    f.Put(pawn + offsets::AbsVelocity, Vector3{120, 0, 0});
    FrameSnapshot stepFrame;
    stepFrame.entityCount = 1;
    stepFrame.entities[0].id = 1;
    stepFrame.entities[0].valid = true;
    stepFrame.entities[0].health = 100;
    FootstepReader phaseReader;
    phaseReader.Update(f.memory, Fixture::list, stepFrame, 10);
    Check(phaseReader.Count() == 0, "first movement sample must not fabricate a step");
    f.Put(service + offsets::MovementStepSide, 1);
    phaseReader.Update(f.memory, Fixture::list, stepFrame, 10.1);
    Check(phaseReader.Count() == 1, "verified step-side transition emits one anchored ring");
    phaseReader.Update(f.memory, Fixture::list, stepFrame, 10.2);
    Check(phaseReader.Count() == 1, "unchanged step phase never repeats rings");
    f.Put(service + offsets::MovementStepSide, 0);
    f.Put(pawn + offsets::MovementFlags, 0u);
    phaseReader.Update(f.memory, Fixture::list, stepFrame, 10.3);
    Check(phaseReader.Count() == 1, "airborne phase changes cannot emit ground steps");
    worldvisuals::Footstep step;
    Check(ReadFootstep(f.memory, Fixture::list, pawn, 10, step) && step.handle == 0x80001,
          "footstep resolves pawn identity");
    f.Put(pawn + offsets::LifeState, std::uint8_t{1});
    Check(!ReadFootstep(f.memory, Fixture::list, pawn, 10, step), "dead pawn cannot emit footstep");
    worldvisuals::Footsteps steps;
    step = {0x80001, 3, {0, 0, .5f}, 10};
    Check(steps.Add(step) && !steps.Add(step), "duplicate footstep callbacks merge");
    auto earlierDuplicate = step;
    earlierDuplicate.time -= .04;
    Check(!steps.Add(earlierDuplicate) && steps.Events().count == 1,
          "older fallback and newer native callback deduplicate symmetrically");
    {
        worldvisuals::Footsteps native, expired;
        native.Add({500, 3, {1, 0, 0}, 99.95});
        for (unsigned i = 0; i < 128; ++i)
            expired.Add({1000 + i, 3, {}, 50 + i * .1});
        const auto merged = worldvisuals::Footsteps::MergeLive(native, expired, 100, 1);
        Check(merged.Events().count == 1 && merged.Events()[0].handle == 500,
              "full expired fallback history cannot evict the current native footstep");
    }
    {
        worldvisuals::Footsteps even, odd;
        for (unsigned i = 0; i < 256; ++i)
            (i % 2 ? odd : even).Add({1000 + i, 3, {}, 99 + i * .003});
        const auto merged = worldvisuals::Footsteps::MergeLive(even, odd, 100, 2);
        bool chronological = true;
        for (std::size_t i = 1; i < merged.Events().count; ++i)
            chronological &= merged.Events()[i].time >= merged.Events()[i - 1].time;
        Check(merged.Events().count == 128 && merged.Events()[0].handle == 1128 &&
                  merged.Events()[127].handle == 1255 && chronological,
              "merged histories preserve the newest 128 events in chronological order");
        Check(!even.Add({999, 3, {}, 10}) && even.Events()[0].handle == 1000,
              "late expired insertion cannot evict a newer event from a full history");
        worldvisuals::Footsteps future;
        future.Add({2000, 3, {}, 101});
        Check(worldvisuals::Footsteps::MergeLive(future, {}, 100, 2).Events().count == 0,
              "future timestamps are excluded from merged live footsteps");
    }
    worldvisuals::Options options;
    Check(worldvisuals::Valid(options) && worldvisuals::ResolveDrop(options, 7).enabled &&
              !worldvisuals::ResolveDrop(options, 45).enabled,
          "default groups preserve firearm visibility and leave utility opt-in");
    options.dropRange = 85;
    options.dropColor = {.2f, .4f, .6f, .8f};
    auto inherited = worldvisuals::ResolveDrop(options, 7);
    Check(inherited.icons && !inherited.names && inherited.range == 85 && inherited.color.g == .4f,
          "uncustomized groups inherit the existing global display, distance and color");
    options.dropGroups[2].custom = 1;
    options.dropGroups[2].display = 2;
    options.dropGroups[2].range = 25;
    options.dropGroups[2].color = {.8f, .6f, .4f, .2f};
    auto rifleStyle = worldvisuals::ResolveDrop(options, 7);
    Check(rifleStyle.enabled && rifleStyle.icons && rifleStyle.names && rifleStyle.range == 25 &&
              rifleStyle.color.a == .2f && worldvisuals::ResolveDrop(options, 4).range == 85,
          "custom rifle display, range and color do not change other groups");
    options.dropGroups[2].enabled = 0;
    Check(!worldvisuals::ResolveDrop(options, 7).enabled && worldvisuals::ResolveDrop(options, 4).enabled,
          "a disabled group filters only that category");
    options.dropGroups[5].enabled = 1;
    Check(worldvisuals::ResolveDrop(options, 49).enabled, "utility category can be enabled independently");
    options.dropped = 0;
    Check(!worldvisuals::ResolveDrop(options, 4).enabled && !worldvisuals::ResolveDrop(options, 49).enabled,
          "the master dropped switch overrides all category switches");
    options = {};
    for (int field = 0; field < 5; ++field) {
        auto invalidGroup = options;
        auto &group = invalidGroup.dropGroups[2];
        if (field == 0)
            group.enabled = 2;
        if (field == 1)
            group.custom = 2;
        if (field == 2)
            group.display = 4;
        if (field == 3)
            group.range = std::numeric_limits<float>::quiet_NaN();
        if (field == 4)
            group.color.a = 1.1f;
        Check(!worldvisuals::Valid(invalidGroup), "invalid category configuration is rejected");
    }
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
    options.footsteps = 0;
    frame.cameraOrigin = {0, 0, -20};
    options.dropGroups[2].custom = 1;
    options.dropGroups[2].range = 5;
    auto groupBefore = draw.VtxBuffer.Size;
    worldvisuals::Draw(draw, io.Fonts->Fonts[0], {}, frame, frame.viewport, config, options, {}, drops, 10.1);
    Check(draw.VtxBuffer.Size == groupBefore, "short rifle range suppresses every part of its dropped overlay");
    options.dropGroups[2].range = 60;
    options.dropGroups[2].display = 1;
    options.dropGroups[2].color = {.7f, .2f, .3f, 1};
    options.dropBoxes = options.dropDistance = 0;
    worldvisuals::Draw(draw, io.Fonts->Fonts[0], {}, frame, frame.viewport, config, options, {}, drops, 10.1);
    const auto nameCount = draw.VtxBuffer.Size - groupBefore;
    Check(nameCount > 0, "custom name-only dropped display emits its label");
    bool categoryColor = false;
    for (int i = groupBefore; i < draw.VtxBuffer.Size; ++i)
        categoryColor |= draw.VtxBuffer[i].col == flight::Pack(options.dropGroups[2].color, config.opacity);
    Check(categoryColor, "custom category color reaches rendered label vertices");
    groupBefore = draw.VtxBuffer.Size;
    options.dropGroups[2].display = 3;
    worldvisuals::Draw(draw, io.Fonts->Fonts[0], {}, frame, frame.viewport, config, options, {}, drops, 10.1);
    Check(draw.VtxBuffer.Size == groupBefore, "none display emits no icon or name when bounds and distance are off");
    options.dropAmmo = 1;
    drops.values[0].ammo = 17;
    worldvisuals::Draw(draw, io.Fonts->Fonts[0], {}, frame, frame.viewport, config, options, {}, drops, 10.1);
    Check(draw.VtxBuffer.Size > groupBefore, "ammo-only mode emits real magazine label");
    options.dropAmmo = 0;
    groupBefore = draw.VtxBuffer.Size;
    options.dropGroups[2].display = 2;
    worldvisuals::Draw(draw, io.Fonts->Fonts[0], {}, frame, frame.viewport, config, options, {}, drops, 10.1);
    Check(draw.VtxBuffer.Size - groupBefore == nameCount,
          "missing icon atlas falls back to one label without duplicating text");
    groupBefore = draw.VtxBuffer.Size;
    options.dropGroups[2].enabled = 0;
    worldvisuals::Draw(draw, io.Fonts->Fonts[0], {}, frame, frame.viewport, config, options, {}, drops, 10.1);
    Check(draw.VtxBuffer.Size == groupBefore, "disabled category emits no dropped geometry");
    drops.values[0].definition = 45;
    worldvisuals::Draw(draw, io.Fonts->Fonts[0], {}, frame, frame.viewport, config, options, {}, drops, 10.1);
    Check(draw.VtxBuffer.Size == groupBefore, "utility remains hidden under its preserved default");
    options.dropGroups[5].enabled = 1;
    options.dropIcons = 1;
    options.dropNames = 0;
    worldvisuals::Draw(draw, io.Fonts->Fonts[0], {}, frame, frame.viewport, config, options, {}, drops, 10.1);
    Check(draw.VtxBuffer.Size > groupBefore, "enabled utility icon mode gets a readable name fallback");
    groupBefore = draw.VtxBuffer.Size;
    drops.time = std::numeric_limits<double>::quiet_NaN();
    worldvisuals::Draw(draw, io.Fonts->Fonts[0], {}, frame, frame.viewport, config, options, {}, drops, 10.1);
    Check(draw.VtxBuffer.Size == groupBefore, "invalid dropped snapshot time emits no geometry");
    for (const auto &v : draw.VtxBuffer)
        Check(std::isfinite(v.pos.x) && std::isfinite(v.pos.y), "world vertices finite");
    ImGui::EndFrame();
    ImGui::DestroyContext();
    std::printf("Feature checks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
