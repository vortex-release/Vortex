#pragma once
#include "profile_store.hpp"
#include "ui_theme.hpp"
#include <imgui.h>
#include <cstring>
#include <array>

namespace awareness::profiles {
inline void DrawProfilePanel(const View &view, Request &request) {
    static char newName[256]{}, renameName[256]{}, filter[128]{};
    static std::string selected, lastSelectionRequest, pendingReveal;
    const float scale = ImGui::GetStyle().FontScaleMain;
    const auto exists = [&](const std::string &name) {
        return std::any_of(view.entries.begin(), view.entries.end(),
                           [&](const Entry &entry) { return entry.name == name; });
    };
    if (view.selectionRequest.empty())
        lastSelectionRequest.clear();
    else if (view.selectionRequest != lastSelectionRequest && exists(view.selectionRequest)) {
        selected = view.selectionRequest;
        lastSelectionRequest = view.selectionRequest;
        pendingReveal = selected;
        filter[0] = 0;
        strncpy_s(renameName, selected.c_str(), _TRUNCATE);
        ImGui::SetScrollY(0); // Bring the library back into view after creating a profile below it.
    }
    if (!exists(selected)) {
        selected = view.entries.empty() ? std::string{} : view.entries.front().name;
        strncpy_s(renameName, selected.c_str(), _TRUNCATE);
    }
    const auto action = [&](Operation operation, const std::string &name = {}) {
        request.operation = operation;
        request.name = name;
    };
    ImGui::TextWrapped("Keep separate setups for different sessions. Named profiles are snapshots; Auto-save updates "
                       "your working settings without replacing them.");
    if (view.busy)
        ImGui::TextColored(studio::Accent, "Working...");
    else if (!view.message.empty())
        ImGui::TextWrapped("%s", view.message.c_str());
    ImGui::Dummy({0, 6 * scale});
    studio::Card("My library", "CONFIGURATION PROFILES");
    if (!view.loadedName.empty())
        ImGui::Text("Last loaded: %s", view.loadedName.c_str());
    ImGui::TextDisabled("%zu / %zu profiles", view.entries.size(), MaximumProfiles);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##ProfileSearch", "Find a profile", filter, sizeof(filter));
    const auto matches = [&](const std::string &name) {
        if (!filter[0])
            return true;
        return std::search(name.begin(), name.end(), filter, filter + std::strlen(filter),
                           [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }) !=
               name.end();
    };
    std::array<std::size_t, MaximumProfiles> visibleRows{};
    std::size_t visibleCount{};
    for (std::size_t i = 0; i < view.entries.size() && visibleCount < visibleRows.size(); ++i)
        if (matches(view.entries[i].name))
            visibleRows[visibleCount++] = i;
    const bool selectedVisible = !selected.empty() && matches(selected);
    const float listHeight = std::clamp(static_cast<float>(visibleCount) * 47.f + 14.f, 70.f, 188.f) * scale;
    if (ImGui::BeginListBox("##ProfileLibrary", {-FLT_MIN, listHeight})) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 4 * scale});
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visibleCount), 47 * scale);
        if (!pendingReveal.empty())
            for (std::size_t row = 0; row < visibleCount; ++row)
                if (view.entries[visibleRows[row]].name == pendingReveal) {
                    clipper.IncludeItemByIndex(static_cast<int>(row));
                    break;
                }
        while (clipper.Step())
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto &entry = view.entries[visibleRows[row]];
                ImGui::PushID(entry.name.c_str());
                const auto origin = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##Profile", selected == entry.name, 0, {0, 43 * scale})) {
                    selected = entry.name;
                    strncpy_s(renameName, selected.c_str(), _TRUNCATE);
                }
                if (testing::enabled) {
                    const auto label = "Profile: " + entry.name;
                    testing::Record(label.c_str());
                }
                if (entry.name == pendingReveal) {
                    ImGui::SetScrollHereY(.5f);
                    pendingReveal.clear();
                }
                auto *draw = ImGui::GetWindowDrawList();
                draw->AddText({origin.x + 9 * scale, origin.y + 3 * scale}, studio::Packed(studio::Text),
                              entry.name.c_str());
                const auto detail = entry.modified + "   /   " + std::to_string((entry.bytes + 1023) / 1024) + " KB";
                draw->AddText({origin.x + 9 * scale, origin.y + 23 * scale}, studio::Packed(studio::Muted),
                              detail.c_str());
                ImGui::PopID();
            }
        if (!visibleCount)
            ImGui::TextDisabled(view.entries.empty() ? "Your saved profiles will appear here."
                                                     : "No matching profiles.");
        ImGui::PopStyleVar();
        ImGui::EndListBox();
    }
    ImGui::BeginDisabled(view.busy || !selectedVisible);
    if (ImGui::BeginTable("Profile actions", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        if (studio::Button("Load profile", {-FLT_MIN, 31 * scale}))
            action(Operation::Load, selected);
        ImGui::TableNextColumn();
        if (studio::Button("Replace with current", {-FLT_MIN, 31 * scale}))
            action(Operation::Replace, selected);
        ImGui::TableNextColumn();
        if (studio::Button("Duplicate", {-FLT_MIN, 31 * scale}))
            action(Operation::Duplicate, selected);
        ImGui::TableNextColumn();
        if (studio::Button("Export profile...", {-FLT_MIN, 31 * scale}))
            action(Operation::Export, selected);
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Replace updates only the selected saved profile.");
    if (ImGui::CollapsingHeader("Manage selected profile")) {
        ImGui::BeginDisabled(view.busy || !selectedVisible);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##RenameProfile", "New name for the selected profile", renameName,
                                 sizeof(renameName));
        ImGui::BeginDisabled(!ValidName(renameName) || selected == renameName);
        if (studio::Button("Rename profile"))
            request = {Operation::Rename, selected, renameName, {}};
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (studio::Button("Archive profile"))
            action(Operation::Archive, selected);
        ImGui::EndDisabled();
        ImGui::TextWrapped("Archive keeps a recoverable file in the profile folder. Import it again to restore it.");
    }
    studio::EndCard();
    studio::Card("Save a new setup");
    ImGui::BeginDisabled(view.busy);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##NewProfileName", "Profile name", newName, sizeof(newName));
    testing::Record("Profile name");
    ImGui::BeginDisabled(!ValidName(newName) || view.entries.size() >= MaximumProfiles);
    if (studio::Button("Create profile", {-FLT_MIN, 31 * scale}))
        action(Operation::Create, newName);
    ImGui::EndDisabled();
    if (ImGui::BeginTable("Profile file actions", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        if (studio::Button("Import...", {-FLT_MIN, 30 * scale}))
            action(Operation::Import);
        ImGui::TableNextColumn();
        if (studio::Button("Refresh", {-FLT_MIN, 30 * scale}))
            action(Operation::Refresh);
        ImGui::TableNextColumn();
        if (studio::Button("Open folder", {-FLT_MIN, 30 * scale}))
            action(Operation::OpenFolder);
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("INI profiles include every setting. Fonts, images and sounds keep their file paths.");
    studio::EndCard();
}
} // namespace awareness::profiles
