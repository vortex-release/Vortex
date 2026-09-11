#pragma once
#include "trajectory_style.hpp"
#include <imgui.h>
namespace awareness::flight {
inline bool ScreenLine(Vector3 a, Vector3 b, const Matrix4x4 &matrix, Viewport viewport, ImVec2 &start,
                       ImVec2 &end) noexcept {
    auto ca = Transform(a, matrix), cb = Transform(b, matrix);
    if (!Valid(viewport) || !ClipDepth(ca, cb))
        return false;
    auto p = Project(ca, viewport), q = Project(cb, viewport);
    // Clip x/y as well: geometry near the camera must not create huge ImGui vertices.
    float lo = 0, hi = 1;
    const auto dx = q.x - p.x, dy = q.y - p.y;
    const float direction[]{-dx, dx, -dy, dy};
    const float distance[]{p.x - viewport.x, viewport.x + viewport.width - p.x, p.y - viewport.y,
                           viewport.y + viewport.height - p.y};
    for (int i = 0; i < 4; ++i) {
        if (std::abs(direction[i]) < 1e-6f) {
            if (distance[i] < 0)
                return false;
        } else {
            float t = distance[i] / direction[i];
            if (direction[i] < 0)
                lo = (std::max)(lo, t);
            else
                hi = (std::min)(hi, t);
        }
    }
    if (lo > hi)
        return false;
    start = {p.x + dx * lo, p.y + dy * lo};
    end = {p.x + dx * hi, p.y + dy * hi};
    return true;
}
inline ImU32 Pack(Color c, float a) noexcept {
    return ImGui::ColorConvertFloat4ToU32({c.r, c.g, c.b, std::clamp(c.a * a, 0.f, 1.f)});
}
// A single strip carries the soft halo and both endpoint colors. No overlapping
// gradient segments or repeated full-width line draws are needed for a tracer.
inline void ScreenStroke(ImDrawList &draw, ImVec2 p, ImVec2 q, Color first, Color last, float alpha, float width,
                         bool glow, float strength, ImVec2 startJoin = {}, ImVec2 endJoin = {}) {
    const float dx = q.x - p.x, dy = q.y - p.y, length = std::sqrt(dx * dx + dy * dy);
    if (length < .01f || alpha <= 0)
        return;
    const ImVec2 normal{-dy / length, dx / length};
    if (startJoin.x == 0 && startJoin.y == 0)
        startJoin = normal;
    if (endJoin.x == 0 && endJoin.y == 0)
        endJoin = normal;
    const float half = width * .5f;
    const bool halo = glow && strength > 0;
    const float radius = half + (halo ? 12.f : 1.f);
    const float positions[]{-radius, -half - 6, -half - 2, -half, half, half + 2, half + 6, radius};
    const float weights[]{0, .07f * strength, .3f * strength, 1, 1, .3f * strength, .07f * strength, 0};
    const float plainPositions[]{-radius, -half, half, radius};
    const float plainWeights[]{0, 1, 1, 0};
    const int bands = halo ? 8 : 4;
    const auto uv = ImGui::GetFontTexUvWhitePixel();
    draw.PrimReserve((bands - 1) * 6, bands * 2);
    const auto base = draw._VtxCurrentIdx;
    for (int i = 0; i < bands - 1; ++i) {
        const unsigned a = base + static_cast<unsigned>(i * 2), b = a + 1, c = a + 2, d = a + 3;
        for (unsigned index : {a, b, c, b, d, c})
            draw.PrimWriteIdx(static_cast<ImDrawIdx>(index));
    }
    for (int i = 0; i < bands; ++i) {
        const float offset = halo ? positions[i] : plainPositions[i], weight = halo ? weights[i] : plainWeights[i];
        draw.PrimWriteVtx({p.x + startJoin.x * offset, p.y + startJoin.y * offset}, uv, Pack(first, alpha * weight));
        draw.PrimWriteVtx({q.x + endJoin.x * offset, q.y + endJoin.y * offset}, uv, Pack(last, alpha * weight));
    }
}
// One pending segment lets adjacent strips share their entire edge, including
// the halo. This avoids both dark cracks and brighter overlaps along a curve.
class ScreenPolyline {
    ImDrawList &draw_;
    Color color_;
    float alpha_, width_, strength_;
    bool glow_, pending_{};
    ImVec2 start_{}, end_{}, startJoin_{};

  public:
    static ImVec2 Join(ImVec2 a, ImVec2 b, ImVec2 c) noexcept {
        const float dx = b.x - a.x, dy = b.y - a.y, ex = c.x - b.x, ey = c.y - b.y;
        const float first = std::sqrt(dx * dx + dy * dy), last = std::sqrt(ex * ex + ey * ey);
        if (first < .001f)
            return last < .001f ? ImVec2{} : ImVec2{-ey / last, ex / last};
        if (last < .001f)
            return {-dy / first, dx / first};
        ImVec2 sum{-dy / first - ey / last, dx / first + ex / last};
        const float length = std::sqrt(sum.x * sum.x + sum.y * sum.y);
        if (length < .001f)
            return {-dy / first, dx / first};
        const float factor = (std::min)(2.f / (length * length), 2.5f / length);
        return {sum.x * factor, sum.y * factor};
    }

