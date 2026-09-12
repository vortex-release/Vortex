#pragma once
#include "combat_features.hpp"
#include "scene_style.hpp"
#include <Windows.h>
namespace awareness::cs2 {
HRESULT StartWorldEffects() noexcept;
HRESULT StopWorldEffects() noexcept;
void ConfigureWorldEffects(const combat::Options &, const combat::WorldSnapshot &, const scene::Options &,
                           bool ready) noexcept;
void PauseWorldEffects() noexcept;
} // namespace awareness::cs2
