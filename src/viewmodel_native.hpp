#pragma once
#include "camera_visuals.hpp"
#include <Windows.h>
namespace awareness::cs2 {
HRESULT StartViewmodel() noexcept;
HRESULT StopViewmodel() noexcept;
void ConfigureViewmodel(const camera_visuals::Options &, bool fresh) noexcept;
} // namespace awareness::cs2
