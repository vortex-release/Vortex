#pragma once
#include <awareness/OverlayApi.hpp>
#include <awareness/Trajectories.hpp>
namespace awareness::worldvisuals {
struct Options {
    std::uint32_t footsteps{1}, footTeams{}, footDouble{1}, dropped{1}, dropBoxes{1}, dropIcons{1}, dropNames{},
        dropDistance{1};
    float footDuration{1}, footRadius{1.8f}, footWidth{1.3f}, footRange{60}, dropRange{60}, dropWidth{1};
    Color footColor{.96f, .97f, 1, .8f}, dropColor{.96f, .97f, 1, .8f};
};
inline bool Valid(const Options &o) noexcept {
    const auto range = [](float n, float lo, float hi) { return std::isfinite(n) && n >= lo && n <= hi; };
    const auto color = [&](Color c) {
        return range(c.r, 0, 1) && range(c.g, 0, 1) && range(c.b, 0, 1) && range(c.a, 0, 1);
    };
    return o.footsteps <= 1 && o.footTeams <= 2 && o.footDouble <= 1 && o.dropped <= 1 && o.dropBoxes <= 1 &&
           o.dropIcons <= 1 && o.dropNames <= 1 && o.dropDistance <= 1 && range(o.footDuration, .2f, 3) &&
           range(o.footRadius, .2f, 5) && range(o.footWidth, .5f, 4) && range(o.footRange, 5, 150) &&
           range(o.dropRange, 5, 150) && range(o.dropWidth, .5f, 3) && color(o.footColor) && color(o.dropColor);
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
        for (std::size_t i = events_.count; i > 0; --i)
            if (events_[i - 1].handle == event.handle && event.time - events_[i - 1].time >= 0 &&
                event.time - events_[i - 1].time < .08)
                return false;
        events_.Push(event);
        return true;
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
