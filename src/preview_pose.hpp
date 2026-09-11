#pragma once
#include <awareness/OverlayApi.hpp>
#include <array>
namespace awareness {
struct PreviewJoint {
    Vector3 position;
    float scale;
    std::array<float, 4> rotation;
};
// Common world-skeleton joints verified against the bundled SAS/Phoenix layout.
inline constexpr std::size_t PreviewJointCount = 25;
struct PreviewPose {
    bool valid{};
    std::uint32_t validMask{};
    EntitySnapshot entity{};
    std::uint32_t weapon{};
    std::array<PreviewJoint, PreviewJointCount> joints{};
};
} // namespace awareness
