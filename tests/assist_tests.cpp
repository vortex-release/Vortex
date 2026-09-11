#include "assist_features.hpp"
#include "assist_reader.hpp"
#include "assist_input_state.hpp"
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
    bool fail{};
    unsigned turns{};
    float yaw{};
    bool Key(unsigned key, bool down) {
        if (fail)
            return false;
        events.push_back({key, down});
        return true;
    }
    bool Mouse(bool down) { return Key(LeftMouse, down); }
    bool Yaw(const Sample &, float value) {
        if (fail)
            return false;
        ++turns;
        yaw = value;
        return true;
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
    s.valid = s.walking = s.enemy = s.weaponReady = true;
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
    static bool Read(void *self, std::uintptr_t address, void *out, std::size_t size) noexcept {
        auto &f = *static_cast<Fixture *>(self);
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
        c.Step(o, s, k, 2, true, false, b);
        Check(b.Count(Space, true) == 0, "airborne hold primes without jumping");
        s.grounded = true;
        c.Step(o, s, k, 2.004, true, false, b);
        Check(b.Count(Space, true) == 1, "first ground sample jumps");
        c.Step(o, s, k, 2.008, true, false, b);
        Check(b.Count(Space, true) == 1, "same ground state does not spam");
        s.grounded = false;
        c.Step(o, s, k, 2.012, true, false, b);
        Check(b.Count(Space, false) == 2, "takeoff releases synthetic jump");
        s.grounded = true;
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
                        const float dt = .016f, limit = o.turnRate * dt;
                        const auto plan = OptimizeStrafe(s, k, o, dt);
                        float best = -1;
                        for (int i = 0; i <= 200; ++i)
                            best = std::max(
                                best, PredictedSpeed(s, NormalizeYaw(s.yaw - limit + 2 * limit * i / 200), side, o));
                        Check(plan.forwardMove == 0 && plan.sideMove == -side * s.maxSpeed,
                              "Source movement vector uses the held strafe direction");
                        Check(plan.active && std::abs(NormalizeYaw(plan.yaw - s.yaw)) <= limit + .001f,
                              "strafe turn respects rate limit");
                        Check(plan.predictedSpeed + .003f >= best,
                              "strafe maximizes configured model within turn interval");
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
    std::printf("Assist checks: %u checks, %u failures; synthetic input backend only.\n", checks, failures);
    return failures ? 1 : 0;
}
