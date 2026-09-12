#pragma once
#include "combat_features.hpp"
#include "configuration.hpp"
#include "entity_filter.hpp"
#include "trajectory_draw.hpp"
#include "ui_icons.hpp"
#include <cstdio>
namespace awareness::combat {
inline void Text(ImDrawList &d, ImFont *font, float size, ImVec2 p, Color color, float alpha, const char *text,
                 bool center = false) {
    if (center)
        p.x -= font->CalcTextSizeA(size, FLT_MAX, 0, text).x * .5f;
    d.AddText(font, size, {p.x + 1, p.y + 1}, flight::Pack({0, 0, 0, .5f}, alpha * color.a), text);
    d.AddText(font, size, p, flight::Pack(color, alpha), text);
}
// Clip the complete convex footprint to the view frustum. Near-camera fire keeps its
// fill instead of disappearing because one perimeter vertex crossed the near plane.
inline void AreaFill(ImDrawList &draw, std::span<const Vector3> world, Color color, const Matrix4x4 &matrix, Viewport v,
                     float alpha) {
    if (!Valid(v) || world.size() < 3 || world.size() > 64)
        return;
    std::array<ClipPoint, 80> polygon{}, next{};
    std::size_t count = world.size();
    for (std::size_t i = 0; i < count; ++i) {
        polygon[i] = Transform(world[i], matrix);
        if (!Finite(polygon[i]))
            return;
    }
    const auto distance = [](ClipPoint p, int plane) {
        switch (plane) {
        case 0:
            return p.w - 1e-4f;
        case 1:
            return p.z;
        case 2:
            return p.w - p.z;
        case 3:
            return p.w + p.x;
        case 4:
            return p.w - p.x;
        case 5:
            return p.w + p.y;
        default:
            return p.w - p.y;
        }
    };
    for (int plane = 0; plane < 7 && count >= 3; ++plane) {
        std::size_t n{};
        auto previous = polygon[count - 1];
        auto before = distance(previous, plane);
        for (std::size_t i = 0; i < count; ++i) {
            const auto current = polygon[i];
            const auto after = distance(current, plane);
            if ((before >= 0) != (after >= 0)) {
                if (n >= next.size())
                    return;
                next[n++] = Lerp(previous, current, before / (before - after));
            }
            if (after >= 0) {
                if (n >= next.size())
                    return;
                next[n++] = current;
            }
            previous = current;
            before = after;
        }
        polygon = next;
        count = n;
    }
    if (count < 3)
        return;
    std::array<ImVec2, 80> screen{};
    float winding{};
    for (std::size_t i = 0; i < count; ++i) {
        auto p = Project(polygon[i], v);
        screen[i] = {p.x, p.y};
    }
    for (std::size_t i = 0; i < count; ++i) {
        auto a = screen[i], b = screen[(i + 1) % count];
        winding += a.x * b.y - a.y * b.x;
    }
    if (winding < 0)
        std::reverse(screen.begin(), screen.begin() + count);
    draw.AddConvexPolyFilled(screen.data(), static_cast<int>(count), flight::Pack(color, alpha));
}
inline void AreaShape(ImDrawList &d, const Area &a, Color color, const Matrix4x4 &matrix, Viewport v, float alpha,
                      float outlineWidth = 1.5f, bool glow = false, bool fill = true) {
    if (!Finite(a.center) || !std::isfinite(a.radius) || a.radius <= 0 || a.radius > 500)
        return;
    std::array<Vector3, 64> world{};
    const unsigned count = a.boundaryCount >= 3 && a.boundaryCount <= 64 ? a.boundaryCount : 48;
    for (unsigned i = 0; i < count; ++i) {
        const float angle = i * (6.28318530718f / count);
        world[i] = a.boundaryCount >= 3 ? a.boundary[i]
                                        : a.center + Vector3{std::cos(angle) * a.radius, std::sin(angle) * a.radius, 0};
    }
    if (fill)
        AreaFill(d, {world.data(), count}, color, matrix, v, alpha);
    if (outlineWidth <= 0)
        return;
    auto edge = color;
    edge.a = std::min(1.f, color.a * 4);
    std::array<ImVec2, 64> projected{};
    bool closed = true;
    for (unsigned i = 0; i < count; ++i) {
        Vector2 p;
        if (!WorldToScreen(world[i], matrix, v, p)) {
            closed = false;
            break;
        }
        projected[i] = {p.x, p.y};
    }
    flight::ScreenPolyline line(d, edge, alpha, outlineWidth, glow, .6f);
    for (unsigned i = 0; i < count; ++i) {
        ImVec2 p, q;
        if (flight::ScreenLine(world[i], world[(i + 1) % count], matrix, v, p, q)) {
            if (closed) {
                const auto here = projected[i], next = projected[(i + 1) % count];
                const auto startJoin =
                    std::abs(p.x - here.x) + std::abs(p.y - here.y) < .01f
                        ? flight::ScreenPolyline::Join(projected[(i + count - 1) % count], here, next)
                        : ImVec2{};
                const auto endJoin = std::abs(q.x - next.x) + std::abs(q.y - next.y) < .01f
                                         ? flight::ScreenPolyline::Join(here, next, projected[(i + 2) % count])
                                         : ImVec2{};
                flight::ScreenStroke(d, p, q, edge, edge, alpha, outlineWidth, glow, .6f, startJoin, endJoin);
            } else
                line.Add(p, q);
        } else
            line.Flush();
    }
    line.Flush();
    if (a.type == AreaType::Smoke) {
        for (unsigned i = 0; i < count; ++i) {
            ImVec2 p, q;
            if (i % 4 == 0 && flight::ScreenLine(world[i], world[i] + Vector3{0, 0, a.height}, matrix, v, p, q))
                d.AddLine(p, q, flight::Pack(edge, alpha * .3f), 1);
            if (flight::ScreenLine(world[i] + Vector3{0, 0, a.height}, world[(i + 1) % count] + Vector3{0, 0, a.height},
                                   matrix, v, p, q))
                d.AddLine(p, q, flight::Pack(edge, alpha * .45f), 1);
        }
    }
}
// Each hit owns a fixed world anchor. Camera movement only changes its projection.
inline float MarkerOpacity(double age, const Options &o) noexcept {
    if (!std::isfinite(age) || age < 0 || !std::isfinite(o.markerHold) || o.markerHold < 0 ||
        !std::isfinite(o.markerDuration) || o.markerDuration <= 0)
        return 0;
    const float t = static_cast<float>(std::clamp((age - o.markerHold) / o.markerDuration, 0., 1.));
    return 1 - t * t * (3 - 2 * t);
}
inline bool DrawHitMarker(ImDrawList &draw, const Hit &hit, const Matrix4x4 &matrix, Viewport v, const Options &o,
                          float opacity, double now) {
    const float alpha = MarkerOpacity(now - hit.time, o) * opacity;
    Vector2 p;
    if (!o.hitMarker || !std::isfinite(alpha) || alpha <= 0 || !std::isfinite(o.markerSize) || o.markerSize < 3 ||
        o.markerSize > 24 || !WorldToScreen(hit.position, matrix, v, p))
        return false;
    const float outer = o.markerSize, inner = outer * .38f, margin = outer + 3;
    if (p.x < v.x - margin || p.x > v.x + v.width + margin || p.y < v.y - margin || p.y > v.y + v.height + margin)
        return false;
    draw.PushClipRect({v.x, v.y}, {v.x + v.width, v.y + v.height}, true);
    // A thin dark outline keeps white ticks legible against bright surfaces.
    for (int pass = 0; pass < 2; ++pass)
        for (int x : {-1, 1})
            for (int y : {-1, 1})
                draw.AddLine({p.x + x * inner, p.y + y * inner}, {p.x + x * outer, p.y + y * outer},
                             flight::Pack(pass ? o.markerColor : Color{0, 0, 0, .65f * o.markerColor.a}, alpha),
                             pass ? 1.6f : 3.2f);
    draw.PopClipRect();
    return true;
}
inline float HitLogOpacity(double age, float lifetime) noexcept {
    if (!std::isfinite(age) || !std::isfinite(lifetime) || lifetime <= 0 || age < 0 || age >= lifetime)
        return 0;
    const float enter = static_cast<float>(std::min(age / .12, 1.));
    const float leave = static_cast<float>(std::min((lifetime - age) / .35, 1.));
    return enter * enter * (3 - 2 * enter) * leave * leave * (3 - 2 * leave);
}
inline unsigned DrawHitFeed(ImDrawList &draw, ImFont *font, const Feedback &feedback, Viewport v, const Options &o,
                            float opacity, double now) {
    if (!o.hitLog || !font || !Valid(v) || v.width < 64 || v.height < 48)
        return 0;
    const float scale = o.hitLogScale, rowHeight = 32 * scale, gap = 4 * scale;
    const float width = std::min(250 * scale, v.width - 16);
    if (v.height < rowHeight + 16 || width < 144 * scale)
        return 0;
    std::array<const Hit *, 8> visible{};
    unsigned count{};
    const auto capacity =
        std::min({o.hitLogRows, 8u, static_cast<unsigned>((v.height - 16 + gap) / (rowHeight + gap))});
    for (std::size_t i = feedback.hits.count; i > 0 && count < capacity; --i)
        if (HitLogOpacity(now - feedback.hits[i - 1].time, o.hitLogDuration) > 0)
            visible[count++] = &feedback.hits[i - 1];
    if (!count)
        return 0;
    const float blockHeight = count * rowHeight + (count - 1) * gap;
    const float x = std::clamp(v.x + v.width * o.hitLogX, v.x + 8, v.x + v.width - width - 8);
    const float y = std::clamp(v.y + v.height * o.hitLogY, v.y + 8, v.y + v.height - blockHeight - 8);
    unsigned rows{};
    draw.PushClipRect({v.x, v.y}, {v.x + v.width, v.y + v.height}, true);
    for (unsigned i = 0; i < count; ++i) {
        const auto &hit = *visible[i];
        const double age = now - hit.time;
        const float alpha = HitLogOpacity(age, o.hitLogDuration) * opacity;
        if (alpha <= 0)
            continue;
        const float top = y + rows * (rowHeight + 4 * scale);
        if (top + rowHeight > v.y + v.height - 4)
            break;
        const float slide = static_cast<float>(std::clamp(1. - age / .16, 0., 1.)) * 8 * scale;
        const ImVec2 a{x - slide, top}, b{x - slide + width, top + rowHeight};
        if (o.hitLogBackground) {
            draw.AddRectFilled(a, b, flight::Pack({.045f, .047f, .06f, .86f}, alpha), 7 * scale);
            draw.AddRect(a, b, flight::Pack({1, 1, 1, .08f}, alpha), 7 * scale);
        }
        const Color color = hit.headshot ? o.damageColor : o.markerColor;
        vortex::icons::Draw(&draw, vortex::icons::Id::Crosshair, {a.x + 9 * scale, top + 8 * scale}, 16 * scale,
                            flight::Pack(color, alpha));
        char amount[32]{};
        std::snprintf(amount, sizeof(amount), hit.headshot ? "-%d  HS" : "-%d", hit.damage);
        const float size = 13 * scale;
        const float amountWidth = font->CalcTextSizeA(size, FLT_MAX, 0, amount).x;
        Text(draw, font, size, {b.x - amountWidth - 10 * scale, top + 9 * scale}, color, alpha, amount);
        draw.PushClipRect({a.x + 33 * scale, top}, {b.x - amountWidth - 18 * scale, b.y}, true);
        Text(draw, font, size, {a.x + 33 * scale, top + 9 * scale}, o.markerColor, alpha,
             hit.name[0] ? hit.name : "Player");
        draw.PopClipRect();
        ++rows;
    }
    draw.PopClipRect();
    return rows;
}
inline unsigned DrawUtilityTimers(ImDrawList &draw, ImFont *font, const FrameSnapshot &frame, Viewport v,
                                  const Configuration &c, const Options &o, const WorldSnapshot &world) {
    if (!o.utilityTimers || !font || !Valid(v) || !std::isfinite(c.worldUnitsPerMeter) || c.worldUnitsPerMeter <= 0)
        return 0;
    unsigned count{};
    const float scale = o.timerScale, radius = 17 * scale;
    draw.PushClipRect({v.x, v.y}, {v.x + v.width, v.y + v.height}, true);
    for (std::size_t i = 0; i < std::min(world.areaCount, world.areas.size()); ++i) {
        const auto &area = world.areas[i];
        const bool fire = area.type == AreaType::Fire;
        if ((!fire && area.type != AreaType::Smoke) || (fire ? !o.timerFire : !o.timerSmoke) || !area.timer.Valid())
            continue;
        const float meters = Distance(frame.cameraOrigin, area.center) / c.worldUnitsPerMeter;
        if (!std::isfinite(meters) || meters > o.timerRange)
            continue;
        Vector2 screen;
        if (!WorldToScreen(area.center, frame.viewProjection, v, screen) || screen.x < v.x + radius ||
            screen.x > v.x + v.width - radius || screen.y < v.y + radius + 28 * scale ||
            screen.y > v.y + v.height - radius)
            continue;
        const ImVec2 at{screen.x, screen.y - 26 * scale};
        Color color = fire ? o.fireColor : o.smokeColor;
        color.a = 1;
        const float alpha = c.opacity * std::clamp(area.timer.remaining / .3f, 0.f, 1.f);
        draw.AddCircleFilled(at, radius, flight::Pack({.035f, .04f, .05f, .88f}, alpha), 32);
        draw.AddCircle(at, radius, flight::Pack({1, 1, 1, .14f}, alpha), 32, 2 * scale);
        constexpr float start = -1.5707963268f, tau = 6.28318530718f;
        draw.PathArcTo(at, radius, start, start + tau * area.timer.Progress(), 32);
        draw.PathStroke(flight::Pack(color, alpha), ImDrawFlags_None, 2 * scale);
        char text[24]{};
        std::snprintf(text, sizeof(text), area.timer.estimated ? "~%.0f" : "%.0f", std::ceil(area.timer.remaining));
        Text(draw, font, 12 * scale, {at.x, at.y - 6 * scale}, {1, 1, 1, 1}, alpha, text, true);
        Text(draw, font, 10 * scale, {at.x, at.y - radius - 13 * scale}, color, alpha, fire ? "FIRE" : "SMOKE", true);
        ++count;
    }
    draw.PopClipRect();
    return count;
}
inline void Draw(ImDrawList &d, ImFont *font, const FrameSnapshot &frame, Viewport v, const Configuration &c,
                 const Options &o, const WorldSnapshot &world, const Feedback &feedback, const ReplayFrame &,
                 double now) {
    DrawUtilityTimers(d, font, frame, v, c, o, world);
    DrawHitFeed(d, font, feedback, v, o, c.opacity, now);
    if (o.areas)
        for (std::size_t i = 0; i < world.areaCount && i < world.areas.size(); ++i) {
            const auto &a = world.areas[i];
            const bool fire = a.type == AreaType::Fire, smoke = a.type == AreaType::Smoke;
            if (fire && !o.fireArea || smoke && !o.smokeArea || !fire && !smoke && !o.blastArea)
                continue;
            // Hold the area while it burns; only fade during its final half second.
            const float fade = a.duration > 0 ? std::clamp(a.remaining / .5f, 0.f, 1.f) : 1;
            AreaShape(d, a,
                      fire    ? o.fireColor
                      : smoke ? o.smokeColor
                              : o.blastColor,
                      frame.viewProjection, v, c.opacity * fade, o.areaOutline, o.areaGlow != 0, o.areaFill != 0);
        }
    if (o.bombTimer && world.bomb.valid) {
        const auto &b = world.bomb;
        const auto color = b.remaining <= 10 ? o.bombDanger : b.remaining <= 20 ? o.bombWarning : o.bombSafe;
        const ImVec2 at{v.x + 50, v.y + v.height * .5f};
        char text[96]{};
        std::snprintf(text, sizeof(text), "%s", b.site == 0 ? "A  /  PLANTED" : "B  /  PLANTED");
        Text(d, font, 13, {at.x, at.y - 20}, color, c.opacity, text);
        std::snprintf(text, sizeof(text), "%.1f", b.remaining);
        Text(d, font, 42, at, color, c.opacity, text);
        if (b.defusing)
            std::snprintf(text, sizeof(text), "Defusing  %.1f  %s", b.defuseRemaining,
                          b.defuseRemaining < b.remaining ? "ON TIME" : "TOO LATE");
        else if (b.kitKnown)
            std::snprintf(text, sizeof(text), "%s  /  %.0f to defuse", b.hasKit ? "Kit" : "No kit", b.defuseLength);
        else
            std::snprintf(text, sizeof(text), "Defuse  5 with kit / 10 without");
        Text(d, font, 15, {at.x, at.y + 46}, color, c.opacity, text);
    }
    for (std::size_t i = 0; i < feedback.hits.count; ++i) {
        const auto &hit = feedback.hits[i];
        const double age = now - hit.time;
        if (age < 0)
            continue;
        DrawHitMarker(d, hit, frame.viewProjection, v, o, c.opacity, now);
        if (!o.damageNumbers || age >= o.damageDuration)
            continue;
        Vector2 p;
        const float life = static_cast<float>(age / o.damageDuration);
        if (WorldToScreen(hit.position, frame.viewProjection, v, p)) {
            char label[24]{};
            std::snprintf(label, sizeof(label), "-%d", hit.damage);
            Text(d, font, 19, {p.x, p.y - 14 - 30 * life - static_cast<float>(hit.serial % 3) * 10}, o.damageColor,
                 c.opacity * (1 - life), label, true);
        }
    }
}
} // namespace awareness::combat
