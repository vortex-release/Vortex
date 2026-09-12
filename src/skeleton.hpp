#pragma once
#include <awareness/OverlayApi.hpp>
#include <array>
namespace awareness::skeleton {
// Common player rig from the same verified layout used by the model preview.
inline constexpr unsigned JointCount = 23;
inline constexpr std::array<std::array<unsigned, 2>, 18> Links{{{1, 3},
                                                                {3, 4},
                                                                {4, 6},
                                                                {6, 7},
                                                                {6, 8},
                                                                {8, 9},
                                                                {9, 10},
                                                                {10, 11},
                                                                {6, 12},
                                                                {12, 13},
                                                                {13, 14},
                                                                {14, 15},
                                                                {1, 17},
                                                                {17, 18},
                                                                {18, 19},
                                                                {1, 20},
                                                                {20, 21},
                                                                {21, 22}}};
struct Options {
    std::uint32_t enabled{}, teamColor{1}, outline{1}, joints{};
    float width{1.35f}, opacity{.9f};
    Color color{.95f, .95f, .98f, 1};
};
inline bool Valid(const Options &o) noexcept {
    if (o.enabled > 1 || o.teamColor > 1 || o.outline > 1 || o.joints > 1 || !std::isfinite(o.width) || o.width < .5f ||
        o.width > 4 || !std::isfinite(o.opacity) || o.opacity < 0 || o.opacity > 1)
        return false;
    for (float c : {o.color.r, o.color.g, o.color.b, o.color.a})
        if (!std::isfinite(c) || c < 0 || c > 1)
            return false;
    return true;
}
struct Pose {
    std::uint32_t entity{0xffffffffu}, handle{}, validMask{};
    std::array<Vector3, JointCount> positions{};
};
struct Frame {
    std::array<Pose, MaxEntities> poses{};
    unsigned count{};
};
inline bool Segment(const Pose &pose, unsigned a, unsigned b) noexcept {
    if (a >= JointCount || b >= JointCount || !(pose.validMask & (1u << a)) || !(pose.validMask & (1u << b)) ||
        !Finite(pose.positions[a]) || !Finite(pose.positions[b]))
        return false;
    const float length = Distance(pose.positions[a], pose.positions[b]);
    return length >= .05f && length <= 64.f;
}
} // namespace awareness::skeleton
