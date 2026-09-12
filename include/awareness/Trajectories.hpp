#pragma once
#include "OverlayApi.hpp"
#include <array>
#include <span>

namespace awareness::flight {
enum class Utility : std::uint32_t { None, HE, Smoke, Flash, Fire, Decoy };
enum class Shots : std::uint32_t { Local, All, Opponents, Teammates, TeamT, TeamCT };
inline Utility Weapon(std::uint32_t id) noexcept {
    switch (id) {
    case 44:
        return Utility::HE;
    case 45:
        return Utility::Smoke;
    case 43:
        return Utility::Flash;
    case 46:
    case 48:
        return Utility::Fire;
    case 47:
        return Utility::Decoy;
    default:
        return Utility::None;
    }
}
inline Color Tint(Utility type) noexcept {
    switch (type) {
    case Utility::HE:
        return {1, .46f, .08f, 1};
    case Utility::Smoke:
        return {.65f, .67f, .7f, 1};
    case Utility::Flash:
        return {1, 1, 1, 1};
    case Utility::Fire:
        return {1, .16f, .12f, 1};
    default:
        return {.4f, .72f, 1, 1};
    }
}
inline Vector3 Scale(Vector3 v, float s) noexcept {
    return {v.x * s, v.y * s, v.z * s};
}
inline float Dot(Vector3 a, Vector3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline float Fade(double now, double created, double duration) noexcept {
    if (!std::isfinite(now) || !std::isfinite(created) || duration <= 0)
        return 0;
    return static_cast<float>(std::clamp(1 - (now - created) / duration, 0., 1.));
}
template <class T, std::size_t N> struct Ring {
    std::array<T, N> values{};
    std::size_t first{}, count{};
    void Clear() noexcept { first = count = 0; }
    const T &operator[](std::size_t i) const noexcept { return values[(first + i) % N]; }
    T &operator[](std::size_t i) noexcept { return values[(first + i) % N]; }
    void Pop() noexcept {
        if (count) {
            first = (first + 1) % N;
            --count;
        }
    }
    void Push(const T &v) noexcept {
        if (count == N)
            Pop();
        values[(first + count++) % N] = v;
    }
};
struct Point {
    Vector3 position{};
    double time{};
};
struct Projectile {
    std::uint32_t handle{};
    Utility type{};
    Vector3 position{};
};
struct Trail {
    std::uint32_t handle{};
    Utility type{};
    Ring<Point, 193> points;
    double lastSeen{};
    Vector3 head{};
    bool active{}, seen{};
};
class Trails {
    std::array<Trail, 32> paths_{};

  public:
    void Clear() noexcept {
        for (auto &p : paths_)
            p = {};
    }
    const auto &Paths() const noexcept { return paths_; }
    void Update(std::span<const Projectile> samples, double now) noexcept {
        if (!std::isfinite(now))
            return;
        for (auto &p : paths_) {
            p.seen = false;
            if (p.handle && (now < p.lastSeen || Fade(now, p.lastSeen, 5) == 0))
                p = {};
        }
        for (const auto &s : samples) {
            if (!s.handle || s.type == Utility::None || !Finite(s.position))
                continue;
            Trail *p = nullptr;
            for (auto &t : paths_)
                if (t.handle == s.handle) {
                    p = &t;
                    break;
                }
            if (!p)
                for (auto &t : paths_)
                    if (!t.handle) {
                        p = &t;
                        break;
                    }
            if (!p)
                p = &*std::min_element(paths_.begin(), paths_.end(),
                                       [](auto &a, auto &b) { return a.lastSeen < b.lastSeen; });
            // A missed asynchronous sample does not create a new projectile. Keep
            // its history through short read gaps; full handles still separate reuse.
            if (p->handle != s.handle || p->type != s.type || (!p->active && now - p->lastSeen > .25) ||
                (p->points.count && Distance(p->points[p->points.count - 1].position, s.position) > 8192)) {
                *p = {};
                p->handle = s.handle;
                p->type = s.type;
            }
            p->lastSeen = now;
            p->head = s.position;
            p->active = p->seen = true;
            while (p->points.count && now - p->points[0].time > 3.)
                p->points.Pop();
            // Sampling at 64 Hz gives three seconds at a fixed, bounded cost.
            if (!p->points.count || now - p->points[p->points.count - 1].time >= 1. / 64.)
                p->points.Push({s.position, now});
        }
        for (auto &p : paths_)
            if (!p.seen)
                p.active = false;
    }
};
struct Shot {
    Vector3 start{}, end{};
    std::uint32_t shooter{};
    std::int32_t team{};
    double time{};
};
inline bool Matches(const Shot &s, Shots filter, std::uint32_t local, int team) noexcept {
    switch (filter) {
    case Shots::Local:
        return s.shooter == local;
    case Shots::All:
        return true;
    case Shots::Opponents:
        return team > 1 && s.team > 1 && s.team != team;
    case Shots::Teammates:
        return team > 1 && s.team == team;
    case Shots::TeamT:
        return s.team == 2;
    case Shots::TeamCT:
        return s.team == 3;
    }
    return false;
}
class Tracers {
    Ring<Shot, 512> shots_;
    float lifetime_{.5f};

  public:
    void Clear() noexcept { shots_.Clear(); }
    void SetLifetime(float seconds) noexcept {
        if (std::isfinite(seconds))
            lifetime_ = std::clamp(seconds, .1f, 5.f);
    }
    void Expire(double now) noexcept {
        while (shots_.count && (now < shots_[0].time || Fade(now, shots_[0].time, lifetime_) == 0))
            shots_.Pop();
    }
    bool Add(Shot s) noexcept {
        if (!Finite(s.start) || !Finite(s.end) || !std::isfinite(s.time) || !s.shooter ||
            Distance(s.start, s.end) > 32768)
            return false;
        Expire(s.time);
        for (std::size_t i = 0; i < shots_.count; ++i) {
            const auto &old = shots_[i];
            if (old.shooter == s.shooter && std::abs(old.time - s.time) < .025 && Distance(old.end, s.end) < 1.0f)
                return false;
        }
        shots_.Push(s);
        return true;
    }
    const auto &Lines() const noexcept { return shots_; }
};
struct Collision {
    float fraction{1};
    Vector3 end{}, normal{};
    bool solid{};
    float elasticity{1};
};
struct World {
    void *context{};
    bool (*sweep)(void *, Vector3, Vector3, Collision &) noexcept {};
};
struct Throw {
    Utility type{};
    Vector3 eye{}, velocity{};
    float pitch{}, yaw{}, strength{1}, speed{750}, gravity{800};
};
struct Prediction {
    std::array<Vector3, 513> points{};
    std::uint32_t count{};
    std::array<Vector3, 32> bounces{};
    std::uint32_t bounceCount{};
    Utility type{};
    bool valid{}, finished{};
    Vector3 landing{};
};
// Hull sweeps are mandatory: failed collision data never produces a pretend landing marker.
inline Prediction Predict(const Throw &input, World world) noexcept {
    Prediction out;
    out.type = input.type;
    if (!world.sweep || input.type == Utility::None || !Finite(input.eye) || !Finite(input.velocity) ||
        !std::isfinite(input.pitch) || !std::isfinite(input.yaw) || !std::isfinite(input.strength) ||
        !std::isfinite(input.speed) || input.speed < 1 || input.speed > 2000 || !std::isfinite(input.gravity) ||
        input.gravity < 1 || input.gravity > 4000)
        return out;
    constexpr float pi = 3.14159265358979323846f, dt = 1.f / 64;
    const float strength = std::clamp(input.strength, 0.f, 1.f);
    float pitch = std::clamp(input.pitch, -89.f, 89.f);
    pitch -= (90 - std::abs(pitch)) * 10 / 90;
    const float p = pitch * pi / 180, y = input.yaw * pi / 180;
    const Vector3 forward{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)};
    auto origin = input.eye;
    origin.z += strength * 12 - 12;
    Collision initial;
    if (!world.sweep(world.context, origin, origin + Scale(forward, 22), initial) || initial.solid)
        return out;
    origin = initial.end - Scale(forward, 6);
    auto velocity = Scale(forward, std::clamp(input.speed * .9f, 15.f, 750.f) * (.3f + .7f * strength)) +
                    Scale(input.velocity, 1.25f);
    out.points[out.count++] = origin;
    bool stopped = false;
    for (unsigned tick = 1; tick <= 512; ++tick) {
        float remaining = dt;
        if (!stopped) {
            const float oldZ = velocity.z;
            velocity.z -= input.gravity * .4f * dt;
            Vector3 displacement{velocity.x * dt, velocity.y * dt, (oldZ + velocity.z) * .5f * dt};
            for (unsigned collision = 0; collision < 4 && remaining > 1e-5f; ++collision) {
                Collision hit;
                if (!world.sweep(world.context, origin, origin + displacement, hit) || !std::isfinite(hit.fraction) ||
                    hit.fraction < 0 || hit.fraction > 1 || !Finite(hit.end) || hit.solid)
                    return {};
                origin = hit.end;
                if (hit.fraction >= 1)
                    break;
                const float length = std::sqrt(Dot(hit.normal, hit.normal));
                if (!std::isfinite(length) || length < .5f || length > 1.5f)
                    return {};
                const auto normal = Scale(hit.normal, 1 / length);
                if (out.bounceCount < out.bounces.size())
                    out.bounces[out.bounceCount++] = origin;
                if (input.type == Utility::Fire && normal.z >= .8660254f) {
                    out.finished = true;
                    break;
                }
                velocity = Scale(velocity - Scale(normal, 2 * Dot(velocity, normal)),
                                 std::clamp(.45f * hit.elasticity, 0.f, .9f));
                for (float *axis : {&velocity.x, &velocity.y, &velocity.z})
                    if (std::abs(*axis) < .1f)
                        *axis = 0;
                if (normal.z > .7f && Dot(velocity, velocity) < 400) {
                    velocity = {};
                    stopped = true;
                    break;
                }
                remaining *= 1 - hit.fraction;
                origin = origin + Scale(normal, .03125f);
                displacement = Scale(velocity, remaining);
                if (collision == 3)
                    return {}; // unresolved repeated contact
            }
        }
        out.points[out.count++] = origin;
        const float elapsed = tick * dt;
        if ((input.type == Utility::HE || input.type == Utility::Flash) && elapsed >= 1.5f)
            out.finished = true;
        if ((input.type == Utility::Smoke || input.type == Utility::Decoy) && stopped)
            out.finished = true;
        if (input.type == Utility::Fire && elapsed >= 3.5f)
            out.finished = true;
        if (out.finished)
            break;
    }
    out.landing = origin;
    out.valid = true;
    return out;
}
class Fov {
    float current_{};
    double previous_{};
    bool active_{};

  public:
    void Reset() noexcept { *this = {}; }
    float Update(float engine, bool enabled, float desired, double now) noexcept {
        if (!std::isfinite(engine) || engine < 1 || engine >= 179 || !std::isfinite(now)) {
            Reset();
            return engine;
        }
        if (!active_) {
            current_ = engine;
            previous_ = now;
            active_ = enabled;
        }
        if (!active_)
            return engine;
        const float dt = static_cast<float>(std::clamp(now - previous_, 0., .1));
        previous_ = now;
        const float target = enabled && std::isfinite(desired) ? std::clamp(desired, 60.f, 140.f) : engine;
        current_ += (target - current_) * (-std::expm1(-12.f * dt));
        if (!enabled && std::abs(current_ - engine) < .05f) {
            Reset();
            return engine;
        }
        return current_;
    }
};
} // namespace awareness::flight
