#include "window_bridge.hpp"
#include <imgui.h>
#include <cstdio>
#include <cmath>
int main() {
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = L"AwarenessInputFixture";
    if (!RegisterClassW(&wc))
        return 1;
    const auto window =
        CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 300, 300, nullptr, nullptr, instance, nullptr);
    if (!window)
        return 1;
    unsigned ticks{};
    WindowBridge bridge;
    bridge.SetTick([](void *p) noexcept { ++*static_cast<unsigned *>(p); }, &ticks);
    if (FAILED(bridge.Attach(window)))
        return 1;
    auto *context = ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {300, 300};
    io.DeltaTime = 1.f / 60;
    io.IniFilename = nullptr;
    unsigned char *pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    const auto frame = [&] {
        bridge.FeedInput(io, 300, 300);
        ImGui::NewFrame();
    };
    int checks{}, failures{};
    const auto check = [&](bool ok, const char *why) {
        ++checks;
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", why);
        }
    };
    SendMessageW(window, WM_TIMER, reinterpret_cast<UINT_PTR>(&bridge), 0);
    check(ticks == 1, "session timer runs on the window thread while menu is open");
    SendMessageW(window, WM_KEYDOWN, VK_CONTROL, 0);
    SendMessageW(window, WM_KEYDOWN, 'A', 0);
    SendMessageW(window, WM_CHAR, '4', 0);
    SendMessageW(window, WM_CHAR, '2', 0);
    frame();
    check(io.KeyCtrl && ImGui::IsKeyDown(ImGuiKey_A), "letters and Ctrl support value editing");
    check(io.InputQueueCharacters.Size == 2 && io.InputQueueCharacters[0] == '4', "text input reaches ImGui");
    ImGui::EndFrame();
    bridge.SetVisible(false);
    frame();
    check(!io.KeyCtrl && !ImGui::IsKeyDown(ImGuiKey_A), "closing menu releases held keys");
    ImGui::EndFrame();
    SendMessageW(window, WM_KEYDOWN, VK_INSERT, 0);
    SendMessageW(window, WM_KEYDOWN, VK_INSERT, 1LL << 30);
    check(bridge.TakeMenuToggle() && !bridge.TakeMenuToggle(), "hotkey autorepeat only toggles once");
    SendMessageW(window, WM_KEYDOWN, VK_INSERT, 0);
    SendMessageW(window, WM_KEYDOWN, VK_INSERT, 0);
    check(!bridge.TakeMenuToggle(), "two queued toggles cancel correctly");
    bridge.SetVisible(true);
    SendMessageW(window, WM_KEYDOWN, 'B', 0);
    frame();
    ImGui::EndFrame();
    SendMessageW(window, WM_KILLFOCUS, 0, 0);
    frame();
    check(!ImGui::IsKeyDown(ImGuiKey_B), "focus loss releases keys");
    ImGui::EndFrame();
    SendMessageW(window, WM_SETFOCUS, 0, 0);
    for (int i = 0; i < 2000; ++i)
        SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(i % 299, i % 299));
    SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(271, 213));
    frame();
    RECT client{};
    GetClientRect(window, &client);
    check(io.MousePos.x == std::floor(271.f * 300 / client.right) &&
              io.MousePos.y == std::floor(213.f * 300 / client.bottom),
          "high-rate mouse burst reaches newest position in one frame");
    ImGui::EndFrame();
    check(!io.MouseDrawCursor, "native cursor avoids frame-bound software cursor latency");
    {
        awareness::binding::Capture capture;
        capture.Event(WM_LBUTTONDOWN, 0, 0);
        capture.Begin();
        check(capture.Event(WM_LBUTTONUP, 0, 0) && !capture.Take(), "the activating click release is not bound");
        check(capture.Event(WM_KEYDOWN, 'K', 0) && capture.Take() == 'K' && !capture.Waiting(),
              "next key binds exactly once");
        check(capture.Event(WM_CHAR, 'k', 0), "captured key cannot type into a text field");
        capture.Event(WM_KEYUP, 'K', 0);
        capture.Event(WM_KEYDOWN, VK_SHIFT, 0x36LL << 16);
        capture.Begin();
        capture.Event(WM_KEYDOWN, VK_SHIFT, (0x36LL << 16) | (1LL << 30));
        check(!capture.Take(), "held modifier and autorepeat are ignored");
        capture.Event(WM_KEYUP, VK_SHIFT, 0x36LL << 16);
        capture.Event(WM_KEYDOWN, VK_SHIFT, 0x36LL << 16);
        check(capture.Take() == VK_RSHIFT, "right Shift is resolved from its scan code");
        capture.Cancel();
        capture.Begin();
        capture.Event(WM_SYSKEYDOWN, VK_MENU, 1LL << 24);
        check(capture.Take() == VK_RMENU, "right Alt is distinct");
        capture.Cancel();
        capture.Begin();
        capture.Event(WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON2), 0);
        check(capture.Take() == VK_XBUTTON2, "mouse side button capture");
        capture.Cancel();
        capture.Begin();
        capture.Event(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
        check(capture.Take() == awareness::binding::WheelDown, "wheel direction capture");
        capture.Begin();
        capture.Event(WM_MOUSEMOVE, 0, 0);
        check(capture.Waiting(), "mouse motion is not a bind");
        capture.Event(WM_KILLFOCUS, 0, 0);
        check(!capture.Waiting() && !capture.Take(), "focus loss cancels without changing the binding");
        capture.Begin();
        capture.Event(WM_KEYDOWN, VK_ESCAPE, 0);
        check(capture.Take() == VK_ESCAPE, "Escape is bindable");
    }
    bridge.BeginBindCapture();
    SendMessageW(window, WM_KEYDOWN, VK_HOME, 0);
    check(bridge.TakeBinding() == VK_HOME && !bridge.TakeOverlayToggle(),
          "binding intercepts reserved keys before shortcuts");
    SendMessageW(window, WM_KEYUP, VK_HOME, 0);
    bridge.SetTrackingKey(VK_INSERT);
    SendMessageW(window, WM_KEYDOWN, VK_INSERT, 0);
    check(!bridge.TakeMenuToggle(), "Insert may be used for follow without opening the menu");
    SendMessageW(window, WM_KEYUP, VK_INSERT, 0);
    SendMessageW(window, WM_KEYDOWN, VK_CONTROL, 0);
    SendMessageW(window, WM_KEYDOWN, VK_INSERT, 0);
    check(bridge.TakeMenuToggle(), "Ctrl+Insert retains menu access when Insert is bound");
    SendMessageW(window, WM_KEYUP, VK_INSERT, 0);
    SendMessageW(window, WM_KEYUP, VK_CONTROL, 0);
    bridge.SetVisible(false);
    SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
    check(bridge.BindingHeld(awareness::binding::WheelUp), "wheel activates a short follow pulse");
    SendMessageW(window, WM_KILLFOCUS, 0, 0);
    check(!bridge.BindingHeld(awareness::binding::WheelUp), "focus loss clears wheel activation");
    SendMessageW(window, WM_KEYDOWN, VK_SPACE, 0);
    const auto previousExtra = SetMessageExtraInfo(static_cast<LPARAM>(awareness::assist::InputTag));
    SendMessageW(window, WM_KEYUP, VK_SPACE, 0);
    SetMessageExtraInfo(previousExtra);
    check(bridge.AssistKeys().Held(VK_SPACE), "bridge ignores tagged release when tracking physical jump hold");
    SendMessageW(window, WM_KEYUP, VK_SPACE, 0);
    check(!bridge.AssistKeys().Held(VK_SPACE), "bridge observes physical jump release");
    check(SUCCEEDED(bridge.Detach()), "input subclass detaches");
    ImGui::DestroyContext(context);
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, instance);
    std::printf("%d input checks; %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
