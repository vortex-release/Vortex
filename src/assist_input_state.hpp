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
    void Event(UINT message, WPARAM value, LPARAM data, ULONG_PTR extra = 0) noexcept {
        if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !value)) {
            keys_ = {};
            return;
        }
        if (extra == InputTag)
            return;
        const bool initial = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !(data & (1LL << 30));
        if (initial) {
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
