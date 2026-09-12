#pragma once
#include "grenade_lineups.hpp"
#include "combat_draw.hpp"
namespace awareness::lineups {
inline void Draw(ImDrawList &draw, ImFont *font, const Library &library, const Capture &capture,
                 const FrameSnapshot &frame, Viewport viewport, const Configuration &config, const Options &options,
                 double now) {
    if (!font || !awareness::Valid(viewport))
        return;
    const auto selected = Select(library.Records(), capture, options, config.worldUnitsPerMeter, now);
    draw.PushClipRect({viewport.x, viewport.y}, {viewport.x + viewport.width, viewport.y + viewport.height}, true);
    constexpr float tau = 6.28318530718f;
    for (std::size_t i = selected.count; i > 0; --i) {
        const auto &guide = selected.guides[i - 1];
        const auto &record = library.Records()[guide.index];
        const bool nearest = i == 1;
        const float distanceFade = std::clamp((options.range - guide.distance) / 5.f, 0.f, 1.f);
        const float alpha = config.opacity * distanceFade * (nearest ? 1.f : .38f);
        const Color color = guide.aligned ? Color{.96f, .97f, 1, options.color.a} : options.color;
        const float radius = options.standTolerance;
        for (unsigned segment = 0; segment < 32; ++segment) {
            const float a = segment * tau / 32, b = (segment + 1) * tau / 32;
            ImVec2 first, last;
            if (flight::ScreenLine(record.position + Vector3{std::cos(a) * radius, std::sin(a) * radius, 1.5f},
                                   record.position + Vector3{std::cos(b) * radius, std::sin(b) * radius, 1.5f},
                                   frame.viewProjection, viewport, first, last))
                draw.AddLine(first, last, flight::Pack(color, alpha), nearest ? 1.5f : 1.f);
        }
        Vector2 screen;
        if (WorldToScreen(record.position + Vector3{0, 0, 8}, frame.viewProjection, viewport, screen) &&
            screen.x >= viewport.x + 20 && screen.x <= viewport.x + viewport.width - 20 && screen.y >= viewport.y &&
            screen.y < viewport.y + viewport.height - 35) {
            combat::Text(draw, font, 13, {screen.x, screen.y}, color, alpha, record.name.c_str(), true);
            if (nearest) {
                char label[64]{};
                std::snprintf(label, sizeof(label), "%s  /  %.1f m", ThrowNames[record.throwType], guide.distance);
                combat::Text(draw, font, 11, {screen.x, screen.y + 16}, {.96f, .97f, 1, 1}, alpha, label, true);
            }
        }
        if (!nearest || !guide.atStand)
            continue;
        Vector2 aim;
        if (WorldToScreen(AimPoint(record), frame.viewProjection, viewport, aim) && aim.x >= viewport.x + 12 &&
            aim.x < viewport.x + viewport.width - 12 && aim.y >= viewport.y + 12 &&
            aim.y < viewport.y + viewport.height - 30) {
            const ImVec2 at{aim.x, aim.y};
            draw.AddCircle(at, guide.aligned ? 5.f : 8.f, flight::Pack(color, alpha), 24, 1.5f);
            draw.AddCircleFilled(at, 1.5f, flight::Pack(color, alpha), 8);
            combat::Text(draw, font, 11, {aim.x, aim.y + 13}, color, alpha, guide.aligned ? "Aligned" : "Aim here",
                         true);
        }
        const float y = viewport.y + viewport.height * .69f;
        combat::Text(draw, font, 13, {viewport.x + viewport.width * .5f, y}, color, alpha, ThrowNames[record.throwType],
                     true);
        if (!record.notes.empty())
            combat::Text(draw, font, 11, {viewport.x + viewport.width * .5f, y + 18}, {.96f, .97f, 1, 1}, alpha,
                         record.notes.c_str(), true);
    }
    draw.PopClipRect();
}
} // namespace awareness::lineups
