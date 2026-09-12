#pragma once
#include "grenade_lineups.hpp"
#include "ui_theme.hpp"
#include <cstring>
#include <cctype>
namespace awareness::lineups {
struct PanelState {
    char name[96]{}, notes[256]{}, filter[96]{};
    int throwType{};
    std::uint64_t selected{}, revision{};
    Record editing;
    std::string error;
    std::vector<std::size_t> visibleRows;
};
inline bool DrawPanel(PanelState &state, Options &options, const Library &library, const Capture &capture, bool busy,
                      std::string_view message, Request &request, double now) {
    bool changed = studio::Toggle("Lineup guides", options.enabled);
    changed |= studio::Toggle("Held grenade only", options.heldOnly);
    ImGui::SetNextItemWidth(std::max(100.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Guide range").x -
                                                ImGui::GetStyle().ItemInnerSpacing.x));
    changed |= ImGui::SliderFloat("Guide range", &options.range, 5, 150, "%.0f m");
    if (ImGui::TreeNode("Guide appearance")) {
        changed |= ImGui::SliderFloat("Stand tolerance", &options.standTolerance, 1, 32, "%.0f units");
        changed |= ImGui::SliderFloat("Aim tolerance", &options.aimTolerance, .1f, 5, "%.1f deg");
        changed |= ImGui::ColorEdit4("Guide color", &options.color.r, ImGuiColorEditFlags_NoInputs);
        ImGui::TreePop();
    }
    ImGui::Separator();
    Capture effective = capture;
    if (capture.map.empty()) {
        char mapInput[96]{};
        strncpy_s(mapInput, options.mapOverride, _TRUNCATE);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##LineupMapOverride", "Map name, e.g. de_mirage", mapInput, sizeof(mapInput))) {
            const auto normalized = NormalizeMap(mapInput);
            if (!mapInput[0] || !normalized.empty()) {
                strncpy_s(options.mapOverride, normalized.c_str(), _TRUNCATE);
                changed = true;
            }
        }
        effective.map = NormalizeMap(options.mapOverride);
        if (!effective.map.empty())
            ImGui::TextDisabled("Manual map: %s", effective.map.c_str());
        else
            ImGui::TextDisabled("Enter the current map to use guides.");
    } else
        ImGui::TextDisabled("%s", capture.map.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##LineupName", "Lineup name", state.name, sizeof(state.name));
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##LineupThrow", &state.throwType, ThrowNames, static_cast<int>(std::size(ThrowNames)));
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##LineupNotes", "Throw instructions (optional)", state.notes, sizeof(state.notes));
    const bool canCapture = options.enabled && Fresh(effective, now) && !effective.map.empty() &&
                            WeaponKind(effective.weapon) != Kind::Any && state.name[0];
    ImGui::BeginDisabled(busy || !canCapture);
    if (studio::Button("Capture position")) {
        try {
            state.error.clear();
            request.operation = Operation::Capture;
            request.record = MakeRecord(effective, state.name, static_cast<unsigned>(state.throwType), state.notes);
        } catch (const std::exception &error) {
            state.error = error.what();
            request = {};
        }
    }
    ImGui::EndDisabled();
    if (!canCapture && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(options.enabled ? "Enter a name and hold a grenade in a loaded map."
                                          : "Enable lineup guides to capture a position.");
    ImGui::BeginDisabled(busy);
    ImGui::SameLine();
    if (studio::Button("Import JSON"))
        request.operation = Operation::Import;
    ImGui::SameLine();
    if (studio::Button("Export JSON"))
        request.operation = Operation::Export;
    if (studio::Button("Reload library"))
        request.operation = Operation::Reload;
    ImGui::SameLine();
    if (studio::Button("Open folder"))
        request.operation = Operation::OpenFolder;
    ImGui::EndDisabled();
    if (!state.error.empty())
        ImGui::TextWrapped("%s", state.error.c_str());
    else if (!message.empty())
        ImGui::TextWrapped("%.*s", static_cast<int>(message.size()), message.data());
    if (busy)
        ImGui::TextDisabled("Working...");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##FindLineup", "Find a lineup", state.filter, sizeof(state.filter));
    ImGui::BeginDisabled(busy);
    state.visibleRows.clear();
    const std::string_view filter(state.filter);
    const auto contains = [&](const std::string &text) {
        return filter.empty() || std::search(text.begin(), text.end(), filter.begin(), filter.end(),
                                             [](unsigned char a, unsigned char b) {
                                                 return std::tolower(a) == std::tolower(b);
                                             }) != text.end();
    };
    for (std::size_t index = 0; index < library.Records().size(); ++index) {
        const auto &record = library.Records()[index];
        if (contains(record.name) || contains(record.map))
            state.visibleRows.push_back(index);
    }
    if (ImGui::BeginListBox("##LineupLibrary", {-FLT_MIN, 140})) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(state.visibleRows.size()));
        while (clipper.Step())
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto &record = library.Records()[state.visibleRows[static_cast<std::size_t>(row)]];
                ImGui::PushID(static_cast<int>(record.id));
                const std::string label = record.name + "  /  " + record.map;
                if (ImGui::Selectable(label.c_str(), state.selected == record.id)) {
                    state.selected = record.id;
                    state.editing = record;
                }
                ImGui::PopID();
            }
        if (library.Records().empty())
            ImGui::TextDisabled("Capture a lineup or import a JSON pack.");
        else if (state.visibleRows.empty())
            ImGui::TextDisabled("No matching lineups.");
        ImGui::EndListBox();
    }
    if (const auto *selected = library.Find(state.selected)) {
        auto &edit = state.editing;
        if (edit.id != selected->id)
            edit = *selected;
        char editName[96]{}, editNotes[256]{};
        strncpy_s(editName, edit.name.c_str(), _TRUNCATE);
        strncpy_s(editNotes, edit.notes.c_str(), _TRUNCATE);
        if (ImGui::InputText("Name", editName, sizeof(editName)))
            edit.name = editName;
        if (ImGui::InputText("Instructions", editNotes, sizeof(editNotes)))
            edit.notes = editNotes;
        int type = static_cast<int>(edit.throwType), kind = static_cast<int>(edit.kind);
        if (ImGui::Combo("Throw", &type, ThrowNames, static_cast<int>(std::size(ThrowNames))))
            edit.throwType = static_cast<unsigned>(type);
        if (ImGui::Combo("Grenade", &kind, KindNames, static_cast<int>(std::size(KindNames))))
            edit.kind = static_cast<Kind>(kind);
        ImGui::Checkbox("Visible", &edit.enabled);
        if (studio::Button("Save lineup")) {
            request.operation = Operation::Update;
            request.record = edit;
        }
        ImGui::SameLine();
        if (studio::Button("Remove lineup")) {
            request.operation = Operation::Remove;
            request.record = *selected;
        }
    }
    ImGui::EndDisabled();
    return changed;
}
inline bool DrawPanel(PanelState &state, Options &options, Controller &controller, const Capture &capture, double now) {
    controller.Tick();
    if (state.revision != controller.Revision()) {
        state.revision = controller.Revision();
        state.selected = 0;
        state.editing = {};
        state.error.clear();
    }
    Request request;
    const bool changed =
        DrawPanel(state, options, controller.Records(), capture, controller.Busy(), controller.Message(), request, now);
    if (request.operation != Operation::None)
        controller.Submit(std::move(request));
    return changed;
}
} // namespace awareness::lineups