  public:
    ScreenPolyline(ImDrawList &draw, Color color, float alpha, float width, bool glow, float strength)
        : draw_(draw), color_(color), alpha_(alpha), width_(width), strength_(strength), glow_(glow) {}
    void Add(ImVec2 a, ImVec2 b) {
        const float dx = b.x - a.x, dy = b.y - a.y;
        if (dx * dx + dy * dy < .0001f)
            return;
        ImVec2 join{};
        if (pending_) {
            const float gapX = a.x - end_.x, gapY = a.y - end_.y;
            if (gapX * gapX + gapY * gapY < .0001f) {
                a = end_;
                join = Join(start_, a, b);
            }
            ScreenStroke(draw_, start_, end_, color_, color_, alpha_, width_, glow_, strength_, startJoin_, join);
        }
        start_ = a;
        end_ = b;
        startJoin_ = join;
        pending_ = true;
    }
    void Flush() {
        if (pending_)
            ScreenStroke(draw_, start_, end_, color_, color_, alpha_, width_, glow_, strength_, startJoin_);
        pending_ = false;
        startJoin_ = {};
    }
};
template <class PointAt>
inline void Path(ImDrawList &draw, std::size_t count, PointAt point, Color color, float alpha, bool glow,
                 const Matrix4x4 &matrix, Viewport viewport, float width, float strength) {
    ScreenPolyline line(draw, color, alpha, width, glow, strength);
    for (std::size_t i = 1; i < count; ++i) {
        ImVec2 a, b;
        if (ScreenLine(point(i - 1), point(i), matrix, viewport, a, b))
            line.Add(a, b);
        else
            line.Flush();
    }
    line.Flush();
}
inline void Segment(ImDrawList &draw, Vector3 a, Vector3 b, Color color, float alpha, bool glow,
                    const Matrix4x4 &matrix, Viewport viewport, float width = 1.6f, float strength = 1) {
    ImVec2 p, q;
    if (ScreenLine(a, b, matrix, viewport, p, q))
        ScreenStroke(draw, p, q, color, color, alpha, width, glow, strength);
}
inline void Draw(ImDrawList &draw, const Trails &trails, const Prediction &prediction, const Tracers &shots,
                 bool showTrails, bool showPrediction, bool showShots, Shots filter, std::uint32_t local, int team,
                 const Matrix4x4 &matrix, Viewport viewport, double now, float opacity = 1,
                 const PathStyle &style = {}) {
    draw.Flags |= ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill;
    if (showTrails)
        for (const auto &p : trails.Paths()) {
            const float alpha = (p.active ? 1 : Fade(now, p.lastSeen, 5)) * opacity;
            if (alpha <= 0)
                continue;
            Path(
                draw, p.points.count + (p.active ? 1 : 0),
                [&](std::size_t i) { return i == p.points.count ? p.head : p.points[i].position; },
                style.UtilityColor(p.type, false), alpha, style.trailGlow != 0, matrix, viewport, style.trailWidth,
                style.trailStrength);
        }
    if (showPrediction && prediction.valid) {
        const auto color = style.UtilityColor(prediction.type, true);
        Path(
            draw, prediction.count, [&](std::size_t i) { return prediction.points[i]; }, color, opacity,
            style.previewGlow != 0, matrix, viewport, style.previewWidth, style.previewStrength);
        auto marker = [&](Vector3 position, float radius) {
            Vector2 p;
            if (WorldToScreen(position, matrix, viewport, p)) {
                if (style.previewGlow && style.previewStrength > 0) {
                    draw.AddCircle({p.x, p.y}, radius, Pack(color, opacity * .1f * style.previewStrength), 24, 9);
                    draw.AddCircle({p.x, p.y}, radius, Pack(color, opacity * .25f * style.previewStrength), 24, 4);
                }
                draw.AddCircle({p.x, p.y}, radius, Pack(color, opacity), 24, style.previewWidth);
            }
        };
        for (unsigned i = 0; i < prediction.bounceCount; ++i)
            marker(prediction.bounces[i], 3);
        if (prediction.finished)
            marker(prediction.landing, 5);
    }
    if (showShots)
        for (std::size_t i = 0; i < shots.Lines().count; ++i) {
            const auto &shot = shots.Lines()[i];
            if (!Matches(shot, filter, local, team))
                continue;
            const float alpha = Fade(now, shot.time, style.shotLifetime) * opacity;
            if (alpha <= 0)
                continue;
            ImVec2 start, end;
            if (!ScreenLine(shot.start, shot.end, matrix, viewport, start, end))
                continue;
            // A real eye-to-impact segment can project to a single point while
            // aiming along it. Keep its actual impact visible without inventing a muzzle.
            const float dx = end.x - start.x, dy = end.y - start.y;
            if (dx * dx + dy * dy < 2.25f) {
                draw.AddCircleFilled(end, std::max(1.5f, style.shotWidth), Pack(style.shotCore, alpha), 12);
                continue;
            }
            ScreenStroke(draw, start, end, style.shotStart, style.shotEnd, alpha, style.shotWidth, style.shotGlow != 0,
                         style.shotStrength);
            if (style.shotGlow && style.shotStrength > 0)
                draw.AddLine(start, end, Pack(style.shotCore, alpha), std::max(.7f, style.shotWidth * .38f));
        }
}
} // namespace awareness::flight
