#include "assist_features.hpp"
#include "assist_reader.hpp"
#include "assist_input_state.hpp"
#include "assist_timing.hpp"
#include <vector>
#include <cstdio>
#include <limits>
using namespace awareness;
using namespace awareness::assist;
namespace {
unsigned failures{}, checks{};
void Check(bool result, const char *message) {
    ++checks;
    if (!result) {
        ++failures;
        std::printf("FAIL: %s\n", message);
    }
}
struct Event {
    unsigned key;
    bool down;
};
struct Fake {
    std::vector<Event> events;
    bool fail{}, failJumpRestore{};
    unsigned turns{};
    float yaw{};
    YawResult yawResult{YawResult::Applied};
    bool Key(unsigned key, bool down) {
        if (fail)
            return false;
        events.push_back({key, down});
        return true;
    }
    bool Mouse(bool down) { return Key(LeftMouse, down); }
    bool Pistol(bool down) { return Key(LeftMouse, down); }
    bool RestorePrimary() { return Key(LeftMouse, true); }
    bool RestoreJump() { return !failJumpRestore && Key(Space, true); }
    YawResult Yaw(const Sample &, float value) {
        if (fail)
            return YawResult::Failed;
        if (yawResult == YawResult::Applied) {
            ++turns;
            yaw = value;
        }
        return yawResult;
    }
    unsigned Count(unsigned key, bool down) const {
        unsigned n{};
        for (auto e : events)
            if (e.key == key && e.down == down)
                ++n;
        return n;
    }
};
Sample Player() {
    Sample s;
    s.valid = s.walking = s.enemy = s.weaponReady = s.weaponKnown = s.readinessKnown = true;
    s.movementKnown = s.velocityKnown = s.anglesKnown = true;
    s.owner = 1;
    s.weaponHandle = 7;
    s.weapon = 7;
    s.target = 5;
    s.velocity = {250, 0, 0};
    s.maxSpeed = 250;
    s.friction = 1;
    s.grounded = true;
    return s;
}
struct Fixture {
    static constexpr std::uintptr_t base = 0x10000000, list = base + 0x1000, controller = base + 0x30000,
                                    pawn = base + 0x40000, enemy = base + 0x48000, weapon = base + 0x60000,
                                    services = base + 0x80000, movement = base + 0x81000;
    std::vector<unsigned char> bytes = std::vector<unsigned char>(0x100000);
    Addresses addresses{base + 0x100, base + 0x108, base + 0x110, base + 0x200};
    cs2::Memory memory{this, Read};
    unsigned combatReads{};
    static bool Read(void *self, std::uintptr_t address, void *out, std::size_t size) noexcept {
        auto &f = *static_cast<Fixture *>(self);
        if (address == pawn + cs2::offsets::WeaponServices || address == pawn + cs2::offsets::ShotsFired ||
            address == pawn + cs2::offsets::CrosshairIndex)
            ++f.combatReads;
        if (address < base || address - base > f.bytes.size() || size > f.bytes.size() - (address - base))
            return false;
        std::memcpy(out, f.bytes.data() + address - base, size);
        return true;
    }
    template <class T> void Put(std::uintptr_t at, T value) {
        std::memcpy(bytes.data() + at - base, &value, sizeof(value));
    }
    void Entity(unsigned slot, std::uintptr_t object) {
        auto entry = base + 0x10000 + slot * cs2::offsets::EntityStride;
        Put(entry, object);
        Put(object + cs2::offsets::Identity, entry);
        Put(entry + 0x10, 0x80000u + slot);
    }
    Fixture() {
        using namespace cs2;
        Put(addresses.listSlot, list);
        Put(addresses.pawnSlot, pawn);
        Put(addresses.controllerSlot, controller);
        Put(addresses.angles, NativeViewAngles{});
        Put(list + offsets::EntityTable, base + 0x10000);
        Entity(1, controller);
        Entity(3, pawn);
        Entity(5, enemy);
        Entity(7, weapon);
        Put(controller + offsets::ControllerPawn, 0x80003u);
        unsigned i{};
        for (auto p : {pawn, enemy}) {
            const auto scene = base + 0x84000 + i * 0x1000, name = base + 0x86000 + i * 0x1000;
            auto entry = base + 0x10000 + (i ? 5 : 3) * offsets::EntityStride;
            std::array<char, 32> type{};
            std::memcpy(type.data(), "cs_player_pawn", sizeof("cs_player_pawn"));
            Put(entry + offsets::DesignerName, name);
            Put(name, type);
            Put(p + offsets::Health, std::int32_t{100});
            Put(p + offsets::LifeState, std::uint8_t{0});
            Put(p + offsets::Team, static_cast<std::uint8_t>(i ? 3 : 2));
            Put(p + offsets::SceneNode, scene);
            Put(scene + offsets::Dormant, std::uint8_t{0});
            Put(p + offsets::SpawnImmunity, std::uint8_t{0});
            ++i;
        }
        Put(pawn + offsets::MovementFlags, 1u);
        Put(pawn + offsets::ActualMoveType, std::uint8_t{2});
        Put(pawn + offsets::WaterLevel, 0.f);
        Put(pawn + offsets::AbsVelocity, Vector3{250, 0, 0});
        Put(pawn + offsets::MovementServices, movement);
        Put(movement + offsets::MovementMaxSpeed, 250.f);
        Put(movement + offsets::MovementFriction, 1.f);
        Put(pawn + offsets::WeaponServices, services);
        Put(services + offsets::ActiveWeapon, 0x80007u);
        Put(weapon + offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, std::uint16_t{7});
        Put(weapon + offsets::Clip1, std::int32_t{30});
        Put(weapon + offsets::WeaponReload, std::uint8_t{0});
        Put(pawn + offsets::WaitForNoAttack, std::uint8_t{0});
        Put(pawn + offsets::IsScoped, std::uint8_t{0});
        Put(controller + offsets::ControllerTick, 1001u);
        Put(weapon + offsets::NextPrimaryTick, 1000u);
        Put(pawn + offsets::CrosshairIndex, std::int32_t{5});
    }
};
} // namespace
int main() {
    Options o;
    Check(Valid(o) && !o.shoot && !o.jumper && !o.strafer, "new modules default off");
    auto invalid = o;
    invalid.turnRate = std::numeric_limits<float>::quiet_NaN();
    Check(!Valid(invalid), "NaN steering settings rejected");
    invalid = o;
    invalid.shootKey = LeftMouse;
    Check(!Valid(invalid), "generated fire cannot be its own activation key");
    {
        Fake b;
        Controller c;
        Keys k;
        auto s = Player();
        o.shoot = 1;
        o.shootMode = 0;
        k.down[Mouse4] = true;
        c.Step(o, s, k, 1, true, false, b);
        c.Step(o, s, k, 1.034, true, false, b);
        Check(b.Count(LeftMouse, true) == 0, "reaction delay waits on same target");
        c.Step(o, s, k, 1.035, true, false, b);
        Check(b.Count(LeftMouse, true) == 1, "valid enemy triggers mouse down");
        c.Step(o, s, k, 1.04, true, false, b);
        Check(b.Count(LeftMouse, true) == 1, "no repeated down while held");
        c.Step(o, s, k, 1.056, true, false, b);
        Check(b.Count(LeftMouse, false) == 1, "click releases after pulse");
        c.Step(o, s, k, 1.134, true, false, b);
        Check(b.Count(LeftMouse, true) == 1, "click interval enforced");
        c.Step(o, s, k, 1.136, true, false, b);
        Check(b.Count(LeftMouse, true) == 2, "next ready interval clicks");
        s.target = 9;
        c.Step(o, s, k, 1.14, true, false, b);
        Check(b.Count(LeftMouse, false) == 2, "target switch releases old click");
        c.Step(o, s, k, 1.16, true, false, b);
        Check(b.Count(LeftMouse, true) == 2, "new target gets own delay");
        s.enemy = false;
        c.Step(o, s, k, 1.20, true, false, b);
        Check(b.Count(LeftMouse, true) == 2, "friendly or empty crosshair never fires");
        s.enemy = true;
        s.weaponReady = false;
        c.Step(o, s, k, 1.24, true, false, b);
        c.Step(o, s, k, 1.28, true, false, b);
        Check(b.Count(LeftMouse, true) == 2, "empty/reloading/cooldown firearm does not click");
        s.weaponReady = true;
        k.down[LeftMouse] = true;
        c.Step(o, s, k, 1.32, true, false, b);
        Check(b.Count(LeftMouse, true) == 2, "physical mouse input takes priority");
        k.down[LeftMouse] = false;
        o.scopeOnly = 1;
        c.Step(o, s, k, 1.36, true, false, b);
        c.Step(o, s, k, 1.4, true, false, b);
        Check(b.Count(LeftMouse, true) == 2, "scope-only requires scoped state");
        o.scopeOnly = 0;
        c.Step(o, s, k, 1.5, true, false, b);
        c.Step(o, s, k, 1.54, true, false, b);
        b.fail = true;
        c.Step(o, s, k, 1.56, false, false, b);
        Check(c.GetStatus().failures > 0, "failed release is reported");
        b.fail = false;
        c.Step(o, s, k, 1.58, false, false, b);
        Check(b.Count(LeftMouse, false) == 3, "owned mouse release retries after failure");
    }
    {
        o = {};
        o.jumper = 1;
        Fake b;
        Controller c;
        Keys k;
        k.down[Space] = true;
        auto s = Player();
        s.grounded = false;
        s.tick = 100;
        c.Step(o, s, k, 2, true, false, b);
        Check(b.Count(Space, true) == 0, "airborne hold primes without jumping");
        s.grounded = true;
        ++s.tick;
        c.Step(o, s, k, 2.004, true, false, b);
        Check(b.Count(Space, true) == 1, "first ground sample jumps");
        c.Step(o, s, k, 2.008, true, false, b);
        Check(b.Count(Space, true) == 1, "same ground state does not spam");
        s.grounded = false;
        c.Step(o, s, k, 2.012, true, false, b);
        Check(b.Count(Space, false) == 2, "takeoff releases synthetic jump");
        s.grounded = true;
        ++s.tick;
        c.Step(o, s, k, 2.028, true, false, b);
        Check(b.Count(Space, true) == 2, "next landing immediately rearms");
        k.down[Space] = false;
        c.Step(o, s, k, 2.032, true, false, b);
        Check((c.SuppressedRepeats() & 4) == 0, "releasing physical jump disarms");
        k.down[Space] = true;
        s.walking = false;
        c.Step(o, s, k, 2.036, true, false, b);
        Check(b.Count(Space, true) == 2, "ladder/noclip never jumps");
    }
    {
        o = {};
        o.strafer = 1;
        o.preserveForward = 0;
        Fake b;
        Controller c;
        Keys k;
        k.down[A] = k.down[W] = true;
        auto s = Player();
        s.grounded = false;
        c.Step(o, s, k, 3, true, false, b);
        Check(b.Count(W, false) == 1 && b.turns == 1, "air strafe sets side-only movement and steers");
        c.Step(o, s, k, 3.004, true, false, b);
        Check(b.Count(W, false) == 1, "suppressed forward key is not resent every poll");
        s.grounded = true;
        c.Step(o, s, k, 3.008, true, false, b);
        Check(b.Count(W, true) == 1, "landing restores still-held forward key");
        s.grounded = false;
        c.Step(o, s, k, 3.012, true, false, b);
        k.down[W] = false;
        c.Step(o, s, k, 3.016, true, false, b);
        c.Stop(b, k, true);
        Check(b.Count(W, true) == 1, "released physical key is never re-pressed");
        k.down[W] = true;
        unsigned turns = b.turns;
        c.Step(o, s, k, 3.024, true, true, b);
        Check(b.turns == turns, "camera tracking has priority over strafing");
        k.down[D] = true;
        c.Step(o, s, k, 3.028, true, false, b);
        Check(b.turns == turns, "opposing strafe keys cancel assist");
        k.down[D] = false;
        c.Step(o, s, k, 3.032, true, false, b);
        const auto restores = b.Count(W, true);
        o.strafer = 0;
        c.Step(o, s, k, 3.036, false, false, b, true);
        Check(b.Count(W, true) == restores + 1 && !c.PendingRelease(),
              "disabling final assist restores physically held forward while controls are safe");
        o.strafer = 1;
        c.Step(o, s, k, 3.040, true, false, b);
        const auto focusRestores = b.Count(W, true);
        c.Step(o, s, k, 3.044, false, false, b, false);
        Check(b.Count(W, true) == focusRestores, "focus loss does not restore a key into another application");
    }
    {
        o = {};
        o.strafer = 1;
        auto s = Player();
        s.grounded = false;
        Keys k;
        k.down[A] = true;
        for (unsigned capped : {0u, 1u})
            for (float speed : {80.f, 250.f, 900.f})
                for (int side : {-1, 1})
                    for (int yaw = -180; yaw < 180; yaw += 30) {
                        o.cappedAcceleration = capped;
                        s.velocity = {speed, 0, 0};
                        s.yaw = static_cast<float>(yaw);
                        k.down[A] = side == 1;
                        k.down[D] = side == -1;
                        const float dt = .016f, limit = o.turnRate * o.strafeStrength * dt;
                        const auto plan = OptimizeStrafe(s, k, o, dt);
                        Check(plan.forwardMove == 0 && plan.sideMove == -side * s.maxSpeed,
                              "Source movement vector uses the held strafe direction");
                        Check(plan.active && std::abs(NormalizeYaw(plan.yaw - s.yaw)) <= limit + .001f,
                              "strafe turn respects rate limit");
                        Check(plan.predictedSpeed + .003f >= PredictedSpeed(s, s.yaw, side, o),
                              "direction-respecting strafe never reduces predicted speed");
                    }
        s.velocity = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        Check(!OptimizeStrafe(s, k, o, .016f).active, "invalid velocity cannot steer");
        s = Player();
        Check(!IsAirborne(s), "ground bit distinguishes airborne state");
        s.grounded = false;
        s.water = .5f;
        Check(!IsAirborne(s), "swimming is not assisted air movement");
    }
    {
        o = {};
        o.shoot = 1;
        o.delayMs = 0;
        Fake b;
        Controller c;
        Keys k;
        auto s = Player();
        c.Step(o, s, k, 10, true, false, b);
        Check(b.Count(LeftMouse, true) == 1, "always mode activates without a hidden hold key");
        o.shootMode = 2;
        c.Step(o, s, k, 10.01, true, false, b);
        Check(c.GetStatus().shoot == Status::ToggleOff && b.Count(LeftMouse, false) == 1,
              "switching to toggle mode starts inactive and releases owned fire");
        k.down[Mouse4] = true;
        c.Step(o, s, k, 10.11, true, false, b);
        Check(b.Count(LeftMouse, true) == 2, "toggle activates on rising physical key edge");
        c.Step(o, s, k, 10.14, true, false, b);
        Check(c.GetStatus().shoot == Status::Target, "held toggle key does not toggle repeatedly");
        k.down[Mouse4] = false;
        c.Step(o, s, k, 10.15, true, false, b);
        k.down[Mouse4] = true;
        c.Step(o, s, k, 10.16, true, false, b);
        Check(c.GetStatus().shoot == Status::ToggleOff, "second physical press toggles off");
        k.down[Mouse4] = false;
        c.Step(o, s, k, 10.17, true, false, b);
        k.down[Mouse4] = true;
        c.Step(o, s, k, 10.22, true, false, b);
        c.Step(o, s, k, 10.23, false, false, b);
        c.Step(o, s, k, 10.24, true, false, b);
        Check(c.GetStatus().shoot == Status::ToggleOff, "focus loss resets toggle; held key cannot reactivate");
        o.shootMode = 1;
        s.weaponReady = false;
        s.reloading = true;
        c.Step(o, s, k, 10.25, true, false, b);
        Check(c.GetStatus().shoot == Status::Reloading, "readiness diagnostics identify reload gate");
        s.reloading = false;
        s.empty = true;
        c.Step(o, s, k, 10.26, true, false, b);
        Check(c.GetStatus().shoot == Status::Empty, "readiness diagnostics identify empty magazine");
    }
    {
        o = {};
        o.strafer = 1;
        Fake b;
        Controller c;
        Keys k;
        k.down[A] = k.down[W] = true;
        auto s = Player();
        s.grounded = false;
        const auto diagonal = OptimizeStrafe(s, k, o, .004f);
        Check(diagonal.active && diagonal.forwardMove > 0 && diagonal.sideMove < 0,
              "default strafe plan respects held forward plus side movement");
        Check(std::abs(std::hypot(diagonal.forwardMove, diagonal.sideMove) - s.maxSpeed) < .001f,
              "diagonal movement vector is normalized to configured movement speed");
        c.Step(o, s, k, 11, true, false, b);
        Check(b.Count(W, false) == 0 && c.SuppressedRepeats() == 0,
              "default strafe does not release or suppress physical forward input");
        o.strafeStrength = 0;
        Check(!OptimizeStrafe(s, k, o, .004f).active, "zero strafe strength leaves camera untouched");
        o.strafeStrength = 1;
        s.tick = 100;
        o.jumper = 1;
        k.down[Space] = true;
        c.Step(o, s, k, 11.01, true, false, b);
        s.tick = 1;
        s.grounded = true;
        c.Step(o, s, k, 11.02, true, false, b);
        Check(c.GetStatus().jumps == 0, "tick rollback does not synthesize an immediate new jump");
        ++s.tick;
        s.grounded = false;
        c.Step(o, s, k, 11.024, true, false, b);
        ++s.tick;
        s.grounded = true;
        c.Step(o, s, k, 11.044, true, false, b);
        Check(c.GetStatus().jumps == 1, "fresh landing after rollback rearms once");
    }
    {
        Options pulse;
        pulse.shoot = 1;
        pulse.shootMode = 1;
        pulse.delayMs = 0;
        pulse.pressMs = 40;
        pulse.intervalMs = 600;
        Controller c;
        Fake b;
        Keys k;
        const auto s = Player();
        c.Step(pulse, s, k, 20, true, false, b);
        c.Step(pulse, s, k, 20.001, false, false, b, true);
        Check(b.Count(LeftMouse, true) == 1 && b.Count(LeftMouse, false) == 1 && !c.PendingRelease(),
              "disabled request cancels an owned click immediately before its scheduled release");
        c.Step(pulse, s, k, 20.7, false, false, b, true);
        Check(b.Count(LeftMouse, true) == 1, "paused request cannot emit another click after the repeat interval");
    }
    {
        Options shooting;
        shooting.shoot = 1;
        shooting.delayMs = 0;
        shooting.intervalMs = 100;
        Controller c;
        Fake b;
        Keys k;
        auto s = Player();
        c.Step(shooting, s, k, 30, true, false, b);
        s.enemy = false;
        s.target = 0;
        c.Step(shooting, s, k, 30.005, true, false, b);
        s.enemy = true;
        s.target = 5;
        c.Step(shooting, s, k, 30.01, true, false, b);
        c.Step(shooting, s, k, 30.099, true, false, b);
        Check(b.Count(LeftMouse, true) == 1,
              "briefly losing and reacquiring a target cannot bypass the click interval");
        c.Step(shooting, s, k, 30.101, true, false, b);
        Check(b.Count(LeftMouse, true) == 2, "reacquired target fires when the original interval expires");
        s.weaponHandle = 8;
        s.weapon = 16;
        c.Step(shooting, s, k, 30.11, true, false, b);
        Check(b.Count(LeftMouse, true) == 3, "verified new weapon gets its own cadence and readiness gate");
    }
    {
        Options movement;
        movement.strafer = 1;
        movement.preserveForward = 0;
        Controller c;
        Fake b;
        Keys k;
        k.down[A] = k.down[W] = true;
        auto s = Player();
        s.grounded = false;
        s.yaw = 35;
        c.Step(movement, s, k, 40, true, false, b);
        const float firstTurn = std::abs(NormalizeYaw(b.yaw - s.yaw));
        float lastTurn{};
        for (unsigned i = 1; i <= 20; ++i) {
            c.Step(movement, s, k, 40 + i * .004, true, false, b);
            lastTurn = std::abs(NormalizeYaw(b.yaw - s.yaw));
            Check(lastTurn <= movement.turnRate * movement.strafeStrength * .004f + .001f,
                  "steering ramp never exceeds configured angular speed");
        }
        const auto fullPlan = OptimizeStrafe(s, k, movement, .004f);
        Check(firstTurn < lastTurn * .1f && std::abs(lastTurn - std::abs(NormalizeYaw(fullPlan.yaw - s.yaw))) < .001f,
              "steering starts gently and reaches full selected strength after the ramp duration");
        const auto turns = b.turns;
        k.down[LeftShift] = true;
        c.Step(movement, s, k, 40.084, true, false, b);
        Check(b.turns == turns && b.Count(W, true) == 1 && c.GetStatus().strafe == Status::Walking,
              "holding walking modifier yields steering and restores physically held forward input");
        k.down[LeftShift] = false;
        k.down[RightShift] = true;
        c.Step(movement, s, k, 40.088, true, false, b);
        Check(b.turns == turns, "right Shift has the same walking override");
        k.down[RightShift] = false;
        c.Step(movement, s, k, 40.092, true, false, b);
        Check(std::abs(NormalizeYaw(b.yaw - s.yaw)) < lastTurn * .1f,
              "releasing walking restarts the gentle steering ramp");
        movement.strafeRampMs = 0;
        k.down[LeftShift] = true;
        movement.strafeWalkPause = 0;
        c.Step(movement, s, k, 40.096, true, false, b);
        Check(b.turns == turns + 2 && std::abs(std::abs(NormalizeYaw(b.yaw - s.yaw)) - lastTurn) < .001f,
              "both new movement behaviors can be disabled independently");
        k.down[LeftShift] = false;
        movement.strafeRampMs = 80;
        k.down[A] = false;
        k.down[D] = true;
        c.Step(movement, s, k, 40.1, true, false, b);
        Check(std::abs(NormalizeYaw(b.yaw - s.yaw)) < lastTurn * .1f,
              "changing strafe direction ramps from rest instead of snapping");
        movement.strafeRampMs = std::numeric_limits<float>::quiet_NaN();
        Check(!Valid(movement), "nonfinite steering ramp rejected");
        movement.strafeRampMs = 80;
        movement.strafeWalkPause = 2;
        Check(!Valid(movement), "invalid walk-override flag rejected");
    }
    {
        PhysicalInput physical;
        physical.Event(WM_KEYDOWN, Space, 0);
        physical.Event(WM_KEYUP, Space, 0, InputTag);
        Check(physical.Snapshot().Held(Space), "own key-up does not erase physical hold");
        physical.Event(WM_KEYUP, Space, 0);
        Check(!physical.Snapshot().Held(Space), "physical key-up releases intent");
        physical.Event(WM_LBUTTONDOWN, 0, 0, InputTag);
        Check(!physical.Snapshot().Held(LeftMouse), "generated click is not physical fire");
        physical.Event(WM_KEYDOWN, 'Y', 0);
        Check(physical.Snapshot().textInput, "chat shortcut pauses input");
        physical.Event(WM_KEYDOWN, VK_RETURN, 0);
        Check(!physical.Snapshot().textInput, "submitting chat clears pause");
        physical.Event(WM_KEYDOWN, W, 0);
        physical.Event(WM_KILLFOCUS, 0, 0);
        Check(!physical.Snapshot().Held(W), "focus loss clears held input");
    }
    {
        using namespace cs2;
        Fixture f;
        Sample s;
        Check(ReadAssistSample(f.memory, f.addresses, s) && CrosshairEnemy(s) && s.weaponReady && s.grounded,
              "Source 2 reader resolves enemy, firearm readiness and ground flag");
        std::array<char, 32> liveType{};
        std::memcpy(liveType.data(), "c_cs_player_for_precache", sizeof("c_cs_player_for_precache"));
        f.Put(Fixture::base + 0x86000, liveType);
        f.Put(Fixture::base + 0x87000, liveType);
        Check(ReadAssistSample(f.memory, f.addresses, s) && s.weaponReady && CrosshairEnemy(s),
              "verified live precache pawn designer name supports local and target identity");
        std::memset(liveType.data(), 0, liveType.size());
        std::memcpy(liveType.data(), "weapon_ak47", sizeof("weapon_ak47"));
        f.Put(Fixture::base + 0x87000, liveType);
        Check(ReadAssistSample(f.memory, f.addresses, s) && !CrosshairEnemy(s),
              "accepting live pawn alias never admits a weapon designer name");
        std::memset(liveType.data(), 0, liveType.size());
        std::memcpy(liveType.data(), "c_cs_player_for_precache", sizeof("c_cs_player_for_precache"));
        f.Put(Fixture::base + 0x87000, liveType);
        f.Put(f.addresses.angles, NativeViewAngles{std::numeric_limits<float>::quiet_NaN(), 0, 0});
        Check(ReadAssistSample(f.memory, f.addresses, s) && s.weaponReady && CrosshairEnemy(s) && !s.anglesKnown,
              "unavailable view angles do not disable independent crosshair shooting or jumping");
        f.Put(f.addresses.angles, NativeViewAngles{});
        f.Put(Fixture::pawn + offsets::WaterLevel, std::numeric_limits<float>::quiet_NaN());
        Check(ReadAssistSample(f.memory, f.addresses, s) && s.weaponReady && !Movable(s),
              "invalid movement data disables movement only, preserving verified shooting data");
        f.Put(Fixture::pawn + offsets::WaterLevel, 0.f);
        f.Put(Fixture::enemy + offsets::Team, std::uint8_t{2});
        Check(ReadAssistSample(f.memory, f.addresses, s) && !CrosshairEnemy(s), "same team is never eligible");
        f.Put(Fixture::enemy + offsets::Team, std::uint8_t{3});
        f.Put(Fixture::enemy + offsets::SpawnImmunity, std::uint8_t{1});
        Check(ReadAssistSample(f.memory, f.addresses, s) && !CrosshairEnemy(s), "spawn immunity blocks shoot");
        f.Put(Fixture::enemy + offsets::SpawnImmunity, std::uint8_t{0});
        f.Put(Fixture::weapon + offsets::Clip1, std::int32_t{0});
        Check(ReadAssistSample(f.memory, f.addresses, s) && !s.weaponReady, "empty magazine blocks shoot");
        f.Put(Fixture::weapon + offsets::Clip1, std::int32_t{30});
        f.Put(Fixture::weapon + offsets::NextPrimaryTick, 1002u);
        Check(ReadAssistSample(f.memory, f.addresses, s) && !s.weaponReady, "next-attack tick gates shot");
        f.Put(Fixture::weapon + offsets::NextPrimaryTick, 1000u);
        f.Put(Fixture::weapon + offsets::WeaponReload, std::uint8_t{1});
        Check(ReadAssistSample(f.memory, f.addresses, s) && !s.weaponReady, "reload state blocks shoot");
        f.Put(Fixture::weapon + offsets::WeaponReload, std::uint8_t{0});
        f.Put(Fixture::controller + offsets::ControllerPawn, 0x80005u);
        Check(!ReadAssistSample(f.memory, f.addresses, s) && !s.valid, "mismatched controller pawn blocks all input");
        f.Put(Fixture::controller + offsets::ControllerPawn, 0x80003u);
        f.Put(Fixture::pawn + offsets::CrosshairIndex, std::int32_t{7});
        Check(ReadAssistSample(f.memory, f.addresses, s) && !CrosshairEnemy(s),
              "crosshair on weapon entity cannot shoot");
        f.Put(Fixture::pawn + offsets::CrosshairIndex, std::int32_t{5});
        f.Put(Fixture::pawn + offsets::MovementFlags, 0u);
        Check(ReadAssistSample(f.memory, f.addresses, s) && IsAirborne(s), "air flag is read directly");
        f.Put(Fixture::pawn + offsets::ActualMoveType, std::uint8_t{9});
        Check(ReadAssistSample(f.memory, f.addresses, s) && !IsAirborne(s), "ladder excluded by movement type");
        f.Put(Fixture::pawn + offsets::LifeState, std::uint8_t{1});
        Check(!ReadAssistSample(f.memory, f.addresses, s) && !s.valid, "dead local pawn clears sample");
    }
    {
        Options jump;
        jump.jumper = 1;
        Controller c;
        Fake b;
        Keys k;
        k.down[Space] = true;
        auto s = Player();
        s.tick = 200;
        c.Step(jump, s, k, 50, true, false, b);
        Check(b.events.empty(), "ground engagement preserves the initial physical press");
        ++s.tick;
        c.Step(jump, s, k, 50.004, true, false, b);
        Check(b.events.empty(), "initial press survives until takeoff or bounded timeout");
        s.grounded = false;
        c.Step(jump, s, k, 50.008, true, false, b);
        Check(b.Count(Space, false) == 1, "takeoff primes the next jump during flight");
        ++s.tick;
        s.grounded = true;
        c.Step(jump, s, k, 50.024, true, false, b);
        Check(b.Count(Space, true) == 1, "first eligible landing sample re-jumps without another delay");
        c.Step(jump, s, k, 50.060, true, false, b);
        Check(b.Count(Space, false) == 2 && b.Count(Space, true) == 1,
              "unconsumed jump pulse releases without same-poll repress");
        c.Step(jump, s, k, 50.080, true, false, b);
        Check(b.Count(Space, true) == 1, "stalled known simulation tick cannot accumulate repeated presses");
        ++s.tick;
        c.Step(jump, s, k, 50.084, true, false, b);
        Check(b.Count(Space, true) == 2, "simulation progress permits one retry of an unconsumed jump");
        s.grounded = false;
        c.Step(jump, s, k, 50.088, true, false, b);
        for (int i = 1; i < 20; ++i)
            c.Step(jump, s, k, 50.088 + i * .004, true, false, b);
        Check(b.Count(Space, true) == 2, "airborne frames do not consume jump press edges");
        c.Stop(b, k);
        s.grounded = true;
        s.tick = 0;
        c.Step(jump, s, k, 51, true, false, b);
        c.Step(jump, s, k, 51.032, true, false, b);
        c.Step(jump, s, k, 51.036, true, false, b);
        Check(b.Count(Space, true) == 2, "missing tick still preserves distinct release and press intervals");
        c.Step(jump, s, k, 51.048, true, false, b);
        Check(b.Count(Space, true) == 3, "elapsed tick interval provides missing-tick recovery");
    }
    {
        PhysicalInput physical;
        physical.RelativeMouse(-5);
        physical.RelativeMouse(2);
        const auto first = physical.Snapshot();
        Check(first.mouseTravelX == -3 && first.mouseSequence == 2,
              "relative mouse samples accumulate without consumption");
        physical.RelativeMouse(50, MOUSE_MOVE_ABSOLUTE);
        physical.RelativeMouse(50, MOUSE_MOVE_RELATIVE, InputTag);
        Check(physical.Snapshot().mouseTravelX == first.mouseTravelX &&
                  physical.Snapshot().mouseSequence == first.mouseSequence,
              "absolute pointer moves and synthetic mouse motion cannot steer");
        physical.Event(WM_KILLFOCUS, 0, 0);
        Check(physical.Snapshot().mouseSequence == 0 && physical.Snapshot().mouseTravelX == 0,
              "focus loss drops old mouse direction");
    }
    {
        Options move;
        move.strafer = 1;
        move.strafeMode = 1;
        move.preserveForward = 0;
        Controller c;
        Fake b;
        Keys k;
        auto s = Player();
        s.grounded = false;
        k.down[Space] = k.down[W] = true;
        c.Step(move, s, k, 60, true, false, b);
        Check(b.events.empty() && b.turns == 0, "mouse mode waits for real motion instead of alternating A/D");
        k.mouseTravelX = -8;
        ++k.mouseSequence;
        c.Step(move, s, k, 60.004, true, false, b);
        Check(b.Count(A, true) == 1 && b.Count(W, false) == 1 && b.turns == 0,
              "left mouse movement presses A without rotating camera");
        c.Step(move, s, k, 60.008, true, false, b);
        Check(b.Count(A, true) == 1, "mouse side is held without repeating key-down each worker poll");
        k.mouseTravelX += 20;
        ++k.mouseSequence;
        c.Step(move, s, k, 60.012, true, false, b);
        Check(b.Count(A, false) == 1 && b.Count(D, true) == 1,
              "mouse direction switch releases old side before pressing new side");
        k.down[A] = true;
        c.Step(move, s, k, 60.016, true, false, b);
        Check(b.Count(D, false) == 1 && b.Count(W, true) == 1 && c.GetStatus().strafe == Status::ManualInput,
              "physical A/D takes over and restores held forward input");
        k.down[A] = false;
        c.Step(move, s, k, 60.020, true, false, b);
        Check(b.Count(A, true) == 1 && b.Count(D, true) == 1, "manual takeover discards stale mouse direction");
        k.mouseTravelX -= 10;
        ++k.mouseSequence;
        c.Step(move, s, k, 60.024, true, false, b);
        c.Step(move, s, k, 60.078, true, false, b);
        Check(b.Count(A, false) == 2 && b.Count(W, true) == 2,
              "stopped mouse releases owned side and restores forward");
        k.mouseTravelX += 10;
        ++k.mouseSequence;
        c.Step(move, s, k, 60.082, true, false, b);
        s.grounded = true;
        c.Step(move, s, k, 60.086, true, false, b);
        Check(b.Count(D, false) == 2 && b.turns == 0, "landing releases mouse strafe with camera untouched throughout");
        s.grounded = false;
        k.mouseTravelX -= 10;
        ++k.mouseSequence;
        c.Step(move, s, k, 60.090, true, false, b);
        c.Step(move, s, k, 60.094, false, false, b);
        Check(!c.PendingRelease(), "pause relinquishes all synthetic movement ownership");
    }
    {
        Options pistol;
        pistol.autoPistol = 1;
        pistol.pistolIntervalMs = 100;
        Controller c;
        Fake b;
        Keys k;
        auto s = Player();
        s.weapon = 32;
        s.enemy = false;
        s.target = 0;
        k.down[LeftMouse] = true;
        c.Step(pistol, s, k, 70, true, false, b);
        Check(b.Count(LeftMouse, false) == 1 && b.Count(LeftMouse, true) == 0,
              "auto-pistol primes a released edge after manual click");
        c.Step(pistol, s, k, 70.004, true, false, b);
        Check(b.Count(LeftMouse, true) == 0, "auto-pistol never combines press and release in one input interval");
        c.Step(pistol, s, k, 70.016, true, false, b);
        Check(b.Count(LeftMouse, true) == 1, "held pistol repeats when weapon ready without requiring an enemy target");
        c.Step(pistol, s, k, 70.037, true, false, b);
        c.Step(pistol, s, k, 70.100, true, false, b);
        Check(b.Count(LeftMouse, true) == 1, "auto-pistol honors its distinct repeat interval");
        s.weaponReady = false;
        s.cooldown = true;
        c.Step(pistol, s, k, 70.120, true, false, b);
        Check(b.Count(LeftMouse, true) == 1, "auto-pistol always respects actual weapon cooldown");
        s.weaponReady = true;
        s.cooldown = false;
        c.Step(pistol, s, k, 70.124, true, false, b);
        Check(b.Count(LeftMouse, true) == 2, "ready pistol fires after cooldown without adding reaction delay");
        k.down[LeftMouse] = false;
        c.Step(pistol, s, k, 70.128, true, false, b);
        Check(b.Count(LeftMouse, false) == 3 && !c.PendingRelease(),
              "releasing manual fire immediately releases auto-pistol ownership");
        for (auto id : {63, 64, 7, 9, 0})
            Check(!SemiAutomaticPistol(static_cast<std::uint16_t>(id)),
                  "automatic charge and nonpistol weapons are excluded");
        for (auto id : {1, 2, 3, 4, 30, 32, 36, 61})
            Check(SemiAutomaticPistol(static_cast<std::uint16_t>(id)), "known semi-pistol is supported");
        k.down[LeftMouse] = true;
        c.Step(pistol, s, k, 70.132, true, false, b);
        const auto downs = b.Count(LeftMouse, true);
        pistol.autoPistol = 0;
        c.Step(pistol, s, k, 70.136, true, false, b);
        Check(b.Count(LeftMouse, true) == downs + 1 && !c.PendingRelease(),
              "disabling auto-pistol restores a physically held button");
        pistol.autoPistol = 1;
        c.Step(pistol, s, k, 70.140, true, false, b);
        const auto beforePause = b.Count(LeftMouse, true);
        c.Step(pistol, s, k, 70.144, false, false, b);
        Check(b.Count(LeftMouse, true) == beforePause && !c.PendingRelease(),
              "focus pause cannot restore mouse down into another application");
    }
    {
        Fixture f;
        Sample s;
        f.Put(Fixture::services + cs2::offsets::ActiveWeapon, std::uint32_t{});
        Check(cs2::ReadAssistSample(f.memory, f.addresses, s) && s.tick == 1001 && !s.weaponReady,
              "movement command timing remains available without an equipped firearm");
        Options badAssist;
        badAssist.strafeMode = 2;
        Check(!Valid(badAssist), "unknown strafe mode rejected");
        badAssist = {};
        badAssist.autoPistol = 2;
        Check(!Valid(badAssist), "invalid auto-pistol flag rejected");
        badAssist = {};
        badAssist.pistolIntervalMs = std::numeric_limits<float>::quiet_NaN();
        Check(!Valid(badAssist), "nonfinite pistol interval rejected");
    }
    {
        PhysicalInput physical;
        physical.Event(WM_KEYDOWN, 'Y', 0, 0, false);
        Check(!physical.Snapshot().textInput,
              "typing a profile name inside the overlay does not enter game chat state");
        physical.Event(WM_KEYUP, 'Y', 0, 0, false);
        physical.Event(WM_KEYDOWN, 'Y', 0);
        Check(physical.Snapshot().textInput, "gameplay chat shortcut still pauses assists");
        physical.Event(WM_KEYDOWN, VK_RETURN, 0, 0, false);
        Check(physical.Snapshot().textInput, "overlay text entry does not dismiss a preexisting game chat pause");
        physical.Event(WM_KEYDOWN, VK_ESCAPE, 0);
        Check(!physical.Snapshot().textInput, "gameplay escape exits chat pause");
        Options move;
        move.strafer = 1;
        move.strafeMode = 1;
        Controller c;
        Fake b;
        Keys k;
        auto sample = Player();
        sample.grounded = false;
        k.down[Space] = true;
        c.Step(move, sample, k, 80, true, false, b);
        k.mouseTravelX = -10;
        ++k.mouseSequence;
        c.Step(move, sample, k, 80.004, true, false, b);
        k.textInput = true;
        c.Step(move, sample, k, 80.008, true, false, b);
        Check(b.Count(A, false) == 1 && !c.PendingRelease() && c.GetStatus().strafe == Status::Paused,
              "chat entry releases synthetic steering even when caller active flag is stale");
    }
    {
        o = {};
        o.autoPistol = 1;
        o.shoot = 1;
        o.delayMs = 0;
        Fake b;
        Controller c;
        Keys k;
        auto s = Player();
        s.weapon = 32;
        k.down[LeftMouse] = true;
        c.Step(o, s, k, 90, true, false, b);
        c.Step(o, s, k, 90.016, true, false, b);
        k.down[LeftMouse] = false;
        c.Step(o, s, k, 90.020, true, false, b);
        Check(b.Count(LeftMouse, true) == 1 && b.Count(LeftMouse, false) == 2,
              "auto-pistol releases before handing the button back to assisted shoot");
        c.Step(o, s, k, 90.024, true, false, b);
        Check(b.Count(LeftMouse, true) == 1, "input owner handoff cannot collapse a release and new shot");
        c.Step(o, s, k, 90.036, true, false, b);
        Check(b.Count(LeftMouse, true) == 2, "working assisted shoot resumes after a distinct button release interval");
    }
    {
        o = {};
        o.strafer = 1;
        o.strafeMode = 1;
        Fake b;
        Controller c;
        Keys k;
        auto s = Player();
        s.grounded = false;
        k.down[Space] = true;
        c.Step(o, s, k, 100, true, false, b);
        k.mouseTravelX = -5;
        ++k.mouseSequence;
        c.Step(o, s, k, 100.004, true, false, b);
        b.fail = true;
        k.mouseTravelX += 10;
        ++k.mouseSequence;
        c.Step(o, s, k, 100.008, true, false, b);
        Check(b.Count(D, true) == 0 && c.PendingRelease(), "failed side release cannot press the opposite direction");
        b.fail = false;
        c.Step(o, s, k, 100.012, false, false, b);
        Check(b.Count(A, false) == 1 && !c.PendingRelease(),
              "failed steering release is retained and retried during pause");
    }
    {
        Options move;
        move.strafer = 1;
        move.strafeRampMs = 0;
        auto s = Player();
        s.grounded = false;
        Keys k;
        k.down[W] = true;
        for (unsigned key : {A, D}) {
            k.down[A] = key == A;
            k.down[D] = key == D;
            s.yaw = 0;
            for (unsigned i = 0; i < 80; ++i) {
                const auto plan = OptimizeStrafe(s, k, move, .004f);
                s.yaw = plan.yaw;
            }
            Check(std::abs(s.yaw) > 30, "held W plus side escapes equal-speed plateau and reaches useful strafe angle");
            Check((key == A ? s.yaw : -s.yaw) > 0, "plateau recovery follows the requested strafe side");
        }
    }
    {
        Options jump;
        jump.jumper = 1;
        auto s = Player();
        s.tick = 100;
        Keys k;
        k.down[Space] = true;
        Fake b;
        Controller c;
        c.Step(jump, s, k, 110, true, false, b);
        Check(b.Count(Space, false) == 0, "initial physical jump is not canceled by an immediate synthetic release");
    }
    {
        Options move;
        move.strafer = 1;
        move.preserveForward = 0;
        move.strafeRampMs = 0;
        auto s = Player();
        s.grounded = false;
        Keys k;
        k.down[A] = k.down[W] = true;
        Fake b;
        Controller c;
        b.yawResult = YawResult::Yielded;
        c.Step(move, s, k, 120, true, false, b);
        Check(c.GetStatus().turns == 0 && c.GetStatus().failures == 0 && c.GetStatus().strafe == Status::CameraBusy &&
                  b.Count(W, true) == 1,
              "camera race yields and restores forward without claiming a successful turn or input failure");
        b.yawResult = YawResult::Unchanged;
        c.Step(move, s, k, 120.004, true, false, b);
        Check(c.GetStatus().turns == 0 && c.GetStatus().strafe == Status::Aligned,
              "already aligned camera does not inflate applied-turn diagnostics");
        b.yawResult = YawResult::Applied;
        c.Step(move, s, k, 120.008, true, false, b);
        Check(c.GetStatus().turns == 1, "only committed camera changes count as turns");
        b.yawResult = YawResult::Failed;
        c.Step(move, s, k, 120.012, true, false, b);
        Check(c.GetStatus().turns == 1 && c.GetStatus().failures == 1 && c.GetStatus().strafe == Status::InputBlocked &&
                  c.SuppressedRepeats() == 0,
              "failed camera write restores manual forward and reports failure");
    }
    {
        Options jump;
        jump.jumper = 1;
        auto s = Player();
        s.grounded = false;
        s.tick = 300;
        Keys k;
        k.down[Space] = true;
        Fake b;
        Controller c;
        c.Step(jump, s, k, 130, true, false, b);
        jump.jumper = 0;
        b.fail = true;
        c.Step(jump, s, k, 130.004, true, false, b);
        Check(c.PendingRelease(), "failed Space handback retains ownership for a retry");
        b.fail = false;
        c.Step(jump, s, k, 130.008, true, false, b);
        Check(b.Count(Space, true) == 1 && !c.PendingRelease() && c.SuppressedRepeats() == 0,
              "disabling Jumper hands a still-held Space back even while airborne");
        jump.jumper = 1;
        c.Step(jump, s, k, 130.012, true, false, b);
        c.Step(jump, s, k, 130.016, false, false, b, false);
        Check(b.Count(Space, true) == 1 && !c.PendingRelease(), "focus loss never restores Space into another app");
        c.Step(jump, s, k, 130.020, true, false, b);
        k.down[Space] = false;
        s.grounded = true;
        ++s.tick;
        c.Step(jump, s, k, 130.040, true, false, b);
        Check(b.Count(Space, true) == 1 && c.GetStatus().jumps == 0,
              "releasing physical Space cancels a pending landing jump");
    }
    {
        Fixture f;
        Sample s;
        Options move;
        move.strafer = 1;
        Keys keys;
        keys.down[A] = true;
        f.Put(Fixture::pawn + cs2::offsets::MovementFlags, 0u);
        for (float yaw : {450.f, -450.f, 1080.f}) {
            f.Put(f.addresses.angles, cs2::NativeViewAngles{0, yaw, 0});
            Check(cs2::ReadAssistSample(f.memory, f.addresses, s, false) && s.anglesKnown && s.yaw == yaw &&
                      OptimizeStrafe(s, keys, move, .004f).active,
                  "finite unwrapped yaw remains usable and retains raw compare-exchange bits");
        }
        Check(f.combatReads == 0 && !s.weaponKnown && !s.enemy && s.shots == 0,
              "movement-only sampling omits weapon, shot and target reads");
        Check(cs2::ReadAssistSample(f.memory, f.addresses, s) && f.combatReads > 0 && s.weaponKnown && s.enemy,
              "default combat sampling preserves assisted-fire readiness");
        f.Put(f.addresses.angles, cs2::NativeViewAngles{0, std::numeric_limits<float>::infinity(), 0});
        Check(cs2::ReadAssistSample(f.memory, f.addresses, s, false) && !s.anglesKnown,
              "nonfinite yaw cannot become a camera write");
    }
    {
        RecoilActivity activity;
        auto s = Player();
        s.shots = 7;
        Check(!activity.Update(s, 140, true), "historical shot count does not lock the strafe camera");
        ++s.shots;
        Check(activity.Update(s, 140.004, true), "observed new shot yields camera to recoil");
        activity.Update(s, 140.1, true);
        activity.Update(s, 140.2, true);
        Check(!activity.Update(s, 140.305, true), "recoil camera ownership expires without another shot");
        ++s.shots;
        activity.Update(s, 140.31, true);
        ++s.weaponHandle;
        Check(!activity.Update(s, 140.32, true), "weapon change cannot inherit recoil ownership");
        ++s.shots;
        activity.Update(s, 140.33, true);
        s.shots = 0;
        Check(!activity.Update(s, 140.34, true), "shot counter reset clears recoil ownership");
        ++s.shots;
        activity.Update(s, 140.35, true);
        Check(!activity.Update(s, 140.36, false), "disabled recoil releases its camera window");
        Options options;
        options.jumper = 1;
        Keys keys;
        keys.down[Space] = true;
        Check(PollIntervalMs(true, true, options, keys) == 1, "held Jumper uses a bounded one-millisecond wait");
        keys.down[Space] = false;
        Check(PollIntervalMs(true, true, options, keys) == 4, "released jump returns to ordinary polling");
        Check(PollIntervalMs(false, true, options, keys) == 20 && PollIntervalMs(true, false, options, keys) == 20,
              "inactive or invalid state backs off without busy spinning");
    }
    {
        Options jump;
        jump.jumper = 1;
        auto s = Player();
        s.grounded = false;
        s.tick = 100;
        Keys keys;
        keys.down[Space] = true;
        Fake b;
        Controller c;
        c.Step(jump, s, keys, 150, true, false, b);
        ++s.owner;
        s.tick = 200;
        s.grounded = true;
        b.failJumpRestore = true;
        c.Step(jump, s, keys, 150.020, true, false, b);
        Check(b.Count(Space, true) == 0 && c.PendingRelease() && c.GetStatus().jump == Status::InputBlocked,
              "failed ownership handback cannot reuse an old airborne release for a new player's jump");
        b.failJumpRestore = false;
        c.Step(jump, s, keys, 150.024, true, false, b);
        Check(b.Count(Space, true) == 1 && c.GetStatus().jumps == 0,
              "ownership handback retries before new jump control starts");
    }
    std::printf("Assist checks: %u checks, %u failures; synthetic input backend only.\n", checks, failures);
    return failures ? 1 : 0;
}
