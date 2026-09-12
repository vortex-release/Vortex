#pragma once
#include "skeleton.hpp"
#include "trajectory_draw.hpp"
namespace awareness::skeleton {
inline unsigned Draw(ImDrawList &draw, const Pose &pose, std::uint32_t entity, const Matrix4x4 &matrix,
                     Viewport viewport, const Options &options, Color team, Color shadow, float alpha) {
    if (!options.enabled || !Valid(options) || pose.entity != entity || !std::isfinite(alpha) || alpha <= 0)
        return 0;
    alpha *= options.opacity;
    const auto color = flight::Pack(options.teamColor ? team : options.color, alpha);
    const auto border = flight::Pack(shadow, alpha * .7f);
    unsigned segments{};
    std::uint32_t used{};
    for (auto link : Links) {
        if (!Segment(pose, link[0], link[1]))
            continue;
        ImVec2 a, b;
        if (!flight::ScreenLine(pose.positions[link[0]], pose.positions[link[1]], matrix, viewport, a, b))
            continue;
        if (options.outline)
            draw.AddLine(a, b, border, options.width + 1.5f);
        draw.AddLine(a, b, color, options.width);
        used |= (1u << link[0]) | (1u << link[1]);
        ++segments;
    }
    if (options.joints)
        for (unsigned i = 0; i < JointCount; ++i) {
            Vector2 point;
            if (!(used & (1u << i)) || !WorldToScreen(pose.positions[i], matrix, viewport, point) ||
                point.x < viewport.x || point.x > viewport.x + viewport.width || point.y < viewport.y ||
                point.y > viewport.y + viewport.height)
                continue;
            if (options.outline)
                draw.AddCircleFilled({point.x, point.y}, options.width + 1.2f, border, 8);
            draw.AddCircleFilled({point.x, point.y}, options.width + .35f, color, 8);
        }
    return segments;
}
} // namespace awareness::skeleton
