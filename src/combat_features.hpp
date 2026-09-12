#pragma once
#include "preview_pose.hpp"
#include "weapon_catalog.hpp"
#include "tracking_profiles.hpp"
#include <awareness/Trajectories.hpp>
#include <awareness/CameraTracking.hpp>
#include <array>
#include <cmath>
#include <cstring>
#include <string_view>
namespace awareness::combat {
struct RecoilProfile {
    std::uint32_t overrideDefault{}, startShot{1}, curve{1};
    float vertical{1}, horizontal{1}, smoothing{.012f};
    std::uint32_t activation{}; // 0 inherit group, 1 on, 2 off.
};
inline constexpr std::size_t RecoilProfiles = std::size(WeaponIcons) + 1;
inline std::size_t WeaponProfile(std::uint32_t weapon) noexcept {
    for (std::size_t i = 0; i < std::size(WeaponIcons); ++i)
        if (WeaponIcons[i].id == weapon)
            return i + 1;
    return 0;
}
struct Options {
    std::uint32_t recoil{}, recoilSelection{}, bombTimer{}, hitMarker{}, hitSound{}, damageNumbers{};
    std::array<std::uint32_t, 6> recoilGroups{0, 0, 1, 1, 0, 0};
    std::uint32_t recoilInput{}; // 0 = Windows mouse input, 1 = native view angles.
    float mouseYaw{.022f}, mousePitch{.022f};
    std::uint32_t ghosts{}, ghostDirection{}, contrast{}, areas{}, hideParticles{};
    std::uint32_t fireArea{1}, smokeArea{1}, blastArea{1};
    std::uint32_t areaFill{1}, areaGlow{};
    float areaOutline{1.5f};
    float hitVolume{.65f}, markerSize{9}, markerDuration{.45f}, markerHold{.25f}, damageDuration{1.2f};
    float ghostDuration{.18f}, ghostOpacity{.32f}, worldDarkness{.35f}, entityBrightness{1.3f}, entitySaturation{1.3f};
    float sceneContrast{1.f}, sceneSaturation{1.f}, sceneExposure{}, sceneVignette{}, sceneTintStrength{};
    float sceneGamma{1}, sceneVibrance{}, sceneTemperature{}, sceneShadows{}, sceneHighlights{};
    Color sceneTint{1, 1, 1, 1};
    Color markerColor{1, 1, 1, 1}, damageColor{1, .8f, .3f, 1};
    Color bombSafe{1, 1, 1, 1}, bombWarning{1, .8f, .2f, 1}, bombDanger{1, .18f, .12f, 1};
    Color ghostFriend{.3f, .6f, 1, 1}, ghostEnemy{.62f, .25f, 1, 1};
    Color fireColor{.18f, .42f, 1, .25f}, smokeColor{.65f, .7f, .76f, .2f}, blastColor{1, .52f, .1f, .3f};
    std::uint32_t utilityTimers{}, timerFire{1}, timerSmoke{1};
    float timerScale{1}, timerRange{60};
    std::uint32_t hitLog{}, hitLogRows{4}, hitLogBackground{1};
    float hitLogDuration{3.5f}, hitLogScale{1}, hitLogX{.02f}, hitLogY{.7f};
    char hitSoundPath[2048]{};
    std::array<RecoilProfile, RecoilProfiles> weapons{};
    bool EnabledFor(std::uint32_t weapon) const noexcept {
        const auto index = WeaponProfile(weapon);
        if (!recoil || !index)
            return false;
        const auto activation = weapons[index].activation;
        return activation ? activation == 1 : recoilGroups[static_cast<unsigned>(tracking::WeaponGroup(weapon))] != 0;
    }
    const RecoilProfile &Profile(std::uint32_t weapon) const noexcept {
        const auto i = WeaponProfile(weapon);
        return weapons[i && weapons[i].overrideDefault ? i : 0];
    }
};
inline bool Valid(const Options &s) noexcept {
    const auto range = [](float v, float a, float b) { return std::isfinite(v) && v >= a && v <= b; };
    for (auto v : {s.recoil, s.bombTimer, s.hitMarker, s.hitSound, s.damageNumbers, s.ghosts, s.ghostDirection,
                   s.contrast, s.areas, s.hideParticles, s.fireArea, s.smokeArea, s.blastArea, s.areaFill, s.areaGlow})
        if (v > 1)
            return false;
    if (s.utilityTimers > 1 || s.timerFire > 1 || s.timerSmoke > 1 || s.hitLog > 1 || s.hitLogBackground > 1 ||
        s.hitLogRows < 1 || s.hitLogRows > 8 || !range(s.timerScale, .75f, 1.5f) || !range(s.timerRange, 5, 150) ||
        !range(s.hitLogDuration, 1, 10) || !range(s.hitLogScale, .75f, 1.5f) || !range(s.hitLogX, 0, 1) ||
        !range(s.hitLogY, 0, 1))
        return false;
    if (!range(s.areaOutline, 0, 4))
        return false;
    if (s.recoilInput > 1 || !range(s.mouseYaw, .001f, .1f) || !range(s.mousePitch, .001f, .1f))
        return false;
    if (s.recoilSelection >= RecoilProfiles || !std::memchr(s.hitSoundPath, 0, sizeof(s.hitSoundPath)) ||
        !range(s.hitVolume, 0, 1) || !range(s.markerSize, 3, 24) || !range(s.markerDuration, .1f, 2) ||
        !range(s.markerHold, 0, 2) || !range(s.damageDuration, .3f, 3) || !range(s.ghostDuration, .05f, 2) ||
        !range(s.ghostOpacity, 0, 1) || !range(s.worldDarkness, 0, .85f) || !range(s.entityBrightness, .25f, 2) ||
        !range(s.entitySaturation, 0, 2))
        return false;
    if (!range(s.sceneGamma, .5f, 2) || !range(s.sceneVibrance, -1, 1) || !range(s.sceneTemperature, -1, 1) ||
        !range(s.sceneShadows, -.3f, .3f) || !range(s.sceneHighlights, -.5f, .5f))
        return false;
    if (!range(s.sceneContrast, .5f, 1.5f) || !range(s.sceneSaturation, 0, 2) || !range(s.sceneExposure, -1, 1) ||
        !range(s.sceneVignette, 0, 1) || !range(s.sceneTintStrength, 0, 1))
        return false;
    for (auto c : {s.sceneTint, s.markerColor, s.damageColor, s.bombSafe, s.bombWarning, s.bombDanger, s.ghostFriend,
                   s.ghostEnemy, s.fireColor, s.smokeColor, s.blastColor})
        if (!range(c.r, 0, 1) || !range(c.g, 0, 1) || !range(c.b, 0, 1) || !range(c.a, 0, 1))
            return false;
    for (const auto enabled : s.recoilGroups)
        if (enabled > 1)
            return false;
    for (const auto &p : s.weapons)
        if (p.overrideDefault > 1 || p.activation > 2 || p.startShot < 1 || p.startShot > 10 || p.curve > 2 ||
            !range(p.vertical, 0, 1.5f) || !range(p.horizontal, 0, 1.5f) || !range(p.smoothing, 0, .3f))
            return false;
    return true;
}
struct RecoilSample {
    std::uint32_t owner{}, weaponHandle{}, weapon{}, shots{};
    Vector3 punch{};
    float recoilIndex{}, lastShot{}, gameTime{};
    bool valid{};
};
class Recoil {
    std::uint32_t owner_{}, weapon_{}, shots_{};
    Vector3 previous_{}, pending_{};
    double sampled_{};
    float lastShot_{}, sinceShot_{1};
    bool initialized_{};

