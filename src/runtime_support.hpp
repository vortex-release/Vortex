#pragma once
#include <Windows.h>
void StartCs2Automatically() noexcept;
void OverlayLog(const char* message) noexcept;
bool BootstrapStopRequested() noexcept;
void RequestBootstrapStop() noexcept;
void WaitForOverlayBootstrap() noexcept;
HMODULE OverlayModule() noexcept;
