#pragma once
#include "world_visuals.hpp"
#include "combat_draw.hpp"
#include "weapon_icons.hpp"
namespace awareness::worldvisuals {
inline float DistanceAlpha(Vector3 a, Vector3 b, float range, float units) noexcept {
    if (!Finite(a) || !Finite(b) || !std::isfinite(units) || units <= 0)
        return 0;
    const float meters = Distance(a, b) / units;
    return std::clamp((range - meters) / std::max(5.f, range * .2f), 0.f, 1.f);
}
inline void Draw(ImDrawList &d, ImFont *font, ImTextureRef atlas, const FrameSnapshot &frame, Viewport viewport,
                 const Configuration &c, const Options &o, const Footsteps &steps, const Drops &drops, double now) {
    if (o.footsteps) {
        unsigned emitted{};
        const auto &events = steps.Events();
        for (std::size_t i = events.count; i > 0 && emitted < 48; --i) {
            const auto &s = events[i - 1];
            if (!ShowFootstep(s, frame.localEntityId, frame.localTeam, o, now))
                continue;
            const float alpha = DistanceAlpha(s.position, frame.cameraOrigin, o.footRange, c.worldUnitsPerMeter);
            if (alpha <= 0)
                continue;
            ++emitted;
            const float life = static_cast<float>((now - s.time) / o.footDuration);
            for (unsigned ring = 0; ring < (o.footDouble ? 2u : 1u); ++ring) {
                const float progress = life - ring * .18f;
                if (progress < 0)
                    continue;
                const float radius = (.12f + progress * .88f) * o.footRadius * c.worldUnitsPerMeter;
                flight::ScreenPolyline line(d, o.footColor, c.opacity * alpha * (1 - life) * (1 - life), o.footWidth,
                                            false, 0);
                for (unsigned segment = 0; segment < 48; ++segment) {
                    const float a = segment * .1308996939f, b = (segment + 1) * .1308996939f;
                    ImVec2 p, q;
                    if (flight::ScreenLine(s.position + Vector3{std::cos(a) * radius, std::sin(a) * radius, 0},
                                           s.position + Vector3{std::cos(b) * radius, std::sin(b) * radius, 0},
                                           frame.viewProjection, viewport, p, q))
                        line.Add(p, q);
                    else
                        line.Flush();
                }
                line.Flush();
            }
        }
    }
    if (!o.dropped || now < drops.time || now - drops.time > .25)
        return;
    for (unsigned i = 0; i < drops.count && i < drops.values.size(); ++i) {
        const auto &weapon = drops.values[i];
        const float alpha =
            DistanceAlpha(weapon.position, frame.cameraOrigin, o.dropRange, c.worldUnitsPerMeter) * c.opacity;
        if (alpha <= 0)
            continue;
        if (o.dropBoxes && weapon.bounds)
            for (unsigned corner = 0; corner < 8; ++corner)
                for (unsigned axis = 1; axis <= 4; axis *= 2)
                    if (!(corner & axis)) {
                        ImVec2 p, q;
                        if (flight::ScreenLine(weapon.corners[corner], weapon.corners[corner | axis],
                                               frame.viewProjection, viewport, p, q))
                            d.AddLine(p, q, flight::Pack(o.dropColor, alpha), o.dropWidth);
                    }
        Vector2 screen;
        if (!WorldToScreen(weapon.position, frame.viewProjection, viewport, screen))
            continue;
        ImVec2 at{screen.x, screen.y + 8};
        if (o.dropIcons &&
            DrawWeaponIcon(&d, atlas, weapon.definition, {at.x - 22, at.y}, {44, 18}, flight::Pack(o.dropColor, alpha)))
            at.y += 20;
        if (o.dropNames)
            if (const auto *icon = FindWeaponIcon(weapon.definition)) {
                combat::Text(d, font, 13, at, o.dropColor, alpha, icon->name, true);
                at.y += 16;
            }
        if (o.dropDistance) {
            char text[24]{};
            std::snprintf(text, sizeof(text), "%.0f m",
                          Distance(weapon.position, frame.cameraOrigin) / c.worldUnitsPerMeter);
            combat::Text(d, font, 11, at, o.dropColor, alpha, text, true);
        }
    }
}
} // namespace awareness::worldvisuals
