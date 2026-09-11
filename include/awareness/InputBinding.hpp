#pragma once
#include <Windows.h>
#include <Windowsx.h>
#include <array>
#include <cstdint>
#include <string>

namespace awareness::binding {
inline constexpr std::uint32_t WheelUp = 256, WheelDown = 257, WheelLeft = 258, WheelRight = 259;
inline constexpr bool Valid(std::uint32_t key) noexcept {
    return key > 0 && key <= WheelRight;
}
inline std::uint32_t Button(UINT message, WPARAM value, LPARAM data) noexcept {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        return VK_LBUTTON;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
        return VK_RBUTTON;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
        return VK_MBUTTON;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
        return GET_XBUTTON_WPARAM(value) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
    case WM_MOUSEWHEEL:
        return GET_WHEEL_DELTA_WPARAM(value) > 0 ? WheelUp : GET_WHEEL_DELTA_WPARAM(value) < 0 ? WheelDown : 0;
    case WM_MOUSEHWHEEL:
        return GET_WHEEL_DELTA_WPARAM(value) > 0 ? WheelRight : GET_WHEEL_DELTA_WPARAM(value) < 0 ? WheelLeft : 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (value == VK_SHIFT)
            return (static_cast<UINT>(data) >> 16 & 0xff) == 0x36 ? VK_RSHIFT : VK_LSHIFT;
        if (value == VK_CONTROL)
            return data & (1LL << 24) ? VK_RCONTROL : VK_LCONTROL;
        if (value == VK_MENU)
            return data & (1LL << 24) ? VK_RMENU : VK_LMENU;
        return value > 0 && value < 256 ? static_cast<std::uint32_t>(value) : 0;
    default:
        return 0;
    }
}
inline bool Down(UINT message) noexcept {
    return message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_LBUTTONDOWN ||
           message == WM_LBUTTONDBLCLK || message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK ||
           message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK || message == WM_XBUTTONDOWN ||
           message == WM_XBUTTONDBLCLK || message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL;
}
inline std::string Name(std::uint32_t key) {
    switch (key) {
    case VK_LBUTTON:
        return "Left mouse";
    case VK_RBUTTON:
        return "Right mouse";
    case VK_MBUTTON:
        return "Middle mouse";
    case VK_XBUTTON1:
        return "Mouse 4";
    case VK_XBUTTON2:
        return "Mouse 5";
    case WheelUp:
        return "Wheel up";
    case WheelDown:
        return "Wheel down";
    case WheelLeft:
        return "Wheel left";
    case WheelRight:
        return "Wheel right";
    }
    UINT scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
    LONG param = static_cast<LONG>((scan & 0xff) << 16);
    if (scan & 0xff00)
        param |= 1L << 24;
    wchar_t wide[128]{};
    char utf8[512]{};
    if (GetKeyNameTextW(param, wide, 128) && WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, 512, nullptr, nullptr))
        return utf8;
    return "Key " + std::to_string(key);
}
// WindowBridge serializes this state across the window and render threads.
class Capture {
    std::array<bool, 256> down_{}, blocked_{}, release_{};
    bool waiting_{}, text_{};
    std::uint32_t result_{};

  public:
    void Begin() noexcept {
        blocked_ = down_;
        result_ = 0;
        waiting_ = true;
        text_ = false;
    }
    void BlockHeld(std::uint32_t key) noexcept {
        if (key < 256)
            blocked_[key] = true;
    }
    void Cancel() noexcept {
        waiting_ = false;
        result_ = 0;
        down_ = {};
        blocked_ = {};
        release_ = {};
        text_ = false;
    }
    bool Waiting() const noexcept { return waiting_; }
    bool Held(std::uint32_t key) const noexcept { return key < 256 && down_[key]; }
    std::uint32_t Take() noexcept {
        auto result = result_;
        result_ = 0;
        return result;
    }
    bool Event(UINT message, WPARAM value, LPARAM data) noexcept {
        if (message == WM_KILLFOCUS) {
            Cancel();
            return false;
        }
        if (message == WM_CHAR || message == WM_SYSCHAR)
            return waiting_ || text_;
        const auto key = Button(message, value, data);
        if (!key)
            return false;
        const bool down = Down(message),
                   repeat = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && (data & (1LL << 30));
        if (key < 256) {
            down_[key] = down;
            if (!down) {
                blocked_[key] = false;
                const bool consume = release_[key];
                release_[key] = false;
                return waiting_ || consume;
            }
            if (release_[key])
                return true;
        }
        text_ = false;
        if (!waiting_)
            return false;
        if (down && !repeat && (key >= 256 || !blocked_[key])) {
            result_ = key;
            waiting_ = false;
            if (key < 256)
                release_[key] = true;
            text_ = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        }
        return true;
    }
};
} // namespace awareness::binding
