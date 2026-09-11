#pragma once
#include "settings.hpp"
#include "branding.hpp"
#include <cstdio>
namespace awareness {
inline ImDrawList *DrawSessionBadge(const VisualOptions &v, const char *name, ImTextureRef avatar, bool hasAvatar,
                                    int ping, ImFont *font = nullptr, ImVec4 avoid = {}) {
    if (!v.sessionBadge)
        return nullptr;
    const auto display = ImGui::GetIO().DisplaySize;
    const float s = v.badgeScale;
    if (display.x < 230 * s || display.y < 64 * s)
        return nullptr;
    if (!font)
        font = ImGui::GetFont();
    const float textSize = 13 * s, height = 36 * s, margin = 14 * s;
    char latency[24]{};
    if (ping >= 0 && ping <= 5000)
        std::snprintf(latency, sizeof(latency), "%d ms", ping);
    else
        std::snprintf(latency, sizeof(latency), "-- ms");
    const float latencyWidth = font->CalcTextSizeA(textSize, FLT_MAX, 0, latency).x;
    const auto label = vortex::brand::FitText(font, textSize, name && name[0] ? name : "Steam user",
                                              std::min(150 * s, display.x - 166 * s - latencyWidth));
    const float nameWidth = font->CalcTextSizeA(textSize, FLT_MAX, 0, label.c_str()).x;
    const float width = 138 * s + latencyWidth + nameWidth;
    const ImVec2 at{display.x - margin - width, margin}, end{display.x - margin, margin + height};
    // Hide the whole badge when the open menu occupies its corner, never obscure its controls.
    if (avoid.z > avoid.x && avoid.w > avoid.y && at.x < avoid.z && end.x > avoid.x && at.y < avoid.w &&
        end.y > avoid.y)
        return nullptr;
    auto *d = ImGui::GetForegroundDrawList();
    const auto color = [&](int r, int g, int b, float alpha = 1.f) {
        return IM_COL32(r, g, b, static_cast<int>(255 * v.badgeOpacity * alpha));
    };
    const bool light = v.badgeLight != 0;
    const auto ink = light ? color(41, 42, 51) : color(232, 232, 240);
    const auto accent = light ? color(109, 84, 178) : color(177, 155, 245);
    d->AddRectFilled({at.x, at.y + 2 * s}, {end.x, end.y + 2 * s}, color(0, 0, 0, .16f), 12 * s);
    d->AddRectFilled(at, end, light ? color(238, 237, 243) : color(22, 23, 29), 12 * s);
    d->AddRect(at, end, light ? color(255, 255, 255, .65f) : color(255, 255, 255, .1f), 12 * s);
    vortex::brand::Mark(d, {at.x + 22 * s, at.y + 18 * s}, 20 * s, ink, accent);
    d->AddLine({at.x + 43 * s, at.y + 10 * s}, {at.x + 43 * s, at.y + 26 * s},
               light ? color(57, 52, 70, .18f) : color(255, 255, 255, .12f));
    const int bars = ping < 0 ? 0 : ping <= 60 ? 4 : ping <= 120 ? 3 : ping <= 200 ? 2 : 1;
    for (int i = 0; i < 4; ++i) {
        const float x = at.x + (54 + i * 4) * s, h = (4 + i * 3) * s;
        d->AddRectFilled({x, at.y + 25 * s - h}, {x + 2.5f * s, at.y + 25 * s},
                         i < bars ? accent : color(131, 128, 143, .35f), .8f * s);
    }
    const float baseline = at.y + (height - textSize) * .5f - 1 * s;
    d->AddText(font, textSize, {at.x + 77 * s, baseline}, ink, latency);
    const float split = at.x + 85 * s + latencyWidth;
    d->AddLine({split, at.y + 10 * s}, {split, at.y + 26 * s},
               light ? color(57, 52, 70, .18f) : color(255, 255, 255, .12f));
    d->AddText(font, textSize, {split + 12 * s, baseline}, ink, label.c_str());
    vortex::brand::Avatar(d, avatar, {end.x - 32 * s, at.y + 6 * s}, 24 * s, hasAvatar, accent);
    return d;
}
} // namespace awareness
