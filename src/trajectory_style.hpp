#pragma once
#include <awareness/Trajectories.hpp>
namespace awareness::flight {
struct PathStyle {
    std::uint32_t trailGlow{1}, shotGlow{1}, previewGlow{};
    float trailStrength{2.f}, shotStrength{2.4f}, previewStrength{1.f};
    float shotLifetime{.75f};
    float trailWidth{2.4f}, shotWidth{2.2f}, previewWidth{1.6f};
    Color trailHE{Tint(Utility::HE)}, trailSmoke{Tint(Utility::Smoke)}, trailFlash{Tint(Utility::Flash)},
        trailFire{Tint(Utility::Fire)}, trailDecoy{Tint(Utility::Decoy)};
    Color previewHE{trailHE}, previewSmoke{trailSmoke}, previewFlash{trailFlash}, previewFire{trailFire},
        previewDecoy{trailDecoy};
    Color shotStart{1.f, .75f, .12f, 1}, shotEnd{1, 1, 1, 1}, shotCore{1, 1, 1, 1};
    Color UtilityColor(Utility type, bool preview) const noexcept {
        switch (type) {
        case Utility::HE:
            return preview ? previewHE : trailHE;
        case Utility::Smoke:
            return preview ? previewSmoke : trailSmoke;
        case Utility::Flash:
            return preview ? previewFlash : trailFlash;
        case Utility::Fire:
            return preview ? previewFire : trailFire;
        default:
            return preview ? previewDecoy : trailDecoy;
        }
    }
};
inline bool ValidPathStyle(const PathStyle &s) noexcept {
    const auto range = [](float x, float low, float high) { return std::isfinite(x) && x >= low && x <= high; };
    if (s.trailGlow > 1 || s.shotGlow > 1 || s.previewGlow > 1 || !range(s.trailStrength, 0, 3) ||
        !range(s.shotStrength, 0, 3) || !range(s.previewStrength, 0, 3) || !range(s.trailWidth, 1, 5) ||
        !range(s.shotLifetime, .1f, 5.f) || !range(s.shotWidth, 1, 5) || !range(s.previewWidth, 1, 5))
        return false;
    for (const Color c : {s.trailHE, s.trailSmoke, s.trailFlash, s.trailFire, s.trailDecoy, s.previewHE, s.previewSmoke,
                          s.previewFlash, s.previewFire, s.previewDecoy, s.shotStart, s.shotEnd, s.shotCore})
        if (!range(c.r, 0, 1) || !range(c.g, 0, 1) || !range(c.b, 0, 1) || !range(c.a, 0, 1))
            return false;
    return true;
}
} // namespace awareness::flight