  public:
    void Reset() noexcept { *this = {}; }
    void Restore(Vector3 amount) noexcept { pending_ = pending_ + amount; }
    Vector3 Update(const RecoilSample &s, const RecoilProfile &p, float dt, bool active) noexcept {
        if (!active || !s.valid || !Finite(s.punch) || !std::isfinite(s.gameTime) || !std::isfinite(s.lastShot) ||
            !s.owner || !s.weaponHandle || !WeaponProfile(s.weapon) || s.shots > 300 || std::abs(s.punch.x) > 45 ||
            std::abs(s.punch.y) > 45 || dt < 0 || !std::isfinite(dt)) {
            Reset();
            return {};
        }
        if (dt == 0)
            return {};
        if (dt > .15f) {
            Reset();
            return {};
        }
        if (!initialized_ || owner_ != s.owner || weapon_ != s.weaponHandle || s.gameTime < sampled_ ||
            s.shots < shots_) {
            Reset();
            owner_ = s.owner;
            weapon_ = s.weaponHandle;
            previous_ = s.punch;
            initialized_ = true;
        }
        const auto delta = s.punch - previous_;
        previous_ = s.punch;
        if (s.shots > shots_ || s.lastShot != lastShot_)
            sinceShot_ = 0;
        else
            sinceShot_ += std::max(std::clamp(dt, 0.f, .1f),
                                   static_cast<float>(std::clamp(double(s.gameTime) - sampled_, 0., 1.)));
        lastShot_ = s.lastShot;
        shots_ = s.shots;
        sampled_ = s.gameTime;
        if (s.shots < p.startShot || sinceShot_ > .3f) {
            pending_ = {};
            return {};
        }
        pending_.x += delta.x * p.vertical;
        pending_.y += delta.y * p.horizontal;
        float alpha = p.smoothing <= 0 ? 1.f : 1.f - std::exp(-std::clamp(dt, 0.f, .1f) / p.smoothing);
        // Deterministic easing of the response, never random input or extra target selection.
        if (p.curve == 1)
            alpha = 1 - (1 - alpha) * (1 - alpha);
        if (p.curve == 2)
            alpha = alpha * alpha * (3 - 2 * alpha);
        Vector3 correction{std::clamp(pending_.x * alpha, -8.f, 8.f), std::clamp(pending_.y * alpha, -8.f, 8.f), 0};
        pending_ = pending_ - correction;
        return correction;
    }
};
// Convert between uncorrected view angles and the shot direction seen by tracking.
inline camera::Angles ShotAngles(camera::Angles view, Vector3 punch) noexcept {
    return camera::Normalize({view.pitch - punch.x, view.yaw + punch.y});
}
inline camera::Angles CompensatedAngles(camera::Angles shot, Vector3 punch) noexcept {
    return camera::Normalize({shot.pitch + punch.x, shot.yaw - punch.y});
}
struct Hit {
    std::uint64_t serial{};
    std::uint32_t target{};
    Vector3 position{};
    int damage{};
    double time{};
    bool headshot{};
    char name[64]{}; // Captured with the event; never follows a reused entity slot.
};
struct Feedback {
    flight::Ring<Hit, 32> hits;
    std::uint64_t serial{};
    void Add(std::uint32_t target, Vector3 position, int damage, bool head, double now,
             std::string_view name = {}) noexcept {
        if (!target || !Finite(position) || damage <= 0 || damage > 1000 || !std::isfinite(now))
            return;
        Hit hit{++serial, target, position, damage, now, head};
        auto length = std::min(name.size(), sizeof(hit.name) - 1);
        // Do not split a UTF-8 sequence when truncating a long player name.
        if (length < name.size())
            while (length && (static_cast<unsigned char>(name[length]) & 0xc0) == 0x80)
                --length;
        for (std::size_t i = 0; i < length; ++i)
            hit.name[i] = static_cast<unsigned char>(name[i]) < 32 || name[i] == 127 ? ' ' : name[i];
        hits.Push(hit);
    }
    void Clear() noexcept { hits.Clear(); }
};
struct Bomb {
    bool valid{}, ticking{}, defused{}, exploded{}, defusing{}, hasKit{}, kitKnown{};
    std::uint32_t handle{};
    float remaining{}, defuseRemaining{}, defuseLength{10};
    int site{};
};
enum class AreaType : std::uint32_t { Fire, Smoke, Blast };
struct AreaTimer {
    float remaining{}, duration{};
    bool estimated{};
    bool Valid() const noexcept {
        return std::isfinite(remaining) && std::isfinite(duration) && duration > 0 && duration <= 60 && remaining > 0 &&
               remaining <= duration;
    }
    float Progress() const noexcept { return Valid() ? remaining / duration : 0; }
};
struct Area {
    std::uint32_t handle{};
    AreaType type{};
    Vector3 center{};
    float radius{}, height{}, remaining{}, duration{};
    std::array<Vector3, 64> boundary{};
    std::uint32_t boundaryCount{};
    std::array<Vector3, 64> cells{}, cellNormals{};
    std::uint32_t cellCount{};
    float cellRadius{25.f};
    AreaTimer timer;
    bool estimatedFootprint{}; // Event-only ground estimate until exact burning cells arrive.
};
struct WorldSnapshot {
    Bomb bomb;
    std::array<Area, 64> areas{};
    std::size_t areaCount{};
    std::uint32_t discovered{}, fireEntities{}, burningCells{}, fireReadFailures{};
    float gameTime{};
    // Area records are replaced before their count is published. Clearing only
    // the live range metadata avoids zeroing 150 KB on every reader/render tick.
    void Clear() noexcept {
        bomb = {};
        areaCount = 0;
        discovered = fireEntities = burningCells = fireReadFailures = 0;
        gameTime = 0;
    }
};
struct InfernoEvent {
    std::uint32_t index{}, handle{};
    Vector3 position{};
    double time{};
};
class InfernoEvents {
    std::array<InfernoEvent, 32> entries_{};

