#pragma once
#include "combat_features.hpp"
#include <Windows.h>
namespace awareness::combat {
// Preserve sub-pixel movement. Rounding each frame to an integer loses nearly all
// compensation at high frame rates or with smooth profiles.
class MouseRecoil {
    double x_{}, y_{};

  public:
    void Reset() noexcept { x_ = y_ = 0; }
    template <class Send>
    HRESULT Apply(Vector3 correction, float sensitivity, float yaw, float pitch, Send send) noexcept {
        if (!Finite(correction) || !std::isfinite(sensitivity) || sensitivity < .01f || sensitivity > 100 ||
            !std::isfinite(yaw) || yaw < .001f || yaw > .1f || !std::isfinite(pitch) || pitch < .001f || pitch > .1f) {
            Reset();
            return E_INVALIDARG;
        }
        // Positive mouse Y looks down; positive punch pitch also looks down.
        const double x = x_ + correction.y / (sensitivity * yaw);
        const double y = y_ - correction.x / (sensitivity * pitch);
        if (std::abs(x) > 32767 || std::abs(y) > 32767) {
            Reset();
            return E_INVALIDARG;
        }
        const auto dx = static_cast<LONG>(std::lround(x)), dy = static_cast<LONG>(std::lround(y));
        if (!dx && !dy) {
            x_ = x;
            y_ = y;
            return S_FALSE;
        }
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = dx;
        input.mi.dy = dy;
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_MOVE_NOCOALESCE;
        if (send(input) != 1)
            return E_ACCESSDENIED; // caller restores only this correction
        x_ = x - dx;
        y_ = y - dy;
        return S_OK;
    }
};
} // namespace awareness::combat
