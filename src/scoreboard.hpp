#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
namespace awareness::scoreboard {
struct Options {
    std::uint32_t enabled{}, weapons{1}, armor{1}, objective{1};
    float scale{1};
    bool operator==(const Options &) const = default;
};
inline bool Valid(const Options &o) noexcept {
    return o.enabled <= 1 && o.weapons <= 1 && o.armor <= 1 && o.objective <= 1 && std::isfinite(o.scale) &&
           o.scale >= .75f && o.scale <= 1.5f;
}
struct Item {
    std::uint16_t definition{};
    bool active{};
    bool operator==(const Item &) const = default;
};
struct Player {
    std::uint64_t steamId{};
    std::uint32_t controller{}, handle{};
    std::array<Item, 16> items{};
    std::uint32_t count{};
    bool armor{}, helmet{}, defuser{};
    bool operator==(const Player &) const = default;
};
struct Frame {
    std::array<Player, 64> players{};
    std::uint32_t count{};
    bool operator==(const Frame &) const = default;
};
inline const char *Equipment(std::uint16_t id) noexcept {
    switch (id) {
    case 1:
        return "deagle";
    case 2:
        return "elite";
    case 3:
        return "fiveseven";
    case 4:
        return "glock";
    case 7:
        return "ak47";
    case 8:
        return "aug";
    case 9:
        return "awp";
    case 10:
        return "famas";
    case 11:
        return "g3sg1";
    case 13:
        return "galilar";
    case 14:
        return "m249";
    case 16:
        return "m4a1";
    case 17:
        return "mac10";
    case 19:
        return "p90";
    case 23:
        return "mp5sd";
    case 24:
        return "ump45";
    case 25:
        return "xm1014";
    case 26:
        return "bizon";
    case 27:
        return "mag7";
    case 28:
        return "negev";
    case 29:
        return "sawedoff";
    case 30:
        return "tec9";
    case 32:
        return "hkp2000";
    case 33:
        return "mp7";
    case 34:
        return "mp9";
    case 35:
        return "nova";
    case 36:
        return "p250";
    case 38:
        return "scar20";
    case 39:
        return "sg556";
    case 40:
        return "ssg08";
    case 43:
        return "flashbang";
    case 44:
        return "hegrenade";
    case 45:
        return "smokegrenade";
    case 46:
        return "molotov";
    case 47:
        return "decoy";
    case 48:
        return "incgrenade";
    case 49:
        return "c4";
    case 60:
        return "m4a1_silencer";
    case 61:
        return "usp_silencer";
    case 63:
        return "cz75a";
    case 64:
        return "revolver";
    default:
        return nullptr;
    }
}
inline unsigned Order(std::uint16_t id) noexcept {
    if (id >= 43 && id <= 49)
        return 2;
    switch (id) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 30:
    case 32:
    case 36:
    case 61:
    case 63:
    case 64:
        return 1;
    default:
        return 0;
    }
}
// Input is fully numeric; equipment identifiers come exclusively from Equipment().
std::string Script(const Frame &, const Options &, bool clear);
} // namespace awareness::scoreboard
