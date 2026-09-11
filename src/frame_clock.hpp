#pragma once
#include <chrono>
namespace awareness {
// One high-resolution monotonic timebase for samples, interpolation and fades.
// GetTickCount64 remains suitable for coarse watchdog deadlines, not frame deltas.
inline double FrameSeconds() noexcept {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace awareness
