#pragma once
#include <awareness/TrackingApi.hpp>
#include <array>
#include "tracking_profiles.hpp"

namespace awareness {
inline Vector3 TrackingPoint(Vector3 point, bool cs2Coordinates) noexcept {
    // CS2 Z-up/+X-forward -> portable Y-up/+Z-forward.
    return cs2Coordinates ? Vector3{point.y, point.z, point.x} : point;
}
inline std::optional<camera::Selection>
TrackFrame(const FrameSnapshot &frame, camera::Angles &angles, const TrackingConfiguration &config, float seconds,
           bool held, bool cs2Coordinates, TargetBone selectedBone = TargetBone::Head,
           camera::TargetTeams teams = camera::TargetTeams::Opponents, const tracking::Sample *motion = nullptr,
           const tracking::Options *prediction = nullptr, double now = 0) noexcept {
    if (!ValidTrackingConfiguration(config) || frame.entityCount > MaxEntities)
        return std::nullopt;
    std::array<camera::Target, MaxEntities> targets{};
    for (std::uint32_t i = 0; i < frame.entityCount; ++i) {
        const auto &entity = frame.entities[i];
        auto point = ResolveTargetBone(frame.bones[i], selectedBone);
        if (point && motion && prediction && motion->motion[i].id == entity.id)
            point = tracking::Predict(*point, motion->motion[i], now - motion->time, *prediction);
        const bool valid = point && entity.valid && (!cs2Coordinates || entity.team == 2 || entity.team == 3);
        targets[i] = {entity.id,     entity.team,
                      entity.health, TrackingPoint(point.value_or(Vector3{}), cs2Coordinates),
                      valid,         entity.dormant != 0};
    }
    return camera::Update(TrackingPoint(frame.cameraOrigin, cs2Coordinates), angles,
                          std::span<const camera::Target>(targets.data(), frame.entityCount), frame.localEntityId,
                          frame.localTeam, {config.enabled != 0, config.fovDegrees, config.interpolationSpeed, teams},
                          seconds, held);
}
} // namespace awareness
