#pragma once
#include "weather_options.hpp"
#include <Windows.h>

namespace awareness::cs2 {
HRESULT StartWeather() noexcept;
void ConfigureWeather(const weather::Options &, bool fresh) noexcept;
// Call after original FrameStageNotify. Verified current NetworkEnd is stage 8.
void TickWeather(int stage) noexcept;
// Must run while the shared game-frame dispatcher is still subscribed. A busy
// result retains state for a later retry; it never destroys particles off-thread.
HRESULT StopWeather() noexcept;
weather::Diagnostics ReadWeatherDiagnostics() noexcept;
} // namespace awareness::cs2
