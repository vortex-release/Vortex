#pragma once
#include "CameraTracking.hpp"
#include <imgui.h>
#if defined(_WIN32)
#include <Windows.h>
#endif

namespace awareness::camera {
// Call between BeginTabBar/EndTabBar during an active ImGui frame.
inline bool DrawTrackingTab(Settings &settings) {
    bool changed = false;
    if (ImGui::BeginTabItem("Camera tracking")) {
        changed |= ImGui::Checkbox("Enable tracking", &settings.enabled);
        ImGui::TextUnformatted("FOV limit");
        ImGui::SetNextItemWidth(-1.f);
        changed |=
            ImGui::SliderFloat("##camera_fov", &settings.fovDegrees, 1.f, 90.f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextUnformatted("Follow speed");
        ImGui::SetNextItemWidth(-1.f);
        changed |= ImGui::SliderFloat("##camera_speed", &settings.interpolationSpeed, 0.f, InstantFollowSpeed,
                                      settings.interpolationSpeed >= InstantFollowSpeed ? "Instant" : "%.0f",
                                      ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextUnformatted("Teams");
        ImGui::SetNextItemWidth(-1.f);
        int teams = static_cast<int>(settings.teams);
        if (ImGui::Combo("##camera_teams", &teams, "Opponents\0All teams\0")) {
            settings.teams = static_cast<TargetTeams>(teams);
            changed = true;
        }
        ImGui::EndTabItem();
    }
    return changed;
}
#if defined(_WIN32)
// Check after ImGui::NewFrame; block scene input while the UI owns the mouse/keyboard.
// Pass another virtual-key code to designate a different hold hotkey.
inline bool ActivationHeld(HWND owner, int virtualKey = VK_RBUTTON) noexcept {
    if (!owner || !IsWindow(owner) || GetForegroundWindow() != owner || virtualKey <= 0 || virtualKey >= 256 ||
        !ImGui::GetCurrentContext())
        return false;
    const auto &io = ImGui::GetIO();
    const bool mouseKey = virtualKey == VK_LBUTTON || virtualKey == VK_RBUTTON || virtualKey == VK_MBUTTON ||
                          virtualKey == VK_XBUTTON1 || virtualKey == VK_XBUTTON2;
    if (mouseKey ? io.WantCaptureMouse : (io.WantCaptureKeyboard || io.WantTextInput))
        return false;
    if (mouseKey) {
        POINT cursor{};
        RECT client{};
        if (!GetCursorPos(&cursor) || !ScreenToClient(owner, &cursor) || !GetClientRect(owner, &client) ||
            !PtInRect(&client, cursor))
            return false;
    }
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}
#endif
} // namespace awareness::camera