  public:
    void Clear() noexcept { entries_ = {}; }
    const auto &Entries() const noexcept { return entries_; }
    void Update(std::uint32_t index, std::uint32_t handle, Vector3 position, double now, bool burning) noexcept {
        if (!index || index > 32767 || !std::isfinite(now))
            return;
        InfernoEvent *slot{};
        for (auto &event : entries_)
            if (event.index == index) {
                slot = &event;
                break;
            }
        if (!burning) {
            if (slot)
                *slot = {};
            return;
        }
        if (!Finite(position))
            return;
        if (!slot)
            slot =
                &*std::min_element(entries_.begin(), entries_.end(), [](auto &a, auto &b) { return a.time < b.time; });
        *slot = {index, handle, position, now};
    }
};
// Start/stop events provide an approximate footprint only until burning-cell
// geometry is available. They never replace a cell-derived area for that entity.
inline void AppendInfernoEvents(const InfernoEvents &events, double now, WorldSnapshot &world) noexcept {
    for (const auto &event : events.Entries()) {
        const double age = now - event.time;
        if (!event.index || age < 0 || age >= 7 || world.areaCount >= world.areas.size())
            continue;
        bool found{};
        for (std::size_t i = 0; i < world.areaCount; ++i)
            found |= world.areas[i].type == AreaType::Fire && (world.areas[i].handle & 32767) == event.index;
        if (found)
            continue;
        Area area;
        area.handle = event.handle ? event.handle : event.index;
        area.type = AreaType::Fire;
        area.center = event.position + Vector3{0, 0, 2};
        area.radius = 42 + 78 * static_cast<float>(std::clamp(age / .6, 0., 1.));
        area.height = 6;
        area.remaining = static_cast<float>(7 - age);
        area.duration = 7;
        area.estimatedFootprint = true;
        world.areas[world.areaCount++] = area;
    }
}
struct GhostPoint {
    PreviewPose pose;
    double time{};
    float speed{};
};
struct GhostTrack {
    std::uint32_t handle{};
    flight::Ring<GhostPoint, 72> points;
    bool seen{};
    GhostPoint current{};
    float movement{};
    void Clear() noexcept {
        handle = 0;
        points.Clear();
        seen = false;
        current = {};
        movement = 0;
    }
};
struct ReplayActor {
    std::uint32_t handle{};
    PreviewPose current, past;
    float movement{};
    bool ready{};
};
struct ReplayFrame {
    std::array<ReplayActor, MaxEntities> actors{};
    std::uint32_t count{};
    double sampledAt{};
    void Clear() noexcept {
        count = 0;
        sampledAt = 0;
    }
};
class GhostHistory {
    std::array<GhostTrack, MaxEntities> tracks_{};

