#pragma once
#include "Math.hpp"
namespace awareness {
struct HudFooter {
    float distanceY{}, weaponX{}, weaponY{}, weaponWidth{}, weaponHeight{};
};
inline HudFooter CalculateHudFooter(ScreenBox box, Viewport viewport, float gap, float fontPixels, bool distance,
                                    bool weapon) noexcept {
    HudFooter result;
    if (!Valid(viewport) || !std::isfinite(fontPixels) || fontPixels <= 0)
        return result;
    if (weapon) {
        result.weaponWidth = (std::min)(viewport.width, std::clamp((box.max.x - box.min.x) * 1.35f, 36.f, 90.f));
        result.weaponHeight = result.weaponWidth * .375f;
    }
    const float textHeight = distance ? fontPixels + 3.f : 0.f;
    result.distanceY = (std::max)(
        viewport.y, (std::min)(box.max.y + gap, viewport.y + viewport.height - textHeight - result.weaponHeight));
    result.weaponX = std::clamp((box.min.x + box.max.x - result.weaponWidth) * .5f, viewport.x,
                                viewport.x + viewport.width - result.weaponWidth);
    result.weaponY = result.distanceY + textHeight;
    return result;
}
} // namespace awareness
