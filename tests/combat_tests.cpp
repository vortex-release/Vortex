#include "combat_reader.hpp"
#include "world_filter.hpp"
#include "combat_draw.hpp"
#include <cstdio>
#include <vector>
#include <memory>
using namespace awareness;
using namespace awareness::cs2;
int failures{}, checks{};
void Check(bool value, const char *name) {
    ++checks;
    if (!value) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", name);
    }
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
    void Entity(std::uint32_t index, std::uintptr_t at, const char *name) {
        const auto identity = chunk + index * offsets::EntityStride;
        const auto text = base + 0xf0000 + index * 64;
        Put(identity, at);
        Put(identity + 0x10, index + 0x80000u);
        Put(at + offsets::Identity, identity);
        Put(identity + offsets::DesignerName, text);
        std::strcpy(reinterpret_cast<char *>(data.data() + text - base), name);
    }
    Memory memory{this, Read};
    Fixture() {
        Put(list + offsets::EntityTable, chunk);
        Put(list + offsets::HighestEntity, 10);
    }
};
int main() {
    combat::Options options;
    Check(combat::Valid(options) && !options.recoil && !options.ghosts, "new features opt in");
    options.weapons[combat::WeaponProfile(7)] = {1, 2, 1, .7f, .8f, .05f};
    Check(options.Profile(7).startShot == 2 && options.Profile(16).startShot == 1, "per-weapon selection and fallback");
    auto invalid = options;
    invalid.worldDarkness = std::numeric_limits<float>::quiet_NaN();
    Check(!combat::Valid(invalid), "reject nonfinite contrast");
    invalid = options;
    invalid.weapons[0].curve = 3;
    Check(!combat::Valid(invalid), "reject invalid response");
    options.recoil = 1;
    Check(options.EnabledFor(7) && options.EnabledFor(16) && options.EnabledFor(17) && !options.EnabledFor(1) &&
              !options.EnabledFor(9) && !options.EnabledFor(25) && !options.EnabledFor(42),
          "recoil defaults allow rifles and SMGs only");
    options.weapons[combat::WeaponProfile(7)].activation = 2;
    options.weapons[combat::WeaponProfile(1)].activation = 1;
    Check(!options.EnabledFor(7) && options.EnabledFor(1), "individual recoil activation overrides weapon group");
    options.recoil = 0;
    Check(!options.EnabledFor(1), "global recoil off overrides explicit per-gun enable");
    invalid = options;
    invalid.weapons[1].activation = 3;
    Check(!combat::Valid(invalid), "invalid per-weapon recoil activation rejected");
    combat::Recoil filter;
    combat::RecoilProfile profile;
    profile.smoothing = 0;
    combat::RecoilSample sample{1, 4, 7, 0, {}, 0, 1, 1, true};
    Check(filter.Update(sample, profile, .016f, true).x == 0, "recoil baseline does not move view");
    sample.shots = 1;
    sample.punch = {2, 1, 0};
    sample.gameTime = sample.lastShot = 1.02f;
    auto correction = filter.Update(sample, profile, .016f, true);
    Check(correction.x == 2 && correction.y == 1, "counter evaluated punch delta");
    Check(filter.Update(sample, profile, .016f, true).x == 0, "same sample does not compensate twice");
    sample.weaponHandle = 5;
    sample.punch = {10, 2, 0};
    Check(filter.Update(sample, profile, .016f, true).x == 0, "weapon switch rebases recoil");
    sample.gameTime = 2;
    sample.punch = {0, 0, 0};
    Check(filter.Update(sample, profile, .016f, true).x == 0, "recoil recovery after firing does not drag view");
    sample.valid = false;
    Check(filter.Update(sample, profile, .016f, true).x == 0, "invalid samples stop recoil");
    sample = {1, 4, 7, 0, {}, 0, 3, 3, true};
    profile.smoothing = .04f;
    filter.Update(sample, profile, .016f, true);
    sample.shots = 2;
    sample.punch = {3, -2, 0};
    sample.lastShot = sample.gameTime = 3.01f;
    Vector3 total{};
    for (int i = 0; i < 20; ++i)
        total = total + filter.Update(sample, profile, .016f, true);
    Check(std::abs(total.x - 3) < .01f && std::abs(total.y + 2) < .01f, "smoothed recoil converges without overshoot");
    Check(filter.Update(sample, profile, .016f, false).x == 0, "focus/menu gating resets compensation");
    sample.valid = true;
    sample.shots = 3;
    sample.punch = {10, 2, 0};
    Check(filter.Update(sample, profile, 1, true).x == 0,
          "long stalls clear pending recoil instead of applying an accumulated camera jump");
    filter.Reset();
    profile.smoothing = .1f;
    sample = {1, 4, 7, 0, {}, 0, 10, 10, true};
    filter.Update(sample, profile, .004f, true);
    sample.shots = 1;
    sample.punch = {5, 2, 0};
    sample.lastShot = sample.gameTime = 10.01f;
    const auto partial = filter.Update(sample, profile, .004f, true);
    Check(partial.x > 0 && partial.x < 5, "smooth recoil leaves a bounded pending correction");
    sample.weaponHandle = 6;
    sample.weapon = 17;
    Check(filter.Update(sample, profile, .004f, true).x == 0 && filter.Update(sample, profile, .004f, true).x == 0,
          "weapon change clears unconsumed correction before another firearm can inherit it");
    sample.owner = 2;
    sample.punch = {9, 3, 0};
    Check(filter.Update(sample, profile, .004f, true).x == 0,
          "respawn ownership change cannot replay correction from the previous pawn");
    combat::Feedback feedback;
    for (int i = 0; i < 40; ++i)
        feedback.Add(2, {0, 0, .5f}, i + 1, false, i);
    Check(feedback.hits.count == 32 && feedback.serial == 40 && feedback.hits[0].damage == 9,
          "bounded hit history keeps newest events");
    feedback.Add(2, {}, 0, false, 41);
    Check(feedback.serial == 40, "non-damaging events cannot trigger hits");
    feedback.Clear();
    Check(!feedback.hits.count && feedback.serial == 40, "round clear preserves monotonic sound serial");
    {
        Fixture hitFixture;
        constexpr auto victim = Fixture::base + 0x40000, scene = Fixture::base + 0x60000,
                       bounds = Fixture::base + 0x68000, bones = Fixture::base + 0x70000;
        hitFixture.Entity(4, victim, "cs_player_pawn");
        hitFixture.Put(victim + offsets::SceneNode, scene);
        hitFixture.Put(scene + offsets::Origin, Vector3{100, 200, 20});
        hitFixture.Put(victim + offsets::Collision, bounds);
        hitFixture.Put(bounds + offsets::Mins, Vector3{-16, -16, 0});
        hitFixture.Put(bounds + offsets::Maxs, Vector3{16, 16, 54});
        Vector3 head, chest, leg;
        Check(ReadHitPosition(hitFixture.memory, victim, 0x80004u, 1, head) &&
                  ReadHitPosition(hitFixture.memory, victim, 0x80004u, 2, chest) &&
                  ReadHitPosition(hitFixture.memory, victim, 0x80004u, 6, leg) && head.z > chest.z && chest.z > leg.z &&
                  head.z < 74,
              "missing bones fall back to the hit region within crouched bounds");
        hitFixture.Put(scene + offsets::ModelState + offsets::BoneArray, bones);
        hitFixture.Put(scene + offsets::ModelState + offsets::BoneCount, std::uint16_t{25});
        hitFixture.Put(bones + offsets::BoneHead * offsets::BoneStride, BoneTransform{{105, 205, 69}, 1, {0, 0, 0, 1}});
        hitFixture.Put(victim + offsets::Health, 0);
        Check(ReadHitPosition(hitFixture.memory, victim, 0x80004u, 1, head) && head.x == 105 && head.z == 69,
              "headshot uses the event-time bone even on lethal damage");
        combat::Feedback frozenHit;
        frozenHit.Add(0x80004u, head, 100, true, 1);
        hitFixture.Put(scene + offsets::Origin, Vector3{500, 500, 0});
        Check(frozenHit.hits[0].position.x == 105, "moving victim cannot move a captured hit anchor");
        Check(!ReadHitPosition(hitFixture.memory, victim, 0x100004u, 1, head),
              "recycled victim handle cannot create a misplaced marker");
    }
    auto history = std::make_unique<combat::GhostHistory>();
    PreviewPose pose;
    pose.valid = true;
    pose.entity.valid = 1;
    pose.entity.id = 2;
    pose.entity.health = pose.entity.maxHealth = 100;
    for (int i = 0; i < 15; ++i) {
        history->Begin();
        pose.entity.origin.x = i * 10.f;
        history->Add(2, pose, 1 + i * .11, 1);
        history->End();
    }
    Check(history->Tracks()[0].points.count == 15, "time history retains the full delayed interval");
    Check(history->Tracks()[0].points[14].speed > 80, "motion speed follows timestamped displacement");
    history->Begin();
    pose.entity.origin.x = 1000;
    history->Add(2, pose, 2.7, 1);
    history->End();
    Check(history->Tracks()[0].points.count == 1, "teleports break ghost trails");
    history->Begin();
    history->End();
    Check(!history->Tracks()[0].handle, "missing entities clear history");
    Check(AwarenessTeam(2, 3, 1) && !AwarenessTeam(3, 3, 1) && AwarenessTeam(3, 3, 2) && AwarenessTeam(2, 3, 0),
          "awareness all/opponent/teammate selection");
    Check(AwarenessTeam(2, 0, 3) && AwarenessTeam(3, 0, 4) && !AwarenessTeam(2, 0, 1),
          "awareness explicit T/CT and unknown local team");
    combat::GhostTrack replay;
    replay.points.Push({pose, 10, 10});
    replay.current = {pose, 10.1, 10};
    replay.points[0].pose.entity.origin = {0, 0, 0};
    replay.current.pose.entity.origin = {10, 0, 0};
    replay.points[0].pose.joints[0].position = {0, 0, 0};
    replay.current.pose.joints[0].position = {10, 0, 0};
    combat::GhostPoint echo;
    Check(combat::ReplayPose(replay, 10.05, echo) && std::abs(echo.pose.entity.origin.x - 5) < .001f &&
              std::abs(echo.pose.joints[0].position.x - 5) < .001f,
          "ghost interpolates body and joints at requested replay time");
    Check(!combat::ReplayPose(replay, 9.9, echo) && !combat::ReplayPose(replay, 10.2, echo),
          "ghost never extrapolates outside live history");
    profile.smoothing = 0;
    sample = {1, 4, 7, 0, {}, 0, 100, 100, true};
    filter.Reset();
    filter.Update(sample, profile, .01f, true);
    sample.shots = 1;
    sample.lastShot = 90;
    sample.gameTime = 100.01f;
    sample.punch = {2, 1, 0};
    correction = filter.Update(sample, profile, .01f, true);
    Check(correction.x == 2, "shot transitions compensate even when shot and view clocks differ");
    filter.Restore(correction);
    Check(filter.Update(sample, profile, 0, true).x == 0, "zero-time update waits without consuming correction");
    Check(filter.Update(sample, profile, .01f, true).x == 2, "failed angle commit retains compensation for retry");
    const auto shotDirection = combat::ShotAngles({10, 20}, {-3, 2, 0});
    const auto compensated = combat::CompensatedAngles(shotDirection, {-3, 2, 0});
    Check(shotDirection.pitch == 13 && shotDirection.yaw == 22 && compensated.pitch == 10 && compensated.yaw == 20,
          "tracking and recoil use inverse angle transforms without cancelling correction");
    history->Clear();
    pose.entity.origin = {10, 20, 0};
    for (int i = 0; i <= 64; ++i) {
        pose.joints[0].position.x = float(i);
        history->Begin();
        history->Add(2, pose, 20 + i / 32., 1);
        history->End();
    }
    auto replayFrame = std::make_unique<combat::ReplayFrame>();
    combat::BuildReplay(*history, 22, 1, *replayFrame);
    Check(replayFrame->count == 1 && !replayFrame->actors[0].ready,
          "stationary players hide their replay while retaining history");
    pose.entity.origin.x += 2;
    history->Begin();
    history->Add(2, pose, 22.04, .05f);
    history->End();
    combat::BuildReplay(*history, 22.04, .05f, *replayFrame);
    Check(replayFrame->actors[0].ready && replayFrame->actors[0].movement > .4f,
          "nearby historical pose appears immediately when movement resumes");
    history->Begin();
    history->Add(2, pose, 22.08, .05f);
    history->End();
    combat::BuildReplay(*history, 22.08, .05f, *replayFrame);
    Check(replayFrame->actors[0].ready && replayFrame->actors[0].movement < .6f,
          "replay fades smoothly after root movement stops instead of popping away");
    combat::BuildReplay(*history, 23, 1, *replayFrame);
    Check(!replayFrame->count, "stale replay actors disappear");
    Fixture f;
    const auto bomb = Fixture::base + 0x40000, fire = Fixture::base + 0x50000, smoke = Fixture::base + 0x60000,
               pawn = Fixture::base + 0x70000;
    f.Entity(1, bomb, "planted_c4");
    f.Entity(2, fire, "inferno");
    f.Entity(3, smoke, "smokegrenade_projectile");
    f.Entity(4, pawn, "cs_player_pawn");
    f.Put(bomb + offsets::BombTicking, std::uint8_t{1});
    f.Put(bomb + offsets::BombBlow, 150.f);
    f.Put(bomb + offsets::BombLength, 40.f);
    f.Put(bomb + offsets::BombSite, 1);
    const auto items = Fixture::base + 0xe0000;
    f.Put(pawn + offsets::ItemServices, items);
    f.Put(items + offsets::HasDefuser, std::uint8_t{1});
    combat::Bomb b;
    Check(ReadBomb(f.memory, Fixture::list, bomb, pawn, 120, 0x80001, b) && b.remaining == 30 && b.hasKit &&
              b.defuseLength == 5 && b.site == 1,
          "bomb countdown and local kit read");
    f.Put(bomb + offsets::BombDefusing, std::uint8_t{1});
    f.Put(bomb + offsets::DefuseLength, 5.f);
    f.Put(bomb + offsets::DefuseCountdown, 124.f);
    f.Put(bomb + offsets::BombDefuser, 0x80004u);
    Check(ReadBomb(f.memory, Fixture::list, bomb, pawn, 120, 0x80001, b) && b.defusing && b.defuseRemaining == 4,
          "active defuse countdown");
    f.Put(bomb + offsets::BombDefused, std::uint8_t{1});
    Check(!ReadBomb(f.memory, Fixture::list, bomb, pawn, 120, 0x80001, b), "finished bomb disappears");
    f.Put(bomb + offsets::BombDefused, std::uint8_t{});
    Check(!ReadBomb(f.memory, Fixture::list, bomb, pawn, 120, 0x100001, b), "reused bomb identity rejected");
    f.Put(fire + offsets::FireStartTick, 118 * 64);
    f.Put(fire + offsets::FireLifetime, 7.f);
    f.Put(smoke + offsets::SmokeStartTick, 110 * 64);
    const auto fireTimer = ReadAreaTimer(f.memory, fire, combat::AreaType::Fire, 120);
    const auto smokeTimer = ReadAreaTimer(f.memory, smoke, combat::AreaType::Smoke, 120);
    Check(fireTimer.Valid() && fireTimer.remaining == 5 && fireTimer.duration == 7 && !fireTimer.estimated,
          "fire countdown follows the engine start and lifetime");
    Check(smokeTimer.Valid() && smokeTimer.remaining == 8 && smokeTimer.estimated,
          "smoke timer clearly marks its standard lifetime estimate");
    Check(!ReadAreaTimer(f.memory, fire, combat::AreaType::Fire, 100).Valid() &&
              !ReadAreaTimer(f.memory, fire, combat::AreaType::Fire, 125).Valid() &&
              !ReadAreaTimer(f.memory, fire, combat::AreaType::Fire, std::numeric_limits<float>::quiet_NaN()).Valid(),
          "future, expired and unavailable clocks never fabricate countdowns");
    f.Put(fire + offsets::FireLifetime, std::numeric_limits<float>::infinity());
    Check(!ReadAreaTimer(f.memory, fire, combat::AreaType::Fire, 120).Valid(), "corrupt lifetime rejected");
    f.Put(fire + offsets::FireLifetime, 7.f);
    f.Put(fire + offsets::FireCount, 2);
    std::array<Vector3, 64> spots{};
    spots[0] = {100, 200, 0};
    spots[1] = {140, 200, 0};
    f.Put(fire + offsets::FirePositions, spots);
    std::array<std::uint8_t, 64> active{};
    active[0] = active[1] = 1;
    f.Put(fire + offsets::FireBurning, active);
    f.Put(smoke + offsets::SmokeEffect, std::uint8_t{1});
    f.Put(smoke + offsets::SmokeCenter, Vector3{600, 700, 0});
    Check(ParticleType("particles/weapons/cs_weapon_fx/molotov.vpcf") == 1 &&
              ParticleType("particles/weapons/smokegrenade.vpcf") == 2 &&
              ParticleType("particles/ui/fire_badge.vpcf") == 0,
          "particle filter excludes unrelated fire and UI effects");
    model::Packet packet;
    packet.Set(offsets::PacketColor, std::uint32_t{0x804080C8});
    TintWorldPacket(packet, .5f);
    Check(packet.Get<std::uint32_t>(offsets::PacketColor) == 0x80204064, "world tint scales RGB and preserves alpha");
    const auto collection = Fixture::base + 0x90000, binding = Fixture::base + 0xa0000,
               descriptor = Fixture::base + 0xb0000, name = Fixture::base + 0xc0000;
    f.Put(collection + trajectory_offsets::CollectionResource, binding);
    f.Put(binding + trajectory_offsets::ResourceDescriptor, descriptor);
    f.Put(descriptor, name);
    std::strcpy(reinterpret_cast<char *>(f.data.data() + name - Fixture::base), "particles/weapons/smokegrenade.vpcf");
    char particleName[260]{};
    Check(ParticleResourceName(f.memory, collection, particleName) && ParticleType(particleName) == 2,
          "resource name follows validated indirection");
    WorldReader reader;
    combat::WorldSnapshot world;
    Check(reader.Update(f.memory, Fixture::list, pawn, 120, 1, world) && world.bomb.valid && world.areaCount == 2,
          "world reader finds objective, fire and smoke");
    const auto fireArea = std::find_if(world.areas.begin(), world.areas.begin() + world.areaCount,
                                       [](auto &a) { return a.type == combat::AreaType::Fire; });
    Check(fireArea != world.areas.begin() + world.areaCount && fireArea->radius == 45 && fireArea->center.x == 120,
          "fire proxy derived from live flame positions");
    Check(fireArea->timer.Valid() && fireArea->timer.remaining == 5, "world publication carries native fire countdown");
    reader.Update(f.memory, Fixture::list, pawn, 140, 1.002, world);
    Check(world.areaCount == 2 && !world.areas[0].timer.Valid() && !world.areas[1].timer.Valid(),
          "expired timer never hides still-active fire or smoke footprints");
    active[0] = 0xff;
    active[1] = 2;
    f.Put(fire + offsets::FireBurning, active);
    reader.Update(f.memory, Fixture::list, pawn, 120, 1.005, world);
    Check(world.areaCount == 2 && world.fireEntities == 1 && world.burningCells == 2 && world.fireReadFailures == 0,
          "fire reader matches engine nonzero burning flags");
    f.Put(fire + offsets::FireCount, 65);
    f.Put(smoke + offsets::SmokeEffect, std::uint8_t{});
    reader.Update(f.memory, Fixture::list, pawn, 121, 1.01, world);
    Check(!world.areaCount && world.bomb.remaining == 29,
          "cached entries refresh countdown and reject inactive effects");
    f.Put(fire + offsets::FireCount, 2);
    reader.Update(f.memory, Fixture::list, pawn, std::numeric_limits<float>::quiet_NaN(), 1.02, world);
    Check(world.areaCount == 1 && world.areas[0].boundaryCount >= 8 && !world.bomb.valid,
          "fire footprint survives unavailable bomb clock");
    combat::InfernoEvents fireEvents;
    fireEvents.Update(2, 0x80002u, {120, 40, 0}, 5, true);
    combat::WorldSnapshot eventWorld;
    combat::AppendInfernoEvents(fireEvents, 5.1, eventWorld);
    Check(eventWorld.areaCount == 1 && eventWorld.areas[0].center.x == 120 && eventWorld.areas[0].radius > 42 &&
              eventWorld.areas[0].estimatedFootprint && !eventWorld.areas[0].cellCount,
          "fire start immediately provides an explicit event-only ground footprint");
    combat::AppendInfernoEvents(fireEvents, 5.2, eventWorld);
    Check(eventWorld.areaCount == 1, "event footprint cannot duplicate a cell-derived inferno");
    f.Entity(2, fire, "unavailable_designer_name");
    WorldReader eventReader;
    eventReader.Update(f.memory, Fixture::list, pawn, 120, 5.1, eventWorld, fireEvents);
    Check(eventWorld.areaCount >= 1 && eventWorld.burningCells == 2 && !eventWorld.areas[0].estimatedFootprint &&
              eventWorld.areas[0].cellCount == 2,
          "fire event seeds exact cells which replace the event-only estimate");
    fireEvents.Update(2, 0, {}, 5.3, false);
    eventWorld = {};
    combat::AppendInfernoEvents(fireEvents, 5.4, eventWorld);
    Check(eventWorld.areaCount == 0, "extinguish event removes its fallback immediately");
    fireEvents.Update(2, 0x80002u, {120, 40, 0}, 5, true);
    combat::AppendInfernoEvents(fireEvents, 12, eventWorld);
    Check(eventWorld.areaCount == 0, "missing expiry event cannot leave a permanent fire footprint");
    const auto effect = Fixture::base + 0xd0000, gun = Fixture::base + 0xc0000;
    f.Entity(5, gun, "weapon_ak47");
    f.Put(gun + offsets::EntityOwner, 0x80004u);
    f.Put(effect + offsets::EffectStart, Vector3{1, 2, 3});
    f.Put(effect + offsets::EffectOrigin, Vector3{100, 20, 30});
    f.Put(effect + offsets::EffectEntity, 0x80005u);
    f.Put(pawn + offsets::Team, std::uint8_t{3});
    flight::Shot traced;
    Check(ReadTracerEffect(f.memory, Fixture::list, effect, traced) && traced.shooter == 4 && traced.team == 3 &&
              traced.end.x == 100,
          "actual tracer endpoints resolve weapon owner and team without fire event");
    const Vector3 muzzle{10, 11, 12};
    Check(ReadTracerEffect(f.memory, Fixture::list, effect, traced, &muzzle) && traced.start.x == 10 &&
              traced.start.y == 11 && traced.start.z == 12 && traced.end.x == 100,
          "native attachment position replaces raw tracer start without changing impact");
    f.Put(effect + offsets::EffectEntity, 0x100005u);
    Check(!ReadTracerEffect(f.memory, Fixture::list, effect, traced), "stale tracer serial rejected");
    f.Put(effect + offsets::EffectEntity, 0x80004u);
    Check(ReadTracerEffect(f.memory, Fixture::list, effect, traced), "direct pawn tracer owner accepted");
    flight::Tracers deduplicated;
    traced.time = 1;
    deduplicated.Add(traced);
    traced.time = 1.001;
    deduplicated.Add(traced);
    Check(deduplicated.Lines().count == 1, "client and impact callbacks cannot duplicate a trace");
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {800, 600};
    io.DeltaTime = .016f;
    io.Fonts->AddFontDefault();
    unsigned char *pixels{};
    int width{}, height{};
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    auto &draw = *ImGui::GetBackgroundDrawList();
    FrameSnapshot frame;
    frame.viewport = {0, 0, 800, 600};
    frame.viewProjection = Matrix4x4::Identity();
    Configuration config;
    config.enabled = 1;
    options = {};
    options.hitMarker = options.damageNumbers = 1;
    feedback.Clear();
    feedback.Add(2, {0, 0, .5f}, 37, true, 10);
    combat::Draw(draw, io.Fonts->Fonts[0], frame, frame.viewport, config, options, {}, feedback, *replayFrame, 10.1);
    Check(draw.VtxBuffer.Size > 0, "hitmarker and damage produce draw geometry");
    auto before = draw.VtxBuffer.Size;
    combat::Draw(draw, io.Fonts->Fonts[0], frame, frame.viewport, config, options, {}, feedback, *replayFrame, 15);
    Check(draw.VtxBuffer.Size == before, "expired hit feedback disappears");
    options = {};
    options.hitMarker = 1;
    Check(combat::MarkerOpacity(.1, options) == 1 &&
              std::abs(combat::MarkerOpacity(options.markerHold + options.markerDuration * .5, options) - .5f) < .0001f,
          "marker holds fully visible then follows a smooth half-opacity fade");
    Check(combat::MarkerOpacity(-.1, options) == 0 && combat::MarkerOpacity(10, options) == 0 &&
              combat::MarkerOpacity(std::numeric_limits<double>::quiet_NaN(), options) == 0,
          "future, expired and invalid marker times draw nothing");
    feedback.Clear();
    feedback.Add(2, {.5f, .2f, .5f}, 20, false, 20);
    feedback.Add(2, {-.5f, .2f, .5f}, 30, false, 20.1);
    before = draw.VtxBuffer.Size;
    combat::Draw(draw, io.Fonts->Fonts[0], frame, frame.viewport, config, options, {}, feedback, *replayFrame, 20.2);
    bool left{}, right{}, centered{};
    for (int i = before; i < draw.VtxBuffer.Size; ++i) {
        left |= draw.VtxBuffer[i].pos.x < 220;
        right |= draw.VtxBuffer[i].pos.x > 580;
        centered |= std::abs(draw.VtxBuffer[i].pos.x - 400) < 100;
    }
    Check(left && right && !centered, "successive hits draw at both world anchors and never at screen center");
    const auto boundsOfMarker = [&](const Matrix4x4 &matrix, double now) {
        const int first = draw.VtxBuffer.Size;
        combat::DrawHitMarker(draw, feedback.hits[0], matrix, frame.viewport, options, 1, now);
        std::array<float, 3> result{10000, -10000, 0};
        for (int i = first; i < draw.VtxBuffer.Size; ++i) {
            result[0] = std::min(result[0], draw.VtxBuffer[i].pos.x);
            result[1] = std::max(result[1], draw.VtxBuffer[i].pos.x);
            result[2] = std::max(result[2], static_cast<float>((draw.VtxBuffer[i].col >> IM_COL32_A_SHIFT) & 255));
        }
        return result;
    };
    const auto held = boundsOfMarker(frame.viewProjection, 20.1);
    const auto fading = boundsOfMarker(frame.viewProjection, 20 + options.markerHold + options.markerDuration * .5);
    Check(held[0] == fading[0] && held[1] == fading[1] && fading[2] > 0 && fading[2] < held[2],
          "marker stays in place and keeps its size while fading");
    auto movedCamera = frame.viewProjection;
    movedCamera.m[0][3] = .25f;
    const auto shifted = boundsOfMarker(movedCamera, 20.1);
    Check(std::abs(shifted[0] - held[0] - 100) < .01f, "camera movement reprojects the frozen world anchor");
    before = draw.VtxBuffer.Size;
    combat::Draw(draw, io.Fonts->Fonts[0], frame, frame.viewport, config, options, {}, feedback, *replayFrame, 20.75);
    bool newerOnly = draw.VtxBuffer.Size > before;
    for (int i = before; i < draw.VtxBuffer.Size; ++i)
        newerOnly &= draw.VtxBuffer[i].pos.x < 220;
    Check(newerOnly, "older hit expires without clearing a newer marker on the same target");
    combat::Hit hidden{1, 2, {0, 0, -.5f}, 20, 20, false}, offscreen{2, 2, {10000, 0, .5f}, 20, 20, false};
    before = draw.VtxBuffer.Size;
    Check(!combat::DrawHitMarker(draw, hidden, frame.viewProjection, frame.viewport, options, 1, 20.1) &&
              !combat::DrawHitMarker(draw, offscreen, frame.viewProjection, frame.viewport, options, 1, 20.1) &&
              draw.VtxBuffer.Size == before,
          "behind-camera and far-offscreen hits never produce giant screen marks");
    options = {};
    options.bombTimer = 1;
    world.bomb = {};
    world.bomb.valid = true;
    world.bomb.remaining = 8;
    combat::Draw(draw, io.Fonts->Fonts[0], frame, frame.viewport, config, options, world, {}, *replayFrame, 15);
    Check(draw.VtxBuffer.Size > before, "objective timer renders independently");
    combat::Area area{1, combat::AreaType::Smoke, {0, 0, .5f}, .3f, .2f};
    before = draw.VtxBuffer.Size;
    combat::AreaShape(draw, area, options.smokeColor, frame.viewProjection, frame.viewport, 1);
    Check(draw.VtxBuffer.Size > before + 100, "smoke volume emits ellipse and cylinder geometry");
    before = draw.VtxBuffer.Size;
    const std::array<Vector3, 4> crossing{{{-.5f, -.5f, -.1f}, {.5f, -.5f, .2f}, {.5f, .5f, .2f}, {-.5f, .5f, -.1f}}};
    combat::AreaFill(draw, crossing, {0, 0, 1, .5f}, frame.viewProjection, frame.viewport, 1);
    Check(draw.VtxBuffer.Size > before, "area fill remains when its boundary crosses near plane");
    for (int i = before; i < draw.VtxBuffer.Size; ++i)
        Check(std::abs(draw.VtxBuffer[i].pos.x) < 1000 && std::abs(draw.VtxBuffer[i].pos.y) < 1000,
              "clipped area vertices stay near viewport");
    before = draw.VtxBuffer.Size;
    combat::AreaShape(draw, area, options.fireColor, frame.viewProjection, frame.viewport, 1, 0, false, false);
    Check(draw.VtxBuffer.Size == before, "disabled area fill and border skip all geometry");
    options = {};
    options.utilityTimers = 1;
    world.Clear();
    world.areaCount = 1;
    world.areas[0] = {1, combat::AreaType::Fire, {0, 0, .5f}, .3f, .2f};
    world.areas[0].timer = {5, 7, false};
    before = draw.VtxBuffer.Size;
    Check(combat::DrawUtilityTimers(draw, io.Fonts->Fonts[0], frame, frame.viewport, config, options, world) == 1 &&
              draw.VtxBuffer.Size > before,
          "countdown renders while area fill is off");
    world.areas[0].timer.remaining = 0;
    before = draw.VtxBuffer.Size;
    Check(!combat::DrawUtilityTimers(draw, io.Fonts->Fonts[0], frame, frame.viewport, config, options, world) &&
              draw.VtxBuffer.Size == before,
          "expired countdown emits no geometry");
    world.areas[0].timer.remaining = 5;
    options.timerFire = 0;
    Check(!combat::DrawUtilityTimers(draw, io.Fonts->Fonts[0], frame, frame.viewport, config, options, world),
          "fire timer can be independently disabled");
    options.timerFire = 1;
    frame.cameraOrigin = {1000, 0, 0};
    Check(!combat::DrawUtilityTimers(draw, io.Fonts->Fonts[0], frame, frame.viewport, config, options, world),
          "utility timer range applies before projection");
    frame.cameraOrigin = {};
    world.areas[0].center.z = -.5f;
    Check(!combat::DrawUtilityTimers(draw, io.Fonts->Fonts[0], frame, frame.viewport, config, options, world),
          "behind-camera timer emits no giant marker");
    options = {};
    options.hitLog = 1;
    options.hitLogRows = 2;
    feedback.Clear();
    char captured[] = "Rival\nPlayer";
    feedback.Add(8, {0, 0, .5f}, 42, true, 50, captured);
    captured[0] = 'X';
    Check(!std::strcmp(feedback.hits[0].name, "Rival Player"), "hit name is frozen and control characters removed");
    const std::string longName = std::string(62, 'a') + "\xE7\x8C\xAB";
    feedback.Add(9, {0, 0, .5f}, 10, false, 50.1, longName);
    Check(std::strlen(feedback.hits[1].name) == 62, "truncating player name retains complete UTF8 characters");
    feedback.Add(10, {0, 0, .5f}, 5, false, 50.2, "Third");
    Check(combat::DrawHitFeed(draw, io.Fonts->Fonts[0], feedback, frame.viewport, options, 1, 50.5) == 2,
          "hit feed renders only configured newest rows without world markers");
    options.hitLogY = 1;
    Check(combat::DrawHitFeed(draw, io.Fonts->Fonts[0], feedback, frame.viewport, options, 1, 50.5) == 2,
          "bottom placement preserves the entire feed block");
    options.hitLogScale = 1.5f;
    Check(!combat::DrawHitFeed(draw, io.Fonts->Fonts[0], feedback, {0, 0, 800, 48}, options, 1, 50.5) &&
              !combat::DrawHitFeed(draw, io.Fonts->Fonts[0], feedback, {0, 0, 64, 600}, options, 1, 50.5),
          "small viewports skip feed without inverted clamp or clipping bounds");
    before = draw.VtxBuffer.Size;
    Check(!combat::DrawHitFeed(draw, io.Fonts->Fonts[0], feedback, frame.viewport, options, 1, 60) &&
              draw.VtxBuffer.Size == before,
          "hit feed expires independently");
    Check(combat::HitLogOpacity(-1, 3) == 0 && combat::HitLogOpacity(4, 3) == 0 && combat::HitLogOpacity(.2, 3) == 1 &&
              combat::HitLogOpacity(2.9, 3) < .5f &&
              combat::HitLogOpacity(std::numeric_limits<double>::quiet_NaN(), 3) == 0,
          "hit feed animation rejects invalid clocks and fades at expiry");
    options.hitLogRows = 9;
    Check(!combat::Valid(options), "unbounded feed capacity rejected");
    options = {};
    options.timerScale = std::numeric_limits<float>::quiet_NaN();
    Check(!combat::Valid(options), "nonfinite timer geometry rejected");
    bool finite = true;
    for (const auto &v : draw.VtxBuffer)
        finite &= std::isfinite(v.pos.x) && std::isfinite(v.pos.y);
    Check(finite, "new HUD geometry remains finite");
    ImGui::EndFrame();
    ImGui::DestroyContext();
    std::printf("Combat checks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
