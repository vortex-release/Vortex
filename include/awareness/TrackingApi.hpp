#pragma once
#include "OverlayApi.hpp"
#include "CameraTracking.hpp"
#include "InputBinding.hpp"

namespace awareness {
inline constexpr std::uint32_t TrackingApiVersion = 1;
struct TrackingConfiguration {
    std::uint32_t size{sizeof(TrackingConfiguration)}, version{TrackingApiVersion};
    std::uint32_t enabled{}, hotkey{0x02}; // VK_RBUTTON; enable from the Targeting Calibration tab.
    float fovDegrees{30.f}, interpolationSpeed{8.f};
};
// Optional host path. CS2 uses its own camera and actual hold key.
// Submit after AwarenessSubmitFrame on the render thread, before that frame's Present.
struct CameraInput {
    std::uint32_t size{sizeof(CameraInput)}, version{TrackingApiVersion};
    camera::Angles angles{}; // Y-up/+Z-forward, degrees.
    std::uint32_t activationHeld{};
};
enum class TrackingStatus : std::uint32_t {
    Disabled,
    MenuOpen,
    WaitingForData,
    WaitingForCamera,
    WaitingForHotkey,
    NoTarget,
    Following,
    WriteBlocked
};
struct TrackingState {
    std::uint32_t size{sizeof(TrackingState)}, version{TrackingApiVersion};
    std::uint64_t frameNumber{};
    std::uint32_t active{}, targetId{0xFFFFFFFFu};
    camera::Angles angles{};
    TrackingStatus status{TrackingStatus::Disabled};
    std::int32_t result{}; // HRESULT.
};
inline bool ValidTrackingConfiguration(const TrackingConfiguration &value) noexcept {
    return value.size == sizeof(value) && value.version == TrackingApiVersion && value.enabled <= 1 &&
           binding::Valid(value.hotkey) &&
           std::isfinite(value.fovDegrees) && value.fovDegrees >= 1.f && value.fovDegrees <= 90.f &&
           std::isfinite(value.interpolationSpeed) && value.interpolationSpeed >= 0.f &&
           value.interpolationSpeed <= camera::InstantFollowSpeed;
}
inline const char *TrackingStatusText(TrackingStatus status) noexcept {
    switch (status) {
    case TrackingStatus::Disabled:
        return "Tracking disabled or speed is zero";
    case TrackingStatus::MenuOpen:
        return "Close the menu to track";
    case TrackingStatus::WaitingForData:
        return "Waiting for fresh player data";
    case TrackingStatus::WaitingForCamera:
        return "Waiting for a valid camera";
    case TrackingStatus::WaitingForHotkey:
        return "Ready: hold the tracking key";
    case TrackingStatus::NoTarget:
        return "No eligible target inside the FOV limit";
    case TrackingStatus::Following:
        return "Following target";
    case TrackingStatus::WriteBlocked:
        return "Camera changed or write was unavailable; retrying next frame";
    }
    return "Unknown tracking state";
}
} // namespace awareness
#if defined(_WIN32)
AWARENESS_API HRESULT __cdecl AwarenessSetTrackingConfiguration(const awareness::TrackingConfiguration *) noexcept;
AWARENESS_API HRESULT __cdecl AwarenessGetTrackingConfiguration(awareness::TrackingConfiguration *) noexcept;
AWARENESS_API HRESULT __cdecl AwarenessSubmitCameraInput(const awareness::CameraInput *) noexcept;
AWARENESS_API HRESULT __cdecl AwarenessGetTrackingState(awareness::TrackingState *) noexcept;
#endif
