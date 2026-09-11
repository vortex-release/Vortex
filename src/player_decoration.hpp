#pragma once
#include "visual_styles.hpp"
#include "trajectory_draw.hpp"
namespace awareness::styling {
inline void Fill(ImDrawList &d, ScreenBox box, Color color, float opacity, const Player &p) {
    if (opacity <= 0)
        return;
    if (p.gradientFill) {
        const auto top = flight::Pack(p.fillTop, opacity), bottom = flight::Pack(p.fillBottom, opacity);
        if (p.boxRounding <= 0) {
            d.AddRectFilledMultiColor({box.min.x, box.min.y}, {box.max.x, box.max.y}, top, top, bottom, bottom);
        } else {
            const int first = d.VtxBuffer.Size;
            d.AddRectFilled({box.min.x, box.min.y}, {box.max.x, box.max.y}, IM_COL32_WHITE, p.boxRounding);
            const float height = std::max(1.f, box.max.y - box.min.y);
            for (int i = first; i < d.VtxBuffer.Size; ++i) {
                auto &vertex = d.VtxBuffer[i];
                const float t = std::clamp((vertex.pos.y - box.min.y) / height, 0.f, 1.f);
                const float coverage = ((vertex.col >> IM_COL32_A_SHIFT) & 255) / 255.f;
                const auto mix = [t](float a, float b) { return a + (b - a) * t; };
                const Color gradient{mix(p.fillTop.r, p.fillBottom.r), mix(p.fillTop.g, p.fillBottom.g),
                                     mix(p.fillTop.b, p.fillBottom.b), mix(p.fillTop.a, p.fillBottom.a)};
                vertex.col = flight::Pack(gradient, opacity * coverage);
            }
        }
    } else
        d.AddRectFilled({box.min.x, box.min.y}, {box.max.x, box.max.y}, flight::Pack(color, opacity), p.boxRounding);
}
inline void Border(ImDrawList &d, ScreenBox box, Color color, Color shadow, float opacity, float width, bool corners,
                   const Player &p) {
    const auto stroke = [&](float thickness, ImU32 tint) {
        if (corners) {
            const float x = (box.max.x - box.min.x) * p.cornerLength,
                        y = (box.max.y - box.min.y) * p.cornerLength * .72f;
            for (int c = 0; c < 4; ++c) {
                const ImVec2 at{c & 1 ? box.max.x : box.min.x, c & 2 ? box.max.y : box.min.y};
                d.AddLine(at, {at.x + (c & 1 ? -x : x), at.y}, tint, thickness);
                d.AddLine(at, {at.x, at.y + (c & 2 ? -y : y)}, tint, thickness);
            }
        } else
            d.AddRect({box.min.x, box.min.y}, {box.max.x, box.max.y}, tint, p.boxRounding, thickness, 0);
    };
    if (p.boxGlow > 0)
        for (int i = 3; i > 0; --i)
            stroke(width + i * 2, flight::Pack(color, opacity * p.boxGlow * .07f));
    stroke(width + 1.5f, flight::Pack(shadow, opacity * .65f));
    stroke(width, flight::Pack(color, opacity));
}
} // namespace awareness::styling
