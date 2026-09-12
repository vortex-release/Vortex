#pragma once
#include "assist_features.hpp"
#include <awareness/InputBinding.hpp>
namespace awareness::assist {
inline constexpr ULONG_PTR InputTag = 0x56545841;
class PhysicalInput {
    Keys keys_;

  public:
    void Seed(unsigned key, bool held) noexcept {
        if (key < 256)
            keys_.down[key] = held;
    }
    void RelativeMouse(LONG dx, USHORT flags = MOUSE_MOVE_RELATIVE, ULONG_PTR extra = 0) noexcept {
        if (!dx || (flags & MOUSE_MOVE_ABSOLUTE) || extra == InputTag)
            return;
        // Bounded accumulation keeps snapshots non-consuming for both workers.
        constexpr std::int64_t limit = std::int64_t{1} << 55;
        keys_.mouseTravelX = std::clamp(keys_.mouseTravelX + dx, -limit, limit);
        ++keys_.mouseSequence;
    }
    void Event(UINT message, WPARAM value, LPARAM data, ULONG_PTR extra = 0, bool gameplayShortcuts = true) noexcept {
        if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !value)) {
            keys_ = {};
            return;
        }
        if (extra == InputTag)
            return;
        if (message == WM_INPUT) {
            RAWINPUT raw{};
            UINT size = sizeof(raw);
            const auto copied =
                GetRawInputData(reinterpret_cast<HRAWINPUT>(data), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER));
            if (copied != UINT(-1) && copied >= sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE) &&
                raw.header.dwType == RIM_TYPEMOUSE)
                RelativeMouse(raw.data.mouse.lLastX, raw.data.mouse.usFlags, raw.data.mouse.ulExtraInformation);
            return;
        }
        const bool initial = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !(data & (1LL << 30));
        if (initial && gameplayShortcuts) {
            if (value == 'Y' || value == 'U')
                keys_.textInput = true;
            if (value == VK_OEM_3)
                keys_.textInput = !keys_.textInput;
            if (value == VK_RETURN || value == VK_ESCAPE)
                keys_.textInput = false;
        }
        const auto key = binding::Button(message, value, data);
        if (key && key < 256)
            keys_.down[key] = binding::Down(message);
    }
    Keys Snapshot() const noexcept { return keys_; }
};
} // namespace awareness::assist
