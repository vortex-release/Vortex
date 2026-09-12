#pragma once
#include <Windows.h>
#include <cstdint>
#include <span>
namespace awareness::cs2::frame {
// Current client build 14181: verified from the switch/profiler lifetimes in
// FrameStageNotify, not the outdated stage labels in either donor project.
enum class Stage : int { NetworkStart = 5, PostDataStart = 6, PostDataEnd = 7, NetworkEnd = 8 };
using Callback = void (*)(int) noexcept;
struct Status {
    bool connected{};
    std::uint32_t failedCallbacks{};
    std::uint64_t callbacks{};
};
HRESULT Start(std::span<const Callback>) noexcept;
HRESULT Stop() noexcept;
Status Diagnostics() noexcept;
} // namespace awareness::cs2::frame
