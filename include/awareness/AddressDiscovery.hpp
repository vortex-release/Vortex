#pragma once
#include "OverlayApi.hpp"

namespace awareness {
enum class ScanStatus : std::uint32_t {
    NotFound, Found, Ambiguous, NotRipRelative, UnreadableTarget, NullEntityList, IncompleteScan
};
struct DiscoveredAddresses {
    std::uint32_t size{sizeof(DiscoveredAddresses)}, version{ApiVersion};
    ScanStatus entityStatus{ScanStatus::NotFound}, viewStatus{ScanStatus::NotFound};
    std::uint32_t entityMatches{}, viewMatches{}; // Counts saturate at 2 (meaning 2 or more).
    std::uintptr_t entityListInstruction{}, entityListSlot{}, entityList{};
    std::uintptr_t viewMatrixInstruction{}, viewMatrix{};
};
}

#if defined(_WIN32)
// Scan a loaded x64 module in this process. nullptr defaults to L"client.dll".
// This is explicit startup discovery, never a per-Present operation. It does not
// attach to a process or read any entity fields. Both statuses must be Found.
AWARENESS_API HRESULT __cdecl AwarenessFindAddresses(const wchar_t* moduleName,
    awareness::DiscoveredAddresses* addresses) noexcept;
#endif