  public:
    void Clear() noexcept {
        for (auto &t : tracks_)
            t.Clear();
    }
    const auto &Tracks() const noexcept { return tracks_; }
    void Begin() noexcept {
        for (auto &t : tracks_)
            t.seen = false;
    }
    void Add(std::uint32_t handle, const PreviewPose &pose, double now, float duration) noexcept {
        if (!handle || !pose.valid || !std::isfinite(now))
            return;
        duration = std::clamp(duration, .05f, 2.f);
        GhostTrack *track{};
        for (auto &t : tracks_)
            if (t.handle == handle) {
                track = &t;
                break;
            }
        if (!track)
            for (auto &t : tracks_)
                if (!t.handle) {
                    track = &t;
                    t.handle = handle;
                    break;
                }
        if (!track)
            return;
        track->seen = true;
        track->current = {pose, now, track->current.speed};
        float speed{};
        if (track->points.count) {
            const auto &last = track->points[track->points.count - 1];
            const double elapsed = now - last.time;
            const float distance = Distance(last.pose.entity.origin, pose.entity.origin);
            if (elapsed < 0 || elapsed > .25 || distance > 128) {
                track->points.Clear();
                track->movement = 0;
            } else {
                if (elapsed < 1. / 32.)
                    return;
                speed = static_cast<float>(distance / elapsed);
                const float target = std::clamp(speed / 12.f, 0.f, 1.f);
                const float smoothing =
                    1.f - std::exp(-static_cast<float>(elapsed) / (target > track->movement ? .055f : .16f));
                track->movement += (target - track->movement) * smoothing;
            }
        }
        track->current.speed = speed;
        track->points.Push({pose, now, speed});
    }
    void End() noexcept {
        for (auto &t : tracks_)
            if (t.handle && !t.seen)
                t.Clear();
    }
};
inline bool ReplayPose(const GhostTrack &track, double at, GhostPoint &out) noexcept {
    if (!track.points.count || at < track.points[0].time || at > track.current.time)
        return false;
    const GhostPoint *a = &track.points[0], *b = &track.current;
    for (std::size_t i = 1; i < track.points.count; ++i) {
        if (track.points[i].time >= at) {
            b = &track.points[i];
            break;
        }
        a = &track.points[i];
    }
    out = *a;
    const float t =
        b->time > a->time ? static_cast<float>(std::clamp((at - a->time) / (b->time - a->time), 0., 1.)) : 0;
    out.time = at;
    out.pose.entity.origin = a->pose.entity.origin + flight::Scale(b->pose.entity.origin - a->pose.entity.origin, t);
    out.pose.validMask &= b->pose.validMask;
    for (std::size_t j = 0; j < out.pose.joints.size(); ++j) {
        auto &v = out.pose.joints[j];
        const auto &x = a->pose.joints[j], &y = b->pose.joints[j];
        v.position = x.position + flight::Scale(y.position - x.position, t);
        v.scale = x.scale + (y.scale - x.scale) * t;
        float dot{}, length{};
        for (int k = 0; k < 4; ++k)
            dot += x.rotation[k] * y.rotation[k];
        for (int k = 0; k < 4; ++k) {
            v.rotation[k] = x.rotation[k] + ((dot < 0 ? -y.rotation[k] : y.rotation[k]) - x.rotation[k]) * t;
            length += v.rotation[k] * v.rotation[k];
        }
        if (length > 1e-6f)
            for (auto &q : v.rotation)
                q /= std::sqrt(length);
    }
    return true;
}
inline Color ContrastColor(Color color, const Options &o) noexcept {
    if (!o.contrast)
        return color;
    const float grey = .2126f * color.r + .7152f * color.g + .0722f * color.b;
    return {std::clamp((grey + (color.r - grey) * o.entitySaturation) * o.entityBrightness, 0.f, 1.f),
            std::clamp((grey + (color.g - grey) * o.entitySaturation) * o.entityBrightness, 0.f, 1.f),
            std::clamp((grey + (color.b - grey) * o.entitySaturation) * o.entityBrightness, 0.f, 1.f), color.a};
}
inline void BuildReplay(const GhostHistory &history, double now, float delay, ReplayFrame &frame) noexcept {
    frame.Clear();
    frame.sampledAt = now;
    for (const auto &track : history.Tracks()) {
        if (!track.handle || !track.current.pose.valid || now - track.current.time > .1)
            continue;
        auto &actor = frame.actors[frame.count++];
        actor = {};
        actor.handle = track.handle;
        actor.current = track.current.pose;
        GhostPoint past;
        // Root velocity controls visibility; pose separation does not. Keep recording
        // stationary animation so movement resumes without rebuilding the history.
        actor.movement = std::clamp(track.movement, 0.f, 1.f);
        actor.ready = actor.movement > .01f && ReplayPose(track, now - std::clamp(delay, .05f, 2.f), past);
        if (actor.ready)
            actor.past = past.pose;
    }
}
} // namespace awareness::combat
