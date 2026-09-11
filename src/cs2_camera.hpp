#pragma once
#include "local_memory.hpp"
#include <awareness/CameraTracking.hpp>
#include <bit>
#include <intrin.h>

namespace awareness::cs2 {
struct NativeViewAngles {
    float pitch{}, yaw{}, roll{};
};
inline bool ReadViewAngles(std::uintptr_t address, NativeViewAngles &value) noexcept {
    LocalMemory memory;
    NativeViewAngles candidate;
    if (!LocalMemory::Read(&memory, address, &candidate, sizeof(candidate)) || !std::isfinite(candidate.pitch) ||
        !std::isfinite(candidate.yaw) || !std::isfinite(candidate.roll) || std::abs(candidate.pitch) > 89.1f ||
        std::abs(candidate.yaw) > 180.1f)
        return false;
    value = candidate;
    return true;
}
inline camera::Angles TrackingAngles(NativeViewAngles value) noexcept {
    return camera::Normalize({-value.pitch, value.yaw}); // CS2 positive pitch looks down.
}
// Change only pitch/yaw, preserving roll. CAS preserves intervening mouse/host angle changes.
inline HRESULT CommitViewAngles(std::uintptr_t address, NativeViewAngles expected, camera::Angles desired) noexcept {
    if (!address || address % alignof(long long) || !camera::Finite(desired) || !std::isfinite(expected.pitch) ||
        !std::isfinite(expected.yaw))
        return E_INVALIDARG;
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(reinterpret_cast<void *>(address), &region, sizeof(region)) || region.State != MEM_COMMIT ||
        (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(region.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return E_ACCESSDENIED;
    const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
    if (address < base || region.RegionSize < sizeof(long long) ||
        address - base > region.RegionSize - sizeof(long long))
        return E_ACCESSDENIED;
    desired = camera::Normalize(desired);
    const std::array<float, 2> before{expected.pitch, expected.yaw}, after{-desired.pitch, desired.yaw};
    const auto expectedBits = std::bit_cast<long long>(before), desiredBits = std::bit_cast<long long>(after);
#if defined(_MSC_VER)
    __try {
        const auto previous =
            _InterlockedCompareExchange64(reinterpret_cast<volatile long long *>(address), desiredBits, expectedBits);
        return previous == expectedBits ? S_OK : HRESULT_FROM_WIN32(ERROR_RETRY);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_ACCESSDENIED;
    }
#else
    return E_NOTIMPL;
#endif
}
} // namespace awareness::cs2
