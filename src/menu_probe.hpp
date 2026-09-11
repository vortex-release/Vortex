#pragma once
#include <Windows.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <array>
#include <cstring>
namespace awareness::testing {
// Opt-in geometry capture for the demo's real mouse/keyboard smoke tests.
// Off in normal app sessions; records UI labels and rectangles only.
struct MenuItem {
    char label[80]{};
    float rect[4]{};
    bool visible{};
};
inline std::array<MenuItem, 512> items;
inline unsigned count{};
inline bool enabled{};
inline void BeginFrame() {
    char value[8]{};
    enabled = GetEnvironmentVariableA("VORTEX_UI_TEST", value, sizeof(value)) && value[0] == '1';
    count = 0;
}
inline void Record(const char *label) {
    if (!enabled || count == items.size())
        return;
    auto &item = items[count++];
    strncpy_s(item.label, label, _TRUNCATE);
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    item.rect[0] = a.x;
    item.rect[1] = a.y;
    item.rect[2] = b.x;
    item.rect[3] = b.y;
    item.visible = ImGui::GetCurrentWindow()->ClipRect.Contains(ImVec2((a.x + b.x) * .5f, (a.y + b.y) * .5f));
}
inline int Find(const char *label, unsigned occurrence, float *rect) {
    if (!enabled || !label || !rect)
        return false;
    for (unsigned i = 0; i < count; ++i)
        if (!std::strcmp(items[i].label, label)) {
            if (occurrence) {
                --occurrence;
                continue;
            }
            std::memcpy(rect, items[i].rect, sizeof(items[i].rect));
            return items[i].visible ? 1 : 2;
        }
    return false;
}
} // namespace awareness::testing
