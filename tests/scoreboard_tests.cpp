#include "scoreboard_reader.hpp"
#include "weapon_catalog.hpp"
#include <cstdio>
#include <limits>
#include <vector>
using namespace awareness;
using namespace awareness::cs2;
namespace {
unsigned failed{}, checks{};
void Check(bool b, const char *text) {
    ++checks;
    if (!b) {
        ++failed;
        std::printf("FAIL %s\n", text);
    }
}
struct Fixture {
    static constexpr std::uintptr_t base = 0x10000000, list = base + 0x1000, chunk = base + 0x10000;
    static constexpr std::uintptr_t controller = base + 0x20000, pawn = base + 0x30000, weapon = base + 0x40000,
                                    services = base + 0x50000, items = base + 0x60000, handles = base + 0x70000;
    std::vector<std::byte> bytes = std::vector<std::byte>(0x100000);
    static bool Read(void *ctx, std::uintptr_t at, void *out, std::size_t n) noexcept {
        auto &f = *static_cast<Fixture *>(ctx);
        if (at < base || at - base > f.bytes.size() || n > f.bytes.size() - (at - base))
            return false;
        std::memcpy(out, f.bytes.data() + at - base, n);
        return true;
    }
    template <class T> void Put(std::uintptr_t at, T value) {
        std::memcpy(bytes.data() + at - base, &value, sizeof(value));
    }
    void Entity(unsigned index, std::uintptr_t at) {
        const auto id = chunk + index * offsets::EntityStride;
        Put(id, at);
        Put(id + 0x10, index + 0x80000u);
        Put(at + offsets::Identity, id);
    }
    Memory m{this, Read};
    Fixture() {
        Put(list + offsets::EntityTable, chunk);
        Entity(1, controller);
        Entity(65, pawn);
        Entity(100, weapon);
        Put(controller + offsets::ControllerPawn, 0x80041u);
        Put(controller + spectator_layout::SteamId, std::uint64_t{76561198012345678});
        Put(pawn + offsets::Team, std::uint8_t{3});
        Put(pawn + offsets::Health, 100);
        Put(pawn + offsets::LifeState, std::uint8_t{0});
        Put(pawn + scoreboard_layout::Armor, 100);
        Put(pawn + offsets::ItemServices, items);
        Put(items + scoreboard_layout::Helmet, std::uint8_t{1});
        Put(items + offsets::HasDefuser, std::uint8_t{1});
        Put(pawn + offsets::WeaponServices, services);
        Put(services + scoreboard_layout::Weapons, 1);
        Put(services + scoreboard_layout::Weapons + 8, handles);
        Put(handles, 0x80064u);
        Put(services + offsets::ActiveWeapon, 0x80064u);
        Put(weapon + offsets::EntityOwner, 0x80041u);
        Put(weapon + offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, std::uint16_t{7});
    }
};
} // namespace
int main() {
    scoreboard::Options o;
    o.enabled = 1;
    Check(scoreboard::Valid(o), "default valid");
    o.scale = std::numeric_limits<float>::quiet_NaN();
    Check(!scoreboard::Valid(o), "reject NaN");
    o.scale = 1;
    for (const auto &w : WeaponIcons)
        Check(scoreboard::Equipment(static_cast<std::uint16_t>(w.id)), "every firearm mapped");
    Fixture f;
    scoreboard::Frame frame;
    Check(ReadScoreboard(f.m, f.list, frame) && frame.count == 1, "read bounded current controller/pawn");
    const auto p = frame.players[0];
    Check(p.count == 1 && p.items[0].definition == 7 && p.items[0].active && p.armor && p.helmet && p.defuser,
          "equipment and armor captured");
    auto script = scoreboard::Script(frame, o, false);
    Check(script.find("\"76561198012345678\"") != std::string::npos,
          "Steam ID kept as string beyond JS integer precision");
    Check(script.find("[\"ak47\",true,false]") != std::string::npos &&
              script.find("[\"armor_helmet\"") != std::string::npos,
          "whitelisted icon identifiers serialized");
    o.weapons = 0;
    o.armor = 0;
    script = scoreboard::Script(frame, o, false);
    Check(script.find("[\"ak47\"") == std::string::npos && script.find("[\"armor_helmet\"") == std::string::npos &&
              script.find("[\"defuser\"") != std::string::npos,
          "independent weapon/armor/objective options");
    script = scoreboard::Script(frame, o, true);
    Check(script.find("var rows=[];") != std::string::npos, "disable has no rows and cleanup body");
    f.Put(Fixture::weapon + offsets::EntityOwner, 0x100041u);
    ReadScoreboard(f.m, f.list, frame);
    Check(frame.count == 1 && !frame.players[0].count, "full owner serial rejects slot reuse");
    f.Put(Fixture::weapon + offsets::EntityOwner, 0x80041u);
    f.Put(Fixture::services + scoreboard_layout::Weapons, 2147483647);
    ReadScoreboard(f.m, f.list, frame);
    Check(frame.count == 1 && !frame.players[0].count, "corrupt vector count bounded");
    f.Put(Fixture::chunk + 65 * offsets::EntityStride + 0x10, 0x100041u);
    ReadScoreboard(f.m, f.list, frame);
    Check(!frame.count, "stale pawn handle rejected");
    Check(!ReadScoreboard(f.m, 0, frame) && !frame.count, "invalid list clears prior data");
    ScoreboardHud hud;
    Check(ReadScoreboardHud(f.m, Fixture::base + 0x80000, hud) == HudRead::Gone,
          "successfully read null HUD proves destroyed tree");
    Check(ReadScoreboardHud(f.m, Fixture::base - 1, hud) == HudRead::Unreadable,
          "failed HUD read cannot dismiss cleanup");
    f.Put(Fixture::base + 0x80000, Fixture::base + 0x81000);
    f.Put(Fixture::base + 0x81008, Fixture::base + 0x82000);
    Check(ReadScoreboardHud(f.m, Fixture::base + 0x80000, hud) == HudRead::Present &&
              hud.panel == Fixture::base + 0x82000,
          "live UI identity read coherently");
    f.Put(Fixture::base + 0x81008, std::uintptr_t{});
    Check(ReadScoreboardHud(f.m, Fixture::base + 0x80000, hud) == HudRead::Gone, "null UI panel proves tree removed");
    std::printf("Scoreboard: %u checks, %u failures\n", checks, failed);
    return failed ? 1 : 0;
}
