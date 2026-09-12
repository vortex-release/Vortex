#pragma once
#include "settings.hpp"
#include "menu_probe.hpp"
#include "menu_motion.hpp"
#include "ui_icons.hpp"
#include "branding.hpp"
#include <imgui.h>
#include <cmath>
#include <cstring>
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
    s.WindowPadding = {16, 16};
    s.FramePadding = {9, 5};
    s.ItemSpacing = {10, 7};
    s.ItemInnerSpacing = {8, 6};
    s.CellPadding = {4, 5};
    s.WindowRounding = 12;
    s.ChildRounding = 9;
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
        ImGui::SameLine();
        const auto at = ImGui::GetCursorScreenPos();
        const float size = 15 * ImGui::GetStyle().FontScaleMain;
        ImGui::InvisibleButton("About", {size, size}, ImGuiButtonFlags_EnableNav);
        vortex::icons::Draw(ImGui::GetWindowDrawList(), vortex::icons::Id::CircleHelp, at, size, Packed(Muted));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) || ImGui::IsItemFocused())
            ImGui::SetTooltip("%s", description);
    }
    ImGui::Dummy({0, 3 * ImGui::GetStyle().FontScaleMain});
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
    const ImVec2 size{ImGui::GetContentRegionAvail().x, 39 * s};
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
        d->AddRectFilled({p.x, p.y + 11 * s}, {p.x + 2 * s, p.y + 27 * s}, Packed(Accent), s);
    Interaction();
    const auto color = Packed(selected ? Accent : Muted);
    constexpr vortex::icons::Id family[]{vortex::icons::Id::UserRound,       vortex::icons::Id::Crosshair,
                                         vortex::icons::Id::LayoutDashboard, vortex::icons::Id::SlidersHorizontal,
                                         vortex::icons::Id::Globe,           vortex::icons::Id::ScanLine,
                                         vortex::icons::Id::Route,           vortex::icons::Id::Download};
    vortex::icons::Draw(d, family[std::clamp(icon, 0, 7)], {p.x + 12 * s, p.y + 10 * s}, 19 * s, color);
    d->AddText({p.x + 40 * s, p.y + 11 * s}, Packed(selected ? Text : Muted), label);
    return clicked;
}
inline const char *RequestedTab{};
inline void Tip(const char *description) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", description);
}
inline bool IconButton(const char *label, vortex::icons::Id icon, float size = 28.f) {
    const float s = ImGui::GetStyle().FontScaleMain;
    const auto at = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, {size * s, size * s}, ImGuiButtonFlags_EnableNav);
    awareness::testing::Record(label);
    const float t = Animate(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.f : 0.f);
    auto *d = ImGui::GetWindowDrawList();
    if (t > .001f)
        d->AddRectFilled(at, {at.x + size * s, at.y + size * s}, Packed({1, 1, 1, .07f * t}), 6 * s);
    vortex::icons::Draw(d, icon, {at.x + (size - 18) * .5f * s, at.y + (size - 18) * .5f * s}, 18 * s, Packed(Text));
    Interaction();
    Tip(label);
    return pressed;
}
inline bool Tab(const char *label) {
    const bool requested = RequestedTab && std::strcmp(RequestedTab, label) == 0;
    const bool open =
        ImGui::BeginTabItem(label, nullptr, requested ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None);
    if (requested)
        RequestedTab = nullptr;
    awareness::testing::Record(label);
    if (std::strcmp(label, "Post-processing") == 0)
        awareness::testing::Record("Scene");
    return open;
}
} // namespace studio
inline void ConfigurePanelStyle(float scale = 1) {
    awareness::VisualOptions v;
    v.uiScale = scale;
    studio::Apply(v);
}
