#pragma once
#include <awareness/OverlayApi.hpp>
#include <awareness/Trajectories.hpp>
#include "tracking_profiles.hpp"
namespace awareness::worldvisuals {
inline constexpr const char *DropGroupNames[]{"Pistols", "SMGs", "Rifles", "Snipers", "Heavy", "Utility"};
inline constexpr unsigned DropGroupCount = 6;
struct DropGroup {
    std::uint32_t enabled{1}, custom{}, display{}; // Icon, name, both, none.
    float range{60};
    Color color{.96f, .97f, 1, .8f};
};
inline const char *UtilityName(std::uint32_t definition) noexcept {
    switch (definition) {
    case 31:
        return "Zeus x27";
    case 43:
        return "Flashbang";
    case 44:
        return "HE grenade";
    case 45:
        return "Smoke";
    case 46:
        return "Molotov";
    case 47:
        return "Decoy";
    case 48:
        return "Incendiary";
    case 49:
        return "C4";
    case 57:
        return "Healthshot";
    case 41:
    case 42:
    case 59:
    case 500:
    case 503:
    case 505:
    case 506:
    case 507:
    case 508:
    case 509:
    case 512:
    case 514:
    case 515:
    case 516:
    case 517:
    case 518:
    case 519:
    case 520:
    case 521:
    case 522:
    case 523:
    case 525:
        return "Knife";
    default:
        return nullptr;
    }
}
inline unsigned DropCategory(std::uint32_t definition) noexcept {
    const auto group = tracking::WeaponGroup(definition);
    if (group != tracking::Group::Default)
        return static_cast<unsigned>(group) - 1;
    return UtilityName(definition) ? 5u : DropGroupCount;
}
inline const char *DroppedName(std::uint32_t definition) noexcept {
    if (const auto *icon = FindWeaponIcon(definition))
        return icon->name;
    return UtilityName(definition);
}
struct Options {
    std::uint32_t footsteps{1}, footTeams{}, footDouble{1}, dropped{1}, dropBoxes{1}, dropIcons{1}, dropNames{},
        dropDistance{1}, dropAmmo{};
    float footDuration{1}, footRadius{1.8f}, footWidth{1.3f}, footRange{60}, dropRange{60}, dropWidth{1};
    Color footColor{.96f, .97f, 1, .8f}, dropColor{.96f, .97f, 1, .8f};
    // Preserve the former all-firearm display. Utility is available but opt-in.
    std::array<DropGroup, DropGroupCount> dropGroups{{{}, {}, {}, {}, {}, {0}}};
};
struct DropStyle {
    bool enabled{}, icons{}, names{};
    float range{};
    Color color{};
};
inline DropStyle ResolveDrop(const Options &o, std::uint32_t definition) noexcept {
    const auto category = DropCategory(definition);
    if (!o.dropped || category >= o.dropGroups.size())
        return {};
    const auto &group = o.dropGroups[category];
    if (!group.enabled)
        return {};
    if (!group.custom)
        return {true, o.dropIcons != 0, o.dropNames != 0, o.dropRange, o.dropColor};
    return {true, group.display == 0 || group.display == 2, group.display == 1 || group.display == 2, group.range,
            group.color};
}
inline bool Valid(const Options &o) noexcept {
    const auto range = [](float n, float lo, float hi) { return std::isfinite(n) && n >= lo && n <= hi; };
    const auto color = [&](Color c) {
        return range(c.r, 0, 1) && range(c.g, 0, 1) && range(c.b, 0, 1) && range(c.a, 0, 1);
    };
    for (const auto &group : o.dropGroups)
        if (group.enabled > 1 || group.custom > 1 || group.display > 3 || !range(group.range, 5, 150) ||
            !color(group.color))
            return false;
    return o.footsteps <= 1 && o.footTeams <= 2 && o.footDouble <= 1 && o.dropped <= 1 && o.dropBoxes <= 1 &&
           o.dropIcons <= 1 && o.dropNames <= 1 && o.dropDistance <= 1 && o.dropAmmo <= 1 &&
           range(o.footDuration, .2f, 3) && range(o.footRadius, .2f, 5) && range(o.footWidth, .5f, 4) &&
           range(o.footRange, 5, 150) && range(o.dropRange, 5, 150) && range(o.dropWidth, .5f, 3) &&
           color(o.footColor) && color(o.dropColor);
}
struct Footstep {
    std::uint32_t handle{};
    int team{};
    Vector3 position{};
    double time{};
};
class Footsteps {
    flight::Ring<Footstep, 128> events_;

  public:
    void Clear() noexcept { events_.Clear(); }
    const auto &Events() const noexcept { return events_; }
    bool Add(Footstep event) noexcept {
        if (!event.handle || event.handle == 0xffffffff || event.team < 2 || event.team > 3 ||
            !Finite(event.position) || !std::isfinite(event.time))
            return false;
        // Keep event order even if independently sampled callbacks arrive late.
        // Symmetric time comparison merges event/fallback duplicates in either order.
        for (std::size_t i = events_.count; i > 0; --i) {
            const auto &previous = events_[i - 1];
            if (previous.time < event.time - .08)
                break;
            if (previous.handle == event.handle && std::abs(event.time - previous.time) < .08)
                return false;
        }
        if (events_.count == events_.values.size()) {
            if (event.time <= events_[0].time)
                return false;
            events_.Pop();
        }
        std::size_t insert = events_.count;
        while (insert && events_[insert - 1].time > event.time)
            --insert;
        events_.Push(event);
        for (std::size_t i = events_.count - 1; i > insert; --i)
            events_[i] = events_[i - 1];
        events_[insert] = event;
        return true;
    }
    static Footsteps MergeLive(const Footsteps &native, const Footsteps &sampled, double now,
                               double lifetime) noexcept {
        Footsteps out;
        if (!std::isfinite(now) || !std::isfinite(lifetime) || lifetime <= 0)
            return out;
        const auto &a = native.events_, &b = sampled.events_;
        std::size_t i{}, j{};
        // Both histories are chronological. Merge once, excluding expired/future
        // samples before they can displace current events from the bounded ring.
        while (i < a.count || j < b.count) {
            const auto &event = j == b.count || (i < a.count && a[i].time <= b[j].time) ? a[i++] : b[j++];
            const double age = now - event.time;
            if (age >= 0 && age < lifetime)
                out.Add(event);
        }
        return out;
    }
};
inline bool ShowFootstep(const Footstep &s, std::uint32_t localId, int localTeam, const Options &o,
                         double now) noexcept {
    const double age = now - s.time;
    if (!o.footsteps || age < 0 || age >= o.footDuration || (s.handle & 0x7fff) == localId)
        return false;
    return o.footTeams == 2 || (o.footTeams == 0 ? s.team != localTeam : s.team == localTeam);
}
struct DroppedWeapon {
    std::uint32_t handle{}, definition{};
    int ammo{-1}; // Unknown is distinct from an empty magazine.
    Vector3 position{};
    std::array<Vector3, 8> corners{};
    bool bounds{};
};
struct Drops {
    std::array<DroppedWeapon, 128> values{};
    std::uint32_t count{};
    double time{};
};
} // namespace awareness::worldvisuals
