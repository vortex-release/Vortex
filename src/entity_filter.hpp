#pragma once
#include <awareness/OverlayApi.hpp>
namespace awareness {
inline bool AwarenessTeam(int target, int local, std::uint32_t mode) noexcept {
    switch (mode) {
    case 0:
        return true;
    case 1:
        return local != 0 && target != 0 && target != local;
    case 2:
        return local != 0 && target == local;
    case 3:
        return target == 2;
    case 4:
        return target == 3;
    default:
        return false;
    }
}
inline float EntityOpacity(const FrameSnapshot &frame, const EntitySnapshot &entity,
                           const Configuration &config) noexcept {
    if (!config.enabled || !entity.valid || entity.dormant || entity.id == frame.localEntityId ||
        !std::isfinite(entity.health) || entity.health <= 0 || !std::isfinite(entity.maxHealth) ||
        entity.maxHealth <= 0 || !Finite(entity.origin) || !Finite(frame.cameraOrigin))
        return 0;
    const bool known = frame.localTeam != 0 && entity.team != 0;
    const bool teammate = known && entity.team == frame.localTeam;
    if (config.teamFilter == TeamFilter::TeammatesOnly && !teammate)
        return 0;
    if (config.teamFilter == TeamFilter::OpponentsOnly && (!known || teammate))
        return 0;
    const float meters = Distance(frame.cameraOrigin, entity.origin) / config.worldUnitsPerMeter;
    return config.opacity * DistanceAlpha(meters, config.fadeStartMeters, config.maxDistanceMeters);
}
} // namespace awareness
