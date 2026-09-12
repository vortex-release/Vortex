#pragma once
#include "scoreboard.hpp"
#include <Windows.h>
namespace awareness::cs2 {
struct ScoreboardStatus {
    bool connected{}, active{};
    std::uint64_t updates{}, failures{};
};
void StartScoreboard() noexcept;
void ConfigureScoreboard(const scoreboard::Options &, bool fresh) noexcept;
void TickScoreboard(int stage) noexcept;
HRESULT StopScoreboard() noexcept;
ScoreboardStatus GetScoreboardStatus() noexcept;
} // namespace awareness::cs2
