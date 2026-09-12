#include "window_bridge.hpp"
#include <Windowsx.h>
#include <imgui.h>
#include <cfloat>
#include <algorithm>
namespace {
std::atomic<WindowBridge *> activeBridge{};
ImGuiKey Key(WPARAM key) noexcept {
    if (key >= 'A' && key <= 'Z')
        return static_cast<ImGuiKey>(ImGuiKey_A + key - 'A');
    if (key >= '0' && key <= '9')
        return static_cast<ImGuiKey>(ImGuiKey_0 + key - '0');
    if (key >= VK_F1 && key <= VK_F12)
        return static_cast<ImGuiKey>(ImGuiKey_F1 + key - VK_F1);
    switch (key) {
    case VK_TAB:
        return ImGuiKey_Tab;
    case VK_SPACE:
        return ImGuiKey_Space;
    case VK_RETURN:
        return ImGuiKey_Enter;
    case VK_ESCAPE:
        return ImGuiKey_Escape;
    case VK_LEFT:
        return ImGuiKey_LeftArrow;
    case VK_RIGHT:
        return ImGuiKey_RightArrow;
    case VK_UP:
        return ImGuiKey_UpArrow;
    case VK_DOWN:
        return ImGuiKey_DownArrow;
    case VK_BACK:
        return ImGuiKey_Backspace;
    case VK_DELETE:
        return ImGuiKey_Delete;
    case VK_HOME:
        return ImGuiKey_Home;
    case VK_END:
        return ImGuiKey_End;
    case VK_PRIOR:
        return ImGuiKey_PageUp;
    case VK_NEXT:
        return ImGuiKey_PageDown;
    case VK_CONTROL:
    case VK_LCONTROL:
        return ImGuiKey_LeftCtrl;
    case VK_RCONTROL:
        return ImGuiKey_RightCtrl;
    case VK_SHIFT:
    case VK_LSHIFT:
        return ImGuiKey_LeftShift;
    case VK_RSHIFT:
        return ImGuiKey_RightShift;
    case VK_MENU:
    case VK_LMENU:
        return ImGuiKey_LeftAlt;
    case VK_RMENU:
        return ImGuiKey_RightAlt;
    case VK_LWIN:
        return ImGuiKey_LeftSuper;
    case VK_RWIN:
        return ImGuiKey_RightSuper;
    default:
        return ImGuiKey_None;
    }
}
} // namespace
void WindowBridge::ApplyCursor(bool open) noexcept {
    // Invoked on the window thread. SDL's relative-mouse APIs require that thread.
    open = open && GetForegroundWindow() == window_;
    if (open == cursorReleased_)
        return;
    if (open) {
        GetClipCursor(&oldClip_);
        oldRelative_ = false;
        sdl3Set_ = nullptr;
        sdl2Set_ = nullptr;
        sdlWindow_ = nullptr;
        if (const auto module = GetModuleHandleW(L"SDL3.dll")) {
            const auto focus = reinterpret_cast<void *(__cdecl *)()>(GetProcAddress(module, "SDL_GetKeyboardFocus"));
            const auto get =
                reinterpret_cast<bool(__cdecl *)(void *)>(GetProcAddress(module, "SDL_GetWindowRelativeMouseMode"));
            sdl3Set_ = reinterpret_cast<bool(__cdecl *)(void *, bool)>(
                GetProcAddress(module, "SDL_SetWindowRelativeMouseMode"));
            if (focus && get && sdl3Set_ && (sdlWindow_ = focus())) {
                oldRelative_ = get(sdlWindow_);
                sdl3Set_(sdlWindow_, false);
            }
        } else if (const auto module2 = GetModuleHandleW(L"SDL2.dll")) {
            const auto get = reinterpret_cast<int(__cdecl *)()>(GetProcAddress(module2, "SDL_GetRelativeMouseMode"));
            sdl2Set_ = reinterpret_cast<int(__cdecl *)(int)>(GetProcAddress(module2, "SDL_SetRelativeMouseMode"));
            if (get && sdl2Set_) {
                oldRelative_ = get() != 0;
                sdl2Set_(0);
            }
        }
        ReleaseCapture();
        ClipCursor(nullptr);
        cursorShowCalls_ = 0;
        do {
            ++cursorShowCalls_;
        } while (ShowCursor(TRUE) < 0 && cursorShowCalls_ < 16);
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        cursorReleased_ = true;
    } else {
        if (sdl3Set_ && sdlWindow_)
            sdl3Set_(sdlWindow_, oldRelative_);
        else if (sdl2Set_)
            sdl2Set_(oldRelative_ ? 1 : 0);
        if (GetForegroundWindow() == window_)
            ClipCursor(&oldClip_);
        while (cursorShowCalls_ > 0) {
            ShowCursor(FALSE);
            --cursorShowCalls_;
        }
        cursorReleased_ = false;
    }
}
HRESULT WindowBridge::Attach(HWND window) noexcept {
    if (attached_)
        return S_OK;
    window_ = window;
    controlMessage_ = RegisterWindowMessageW(L"EntityAwarenessOverlay.Control.2");
    if (!controlMessage_)
        return HRESULT_FROM_WIN32(GetLastError());
    original_ = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
    if (!original_)
        return E_FAIL;
    WindowBridge *empty{};
    if (!activeBridge.compare_exchange_strong(empty, this))
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    SetLastError(0);
    if (!SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Procedure)) && GetLastError()) {
        activeBridge = nullptr;
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (GetForegroundWindow() == window_) {
        std::scoped_lock lock(inputMutex_);
        for (unsigned key = 1; key < 256; ++key)
            physical_.Seed(key, (GetAsyncKeyState(key) & 0x8000) != 0);
    }
    attached_ = true;
    if (tick_) {
        tickTimer_ = SetTimer(window_, reinterpret_cast<UINT_PTR>(this), 500, nullptr);
        if (!tickTimer_) {
            SetWindowLongPtrW(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_));
            attached_ = false;
            activeBridge = nullptr;
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }
    SetVisible(visible_);
    return S_OK;
}
void WindowBridge::SetVisible(bool open) noexcept {
    if (!open) {
        std::scoped_lock lock(inputMutex_);
        capture_.Cancel();
    }
    if (visible_.exchange(open) != open)
        resetInput_ = true;
    if (attached_)
        PostMessageW(window_, controlMessage_, 0, reinterpret_cast<LPARAM>(this));
}
HRESULT WindowBridge::DetachOnWindowThread() noexcept {
    if (!attached_)
        return S_OK;
    if (reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window_, GWLP_WNDPROC)) != &Procedure)
        return HRESULT_FROM_WIN32(ERROR_BUSY); // Do not break another subclass's return chain.
    ApplyCursor(false);
    if (tickTimer_) {
        KillTimer(window_, tickTimer_);
        tickTimer_ = 0;
    }
    SetLastError(0);
    if (!SetWindowLongPtrW(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_)) && GetLastError())
        return HRESULT_FROM_WIN32(GetLastError());
    attached_ = false;
    activeBridge = nullptr;
    return S_OK;
}
HRESULT WindowBridge::Detach() noexcept {
    if (!attached_)
        return S_OK;
    if (!IsWindow(window_)) {
        attached_ = false;
        activeBridge = nullptr;
        return S_OK;
    }
    if (GetWindowThreadProcessId(window_, nullptr) == GetCurrentThreadId())
        return DetachOnWindowThread();
    DWORD_PTR result{};
    if (!SendMessageTimeoutW(window_, controlMessage_, 2, reinterpret_cast<LPARAM>(this), SMTO_ABORTIFHUNG, 2000,
                             &result))
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    return static_cast<HRESULT>(result);
}
LRESULT CALLBACK WindowBridge::Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto *bridge = activeBridge.load();
    if (!bridge || bridge->window_ != window)
        return DefWindowProcW(window, message, wparam, lparam);
    const auto original = bridge->original_;
    if (message == WM_TIMER && bridge->tickTimer_ && wparam == bridge->tickTimer_) {
        if (bridge->tick_)
            bridge->tick_(bridge->tickContext_);
        return 0;
    }
    const auto extra = static_cast<ULONG_PTR>(GetMessageExtraInfo());
    if (extra == awareness::assist::InputTag && awareness::binding::Button(message, wparam, lparam)) {
        if (bridge->Visible() && awareness::binding::Down(message))
            return 0;
        return CallWindowProcW(original, window, message, wparam, lparam);
    }
    bool controlHeld{};
    {
        std::scoped_lock lock(bridge->inputMutex_);
        bridge->physical_.Event(message, wparam, lparam, extra, !bridge->Visible());
        const bool consumed = bridge->capture_.Event(message, wparam, lparam);
        controlHeld = bridge->capture_.Held(VK_LCONTROL) || bridge->capture_.Held(VK_RCONTROL);
        if (message == WM_KILLFOCUS)
            bridge->wheelUntil_ = 0;
        if (consumed)
            return 0;
    }
    const bool repeat = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && (lparam & (1LL << 30));
    const unsigned mask = bridge->assistRepeats_;
    if (repeat &&
        ((wparam == 'W' && (mask & 1)) || (wparam == 'S' && (mask & 2)) || (wparam == VK_SPACE && (mask & 4))))
        return 0;
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
        const auto key = awareness::binding::Button(message, wparam, lparam);
        if (key) {
            bridge->wheelKey_ = key;
            bridge->wheelUntil_ = GetTickCount64() + 120;
        }
    }
    if (message == bridge->controlMessage_ && lparam == reinterpret_cast<LPARAM>(bridge)) {
        if (wparam == 2)
            return bridge->DetachOnWindowThread();
        bridge->ApplyCursor(bridge->Visible());
        return 0;
    }
    if (message == WM_ACTIVATEAPP)
        bridge->ApplyCursor(wparam && bridge->Visible());
    if (message == WM_KILLFOCUS || message == WM_SETFOCUS) {
        try {
            std::scoped_lock lock(bridge->inputMutex_);
            bridge->events_.push_back({message, wparam, lparam});
        } catch (...) {
        }
    }
    const bool initialDown = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !(lparam & (1LL << 30));
    const bool keyMessage =
        message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_KEYUP || message == WM_SYSKEYUP;
    if (keyMessage && wparam == VK_INSERT && (bridge->trackingKey_ != VK_INSERT || controlHeld)) {
        if (initialDown)
            ++bridge->menuToggle_;
        return 0;
    }
    if (keyMessage &&
        ((bridge->overlayKey_ && wparam == bridge->overlayKey_) ||
         (bridge->alternateKey_ && wparam == bridge->alternateKey_)) &&
        (wparam != bridge->trackingKey_ || controlHeld)) {
        if (initialDown)
            ++bridge->overlayToggle_;
        return 0;
    }
    if (bridge->Visible()) {
        const bool mouse = message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
        const bool keyboard = message >= WM_KEYFIRST && message <= WM_KEYLAST;
        if (mouse || keyboard) {
            try {
                std::scoped_lock lock(bridge->inputMutex_);
                if (bridge->events_.size() >= 512) {
                    bridge->events_.clear();
                    bridge->resetInput_ = true;
                }
                if (message == WM_MOUSEMOVE && !bridge->events_.empty() &&
                    bridge->events_.back().message == WM_MOUSEMOVE)
                    bridge->events_.back() = {message, wparam, lparam};
                else
                    bridge->events_.push_back({message, wparam, lparam});
            } catch (...) {
            }
            return 0;
        }
        if (message == WM_INPUT)
            return DefWindowProcW(window, message, wparam, lparam);
        if (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
    }
    if (message == WM_NCDESTROY) {
        if (bridge->tickTimer_) {
            KillTimer(window, bridge->tickTimer_);
            bridge->tickTimer_ = 0;
        }
        bridge->ApplyCursor(false);
        bridge->attached_ = false;
        activeBridge = nullptr;
    }
    return CallWindowProcW(original, window, message, wparam, lparam);
}
void WindowBridge::FeedInput(ImGuiIO &io, float width, float height) {
    // A single renderer drains the window-thread producer. Swap only under the
    // producer lock and keep both buffers allocated between frames.
    frameEvents_.clear();
    bool reset{};
    {
        std::scoped_lock lock(inputMutex_);
        frameEvents_.swap(events_);
        reset = resetInput_.exchange(false);
    }
    if (reset) {
        io.ClearInputKeys();
        io.ClearInputMouse();
        keys_ = {};
    }
    io.MouseDrawCursor = false; // Native cursor tracks the mouse independently of game FPS.
    if (!Visible()) {
        io.ClearInputKeys();
        keys_ = {};
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        for (int i = 0; i < 5; ++i)
            io.AddMouseButtonEvent(i, false);
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    const float sx = client.right > 0 ? width / static_cast<float>(client.right) : 1.f;
    const float sy = client.bottom > 0 ? height / static_cast<float>(client.bottom) : 1.f;
    POINT cursor{};
    for (const auto &event : frameEvents_) {
        switch (event.message) {
        case WM_MOUSEMOVE:
            io.AddMousePosEvent(GET_X_LPARAM(event.lparam) * sx, GET_Y_LPARAM(event.lparam) * sy);
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            io.AddMouseButtonEvent(0, event.message == WM_LBUTTONDOWN);
            break;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            io.AddMouseButtonEvent(1, event.message == WM_RBUTTONDOWN);
            break;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            io.AddMouseButtonEvent(2, event.message == WM_MBUTTONDOWN);
            break;
        case WM_KILLFOCUS:
            io.AddFocusEvent(false);
            keys_ = {};
            break;
        case WM_SETFOCUS:
            io.AddFocusEvent(true);
            break;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
            io.AddMouseButtonEvent(GET_XBUTTON_WPARAM(event.wparam) == XBUTTON1 ? 3 : 4,
                                   event.message == WM_XBUTTONDOWN);
            break;
        case WM_MOUSEWHEEL:
            io.AddMouseWheelEvent(0.f, static_cast<float>(GET_WHEEL_DELTA_WPARAM(event.wparam)) / WHEEL_DELTA);
            break;
        case WM_MOUSEHWHEEL:
            io.AddMouseWheelEvent(-static_cast<float>(GET_WHEEL_DELTA_WPARAM(event.wparam)) / WHEEL_DELTA, 0.f);
            break;
        case WM_CHAR:
            if (event.wparam > 0 && event.wparam <= 0xffff)
                io.AddInputCharacterUTF16(static_cast<ImWchar16>(event.wparam));
            break;
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP: {
            const bool down = event.message == WM_KEYDOWN || event.message == WM_SYSKEYDOWN;
            if (event.wparam < keys_.size())
                keys_[event.wparam] = down;
            io.AddKeyEvent(ImGuiMod_Ctrl, keys_[VK_CONTROL] || keys_[VK_LCONTROL] || keys_[VK_RCONTROL]);
            io.AddKeyEvent(ImGuiMod_Shift, keys_[VK_SHIFT] || keys_[VK_LSHIFT] || keys_[VK_RSHIFT]);
            io.AddKeyEvent(ImGuiMod_Alt, keys_[VK_MENU] || keys_[VK_LMENU] || keys_[VK_RMENU]);
            io.AddKeyEvent(ImGuiMod_Super, keys_[VK_LWIN] || keys_[VK_RWIN]);
            const auto key = Key(event.wparam);
            if (key != ImGuiKey_None)
                io.AddKeyEvent(key, down);
            break;
        }
        default:
            break;
        }
    }
    // Sample the newest cursor after queued edges; high-polling-rate motion must
    // not leave the UI following an old queue of mouse positions.
    if (GetForegroundWindow() == window_ && GetCursorPos(&cursor) && ScreenToClient(window_, &cursor))
        io.AddMousePosEvent(cursor.x * sx, cursor.y * sy);
}
void WindowBridge::BeginBindCapture() noexcept {
    std::scoped_lock lock(inputMutex_);
    capture_.Begin();
    for (UINT key = 1; key < 256; ++key)
        if (GetAsyncKeyState(key) & 0x8000)
            capture_.BlockHeld(key);
    events_.clear();
    resetInput_ = true;
}
bool WindowBridge::WaitingForBind() noexcept {
    std::scoped_lock lock(inputMutex_);
    return capture_.Waiting();
}
UINT WindowBridge::TakeBinding() noexcept {
    std::scoped_lock lock(inputMutex_);
    return capture_.Take();
}
bool WindowBridge::BindingHeld(UINT key) const noexcept {
    if (key >= awareness::binding::WheelUp)
        return wheelKey_ == key && GetTickCount64() < wheelUntil_;
    return awareness::binding::Valid(key) && (GetAsyncKeyState(key) & 0x8000) != 0;
}
