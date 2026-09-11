#pragma once
#include "settings.hpp"
#include "menu_probe.hpp"
#include "menu_motion.hpp"
#include <imgui.h>
#include <cmath>
namespace studio {
inline ImVec4 Canvas{.065f, .065f, .078f, 1}, Panel{.085f, .085f, .10f, 1};
inline ImVec4 Border{1, 1, 1, .075f}, Text{.95f, .95f, .98f, 1}, Muted{.57f, .57f, .64f, 1};
inline ImVec4 Accent{.86f, .16f, .22f, 1};
inline bool Animations{true};
inline ImVec4 Alpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}
inline ImU32 Packed(ImVec4 c) {
    return ImGui::GetColorU32(c);
}
inline void Apply(const awareness::VisualOptions &v) {
    constexpr ImVec4 accents[]{
        {.86f, .16f, .22f, 1}, {.3f, .55f, .95f, 1}, {.65f, .40f, .92f, 1}, {.86f, .86f, .89f, 1}};
    Accent = v.theme < 4 ? accents[v.theme] : ImVec4{v.accent.r, v.accent.g, v.accent.b, 1};
    Animations = v.menuAnimations != 0;
    Canvas.w = v.menuOpacity;
    Panel.w = v.panelOpacity;
    auto &s = ImGui::GetStyle();
    s = ImGuiStyle{};
    ImGui::StyleColorsDark();
    s.WindowPadding = {20, 20};
    s.FramePadding = {9, 5};
    s.ItemSpacing = {12, 9};
    s.ItemInnerSpacing = {8, 6};
    s.CellPadding = {4, 5};
    s.WindowRounding = 14;
    s.ChildRounding = 12;
    s.FrameRounding = 7;
    s.PopupRounding = 6;
    s.GrabRounding = 6;
    s.TabRounding = 5;
    s.WindowBorderSize = 1;
    s.PopupBorderSize = 1;
    s.ChildBorderSize = 1;
    s.FrameBorderSize = 0;
    s.ScrollbarSize = 7;
    s.ScrollbarRounding = 5;
    s.AntiAliasedFill = s.AntiAliasedLines = s.AntiAliasedLinesUseTex = true;
    auto *c = s.Colors;
    c[ImGuiCol_WindowBg] = Canvas;
    c[ImGuiCol_ChildBg] = {0, 0, 0, 0};
    c[ImGuiCol_PopupBg] = {.045f, .045f, .055f, .99f};
    c[ImGuiCol_Text] = Text;
    c[ImGuiCol_TextDisabled] = Muted;
    c[ImGuiCol_Border] = Border;
    c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = Canvas;
    c[ImGuiCol_FrameBg] = {1, 1, 1, .045f};
    c[ImGuiCol_FrameBgHovered] = {1, 1, 1, .075f};
    c[ImGuiCol_FrameBgActive] = {1, 1, 1, .10f};
    c[ImGuiCol_CheckMark] = c[ImGuiCol_SliderGrab] = c[ImGuiCol_SliderGrabActive] = Accent;
    c[ImGuiCol_CheckboxSelectedBg] = Alpha(Accent, .15f);
    c[ImGuiCol_Button] = {1, 1, 1, .035f};
    c[ImGuiCol_ButtonHovered] = {1, 1, 1, .075f};
    c[ImGuiCol_ButtonActive] = {1, 1, 1, .11f};
    c[ImGuiCol_Header] = Alpha(Accent, .12f);
    c[ImGuiCol_HeaderHovered] = {1, 1, 1, .06f};
    c[ImGuiCol_HeaderActive] = Alpha(Accent, .2f);
    c[ImGuiCol_Tab] = {1, 1, 1, .03f};
    c[ImGuiCol_TabHovered] = {1, 1, 1, .08f};
    c[ImGuiCol_TabSelected] = Alpha(Accent, .12f);
    c[ImGuiCol_TabSelectedOverline] = Accent;
    c[ImGuiCol_Separator] = Border;
    c[ImGuiCol_ResizeGrip] = {1, 1, 1, .08f};
    c[ImGuiCol_ResizeGripHovered] = Alpha(Accent, .4f);
    c[ImGuiCol_ResizeGripActive] = Accent;
    c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab] = {1, 1, 1, .1f};
    c[ImGuiCol_ScrollbarGrabHovered] = {1, 1, 1, .2f};
    c[ImGuiCol_TableRowBg] = {0, 0, 0, 0};
    c[ImGuiCol_TableRowBgAlt] = {1, 1, 1, .018f};
    c[ImGuiCol_TextSelectedBg] = Alpha(Accent, .25f);
    c[ImGuiCol_NavCursor] = Alpha(Accent, .85f);
    c[ImGuiCol_ScrollbarGrabActive] = Alpha(Accent, .55f);
    s.DisabledAlpha = .42f;
    s.ScaleAllSizes(v.uiScale);
    s.FontScaleMain = v.uiScale;
}
inline float Animate(ImGuiID id, float goal, float speed = 18.f, float initial = 0.f) {
    auto *storage = ImGui::GetStateStorage();
    const float value =
        awareness::MotionStep(storage->GetFloat(id, initial), goal, ImGui::GetIO().DeltaTime, Animations, speed);
    storage->SetFloat(id, value);
    return value;
}
inline void Interaction() {
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible)
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                            Packed(Alpha(Accent, .85f)), ImGui::GetStyle().FrameRounding, 1.2f);
}
inline void Shade() {
    const auto p = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    const float s = ImGui::GetStyle().FontScaleMain;
    auto *d = ImGui::GetWindowDrawList();
    const float inset = ImGui::GetStyle().ChildRounding * .4f;
    const ImVec2 a{p.x + inset, p.y + 1}, b{p.x + size.x - inset, p.y + (std::min)(size.y - 2, 82 * s)};
    if (b.y > a.y)
        d->AddRectFilledMultiColor(a, b, Packed({1, 1, 1, .015f}), Packed({1, 1, 1, .015f}), Packed({1, 1, 1, 0}),
                                   Packed({1, 1, 1, 0}));
    d->AddLine({p.x + 16 * s, p.y + 1}, {p.x + size.x - 16 * s, p.y + 1}, Packed({1, 1, 1, .065f}));
}
inline void Caption(const char *text) {
    ImGui::TextWrapped("%s", text);
}
inline void Card(const char *title, const char *description = nullptr) {
    ImGui::PushID(title);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Panel);
    ImGui::BeginChild("Card", {0, 0},
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();
    Shade();
    ImGui::PushFont(nullptr, 16);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (description && *description) {
        ImGui::PushStyleColor(ImGuiCol_Text, Muted);
        ImGui::TextWrapped("%s", description);
        ImGui::PopStyleColor();
    }
    ImGui::Dummy({0, 6 * ImGui::GetStyle().FontScaleMain});
}
inline void EndCard() {
    ImGui::EndChild();
    ImGui::PopID();
}
inline bool Button(const char *label, ImVec2 size = {}) {
    const bool clicked = ImGui::Button(label, size);
    awareness::testing::Record(label);
    const auto id = ImGui::GetItemID();
    const float goal = ImGui::IsItemActive() ? 1.f : ImGui::IsItemHovered() ? .6f : 0;
    const float value = Animate(id, goal);
    Interaction();
    if (value > .01f)
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                            Packed(Alpha(Accent, .3f * value)), ImGui::GetStyle().FrameRounding);
    return clicked;
}
inline bool Toggle(const char *label, std::uint32_t &value) {
    const float s = ImGui::GetStyle().FontScaleMain;
    const auto p = ImGui::GetCursorScreenPos();
    const float w = (std::max)(1.f, ImGui::GetContentRegionAvail().x);
    const bool clicked = ImGui::InvisibleButton(label, {w, 23 * s}, ImGuiButtonFlags_EnableNav);
    awareness::testing::Record(label);
    if (clicked)
        value = value ? 0u : 1u;
    const auto id = ImGui::GetItemID();
    const float t = Animate(id, value ? 1.f : 0.f, 22.f, value ? 1.f : 0.f);
    Interaction();
    auto *d = ImGui::GetWindowDrawList();
    d->PushClipRect(p, {p.x + (std::max)(0.f, w - 38 * s), p.y + 23 * s}, true);
    d->AddText({p.x, p.y + 3 * s}, Packed(ImGui::IsItemHovered() ? Text : Alpha(Text, .86f)), label);
    d->PopClipRect();
    const float x = p.x + w - 29 * s, y = p.y + 6 * s;
    d->AddRectFilled({x, y}, {x + 29 * s, y + 12 * s}, Packed(value ? Alpha(Accent, .25f) : ImVec4{1, 1, 1, .08f}),
                     6 * s);
    d->AddCircleFilled({x + (6 + 17 * t) * s, y + 6 * s}, 7 * s, Packed(value ? Accent : ImVec4{.29f, .29f, .32f, 1}),
                       24);
    return clicked;
}
inline bool Nav(const char *label, int icon, bool selected) {
    const float s = ImGui::GetStyle().FontScaleMain;
    const auto p = ImGui::GetCursorScreenPos();
    const ImVec2 size{ImGui::GetContentRegionAvail().x, 43 * s};
    const bool clicked = ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
    awareness::testing::Record(label);
    auto *d = ImGui::GetWindowDrawList();
    const float t = Animate(ImGui::GetItemID(),
                            selected                 ? 1.f
                            : ImGui::IsItemHovered() ? .3f
                                                     : 0.f,
                            20.f, selected ? 1.f : 0.f);
    if (t > .001f)
        d->AddRectFilled(p, {p.x + size.x, p.y + size.y}, Packed(Alpha(Accent, .13f * t)), 7 * s);
    if (selected)
        d->AddRectFilled({p.x, p.y + 11 * s}, {p.x + 2 * s, p.y + 28 * s}, Packed(Accent), s);
    Interaction();
    const auto color = Packed(selected ? Accent : Muted);
    const ImVec2 q{p.x + 21 * s, p.y + 19 * s};
    if (icon == 0) {
        d->AddCircle({q.x, q.y - 4 * s}, 3 * s, color, 16, 1.4f * s);
        d->AddRect({q.x - 5 * s, q.y + 1 * s}, {q.x + 5 * s, q.y + 7 * s}, color, 2 * s);
    } else if (icon == 1) {
        d->AddCircle(q, 6 * s, color, 24, 1.4f * s);
        d->AddLine({q.x - 10 * s, q.y}, {q.x - 3 * s, q.y}, color);
        d->AddLine({q.x, q.y - 10 * s}, {q.x, q.y - 3 * s}, color);
    } else if (icon == 2) {
        d->AddCircle(q, 6 * s, color, 24, 1.4f * s);
        d->AddCircleFilled({q.x + 2 * s, q.y - 2 * s}, 2 * s, color);
    } else if (icon == 4) {
        d->AddCircle(q, 7 * s, color, 24, 1.3f * s);
        d->AddEllipse(q, {3 * s, 7 * s}, color, 0, 24, 1.1f * s);
        d->AddLine({q.x - 7 * s, q.y}, {q.x + 7 * s, q.y}, color);
    } else if (icon == 5) {
        for (int x : {-1, 1})
            for (int y : {-1, 1})
                d->AddLine({q.x + x * 3 * s, q.y + y * 3 * s}, {q.x + x * 7 * s, q.y + y * 7 * s}, color, 1.4f * s);
    } else if (icon == 6) {
        for (int i = 0; i < 2; ++i) {
            float x = q.x + (i * 7 - 5) * s;
            d->AddLine({x - 3 * s, q.y - 5 * s}, {x + 2 * s, q.y}, color, 1.4f * s);
            d->AddLine({x + 2 * s, q.y}, {x - 3 * s, q.y + 5 * s}, color, 1.4f * s);
        }
    } else if (icon == 7) {
        d->AddLine({q.x, q.y - 7 * s}, {q.x, q.y + 7 * s}, color, 1.4f * s);
        d->AddLine({q.x - 4 * s, q.y + 2 * s}, {q.x, q.y + 7 * s}, color, 1.4f * s);
        d->AddLine({q.x + 4 * s, q.y + 2 * s}, {q.x, q.y + 7 * s}, color, 1.4f * s);
        d->AddLine({q.x - 7 * s, q.y - 6 * s}, {q.x + 7 * s, q.y - 6 * s}, color, 1.4f * s);
    } else {
        d->AddLine({q.x - 7 * s, q.y - 4 * s}, {q.x + 7 * s, q.y - 4 * s}, color, 1.4f * s);
        d->AddLine({q.x - 7 * s, q.y + 4 * s}, {q.x + 7 * s, q.y + 4 * s}, color, 1.4f * s);
        d->AddCircleFilled({q.x - 3 * s, q.y - 4 * s}, 2.5f * s, color);
        d->AddCircleFilled({q.x + 3 * s, q.y + 4 * s}, 2.5f * s, color);
    }
    d->AddText({p.x + 40 * s, p.y + 13 * s}, Packed(selected ? Text : Muted), label);
    return clicked;
}
inline bool Tab(const char *label) {
    const bool open = ImGui::BeginTabItem(label);
    awareness::testing::Record(label);
    return open;
}
} // namespace studio
inline void ConfigurePanelStyle(float scale = 1) {
    awareness::VisualOptions v;
    v.uiScale = scale;
    studio::Apply(v);
}
