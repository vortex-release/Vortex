#pragma once
#include <Windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <imgui.h>
#include "../src/menu_motion.hpp"
#include "../src/ui_icons.hpp"
namespace vortex {
inline LRESULT FrameHitTest(HWND window, LPARAM point) noexcept {
    RECT r{};
    GetWindowRect(window, &r);
    const int x = GET_X_LPARAM(point) - r.left, y = GET_Y_LPARAM(point) - r.top;
    const int width = r.right - r.left, height = r.bottom - r.top;
    const int edge = MulDiv(7, GetDpiForWindow(window), 96);
    if (!IsZoomed(window)) {
        const bool left = x < edge, right = x >= width - edge;
        const bool top = y < edge, bottom = y >= height - edge;
        if (top && left)
            return HTTOPLEFT;
        if (top && right)
            return HTTOPRIGHT;
        if (bottom && left)
            return HTBOTTOMLEFT;
        if (bottom && right)
            return HTBOTTOMRIGHT;
        if (left)
            return HTLEFT;
        if (right)
            return HTRIGHT;
        if (top)
            return HTTOP;
        if (bottom)
            return HTBOTTOM;
    }
    const int title = MulDiv(40, GetDpiForWindow(window), 96), controls = MulDiv(132, GetDpiForWindow(window), 96);
    if (y < title && x < width - controls)
        return HTCAPTION;
    return HTCLIENT;
}
inline void FrameControls(HWND window, bool animated = true) {
    const float width = ImGui::GetWindowWidth(), s = GetDpiForWindow(window) / 96.f;
    auto *d = ImGui::GetForegroundDrawList();
    const char *ids[]{"##minimize", "##maximize", "##close"};
    for (int i = 0; i < 3; ++i) {
        ImGui::SetCursorPos({width - 132 * s + i * 40.f * s, 6 * s});
        const bool clicked = ImGui::InvisibleButton(ids[i], {36 * s, 30 * s}, ImGuiButtonFlags_EnableNav);
        const auto p = ImGui::GetItemRectMin();
        auto *state = ImGui::GetStateStorage();
        const auto id = ImGui::GetItemID();
        const float hover = awareness::MotionStep(state->GetFloat(id, 0), ImGui::IsItemHovered() ? 1.f : 0.f,
                                                  ImGui::GetIO().DeltaTime, animated);
        state->SetFloat(id, hover);
        if (hover > .001f)
            d->AddRectFilled(
                p, {p.x + 36 * s, p.y + 30 * s},
                ImGui::GetColorU32(i == 2 ? ImVec4(.69f, .21f, .26f, hover) : ImVec4(1, 1, 1, .055f * hover)), 5 * s);
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible)
            d->AddRect(p, {p.x + 36 * s, p.y + 30 * s}, ImGui::GetColorU32(ImGuiCol_NavCursor), 5 * s);
        const auto color = IM_COL32(231, 229, 240, 255);
        const auto icon = i == 0             ? icons::Id::Minus
                          : i == 2           ? icons::Id::X
                          : IsZoomed(window) ? icons::Id::Copy
                                             : icons::Id::Square;
        icons::Draw(d, icon, {p.x + 10 * s, p.y + 7 * s}, 16 * s, color);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", i == 0   ? "Minimize"
                                    : i == 1 ? (IsZoomed(window) ? "Restore" : "Maximize")
                                             : "Close");
        if (clicked)
            PostMessageW(window, WM_SYSCOMMAND,
                         i == 0   ? SC_MINIMIZE
                         : i == 1 ? (IsZoomed(window) ? SC_RESTORE : SC_MAXIMIZE)
                                  : SC_CLOSE,
                         0);
    }
}
} // namespace vortex
