#pragma once
#include <awareness/OverlayApi.hpp>
#include <cstring>
#include <cstddef>
namespace awareness {
inline bool ValidColor(Color c) noexcept {
    for (float x : {c.r, c.g, c.b, c.a})
        if (!std::isfinite(x) || x < 0.f || x > 1.f)
            return false;
    return true;
}
inline bool ValidConfiguration(const Configuration &c) noexcept {
    // An older host may provide only the common header of a different struct.
    // Reject its layout before inspecting any fields beyond that header.
    if (c.size != sizeof(c) || c.version != ApiVersion)
        return false;
    if (std::memchr(c.fontPath, '\0', sizeof(c.fontPath)) == nullptr)
        return false;
    if (c.toggleKey > 255 || c.alternateToggleKey > 255 || c.teamFilter > TeamFilter::TeammatesOnly ||
        c.healthBar > HealthBar::Horizontal)
        return false;
    for (auto flag :
         {c.enabled, c.boxes, c.lines, c.healthBars, c.names, c.distances, c.colorBoxesByHealth, c.clearOverlayDepth})
        if (flag > 1)
            return false;
    for (float x : {c.boxThickness, c.lineThickness, c.barThickness, c.fontPixels, c.worldUnitsPerMeter,
                    c.fadeStartMeters, c.maxDistanceMeters, c.opacity, c.lowHealthThreshold, c.mediumHealthThreshold})
        if (!std::isfinite(x))
            return false;
    if (c.boxThickness <= 0.f || c.boxThickness > 20.f || c.lineThickness <= 0.f || c.lineThickness > 20.f ||
        c.barThickness <= 0.f || c.barThickness > 50.f || c.fontPixels < 8.f || c.fontPixels > 96.f ||
        c.worldUnitsPerMeter <= 0.f || c.fadeStartMeters < 0.f || c.maxDistanceMeters <= c.fadeStartMeters ||
        c.opacity < 0.f || c.opacity > 1.f || c.lowHealthThreshold < 0.f ||
        c.mediumHealthThreshold <= c.lowHealthThreshold || c.mediumHealthThreshold > 1.f)
        return false;
    for (auto color :
         {c.teammate, c.opponent, c.neutral, c.healthy, c.medium, c.low, c.text, c.outline, c.barBackground})
        if (!ValidColor(color))
            return false;
    return true;
}
inline bool ValidFrame(const FrameSnapshot &f) noexcept {
    if (f.size != sizeof(f) || f.version != FrameVersion || f.entityCount > MaxEntities || !Finite(f.cameraOrigin))
        return false;
    for (const auto &row : f.viewProjection.m)
        for (float x : row)
            if (!std::isfinite(x))
                return false;
    const auto v = f.viewport;
    return (v.width == 0.f && v.height == 0.f && v.x == 0.f && v.y == 0.f) || Valid(v);
}
inline bool CopySubmittedFrame(const FrameSnapshot *input, FrameSnapshot &output) noexcept {
    if (!input)
        return false;
    constexpr auto legacySize = offsetof(FrameSnapshot, weaponDefinitionIndices);
    if (input->version == ApiVersion && input->size == legacySize) {
        output = {};
        std::memcpy(&output, input, legacySize);
        output.size = sizeof(output);
        output.version = FrameVersion;
    } else if (input->version == 2 && input->size == offsetof(FrameSnapshot, bones)) {
        output = {};
        std::memcpy(&output, input, offsetof(FrameSnapshot, bones));
        output.size = sizeof(output);
        output.version = FrameVersion;
    } else if (input->version == FrameVersion && input->size == sizeof(FrameSnapshot))
        output = *input;
    else
        return false;
    return ValidFrame(output);
}
} // namespace awareness
