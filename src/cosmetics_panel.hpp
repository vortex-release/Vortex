#pragma once
// Included after overlay_panel.hpp's shared controls. No native calls or catalog
// methods are used here, so the standalone menu/demo can render the same panel.
#include "cosmetics_options.hpp"
#include "economy_catalog.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

namespace cosmetics_panel_detail {
using awareness::cosmetics::Catalog;
using awareness::cosmetics::Definition;
using awareness::cosmetics::Finish;
using awareness::cosmetics::ItemKind;
using awareness::cosmetics::PaintKit;
inline char Fold(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}
inline bool Contains(std::string_view value, std::string_view query) noexcept {
    if (query.empty())
        return true;
    if (query.size() > value.size())
        return false;
    for (std::size_t at = 0; at <= value.size() - query.size(); ++at) {
        bool same = true;
        for (std::size_t i = 0; i < query.size(); ++i)
            if (Fold(value[at + i]) != Fold(query[i])) {
                same = false;
                break;
            }
        if (same)
            return true;
    }
    return false;
}
struct CatalogKey {
    const Catalog *owner{};
    const void *definitions{}, *paints{}, *pairs{};
    std::size_t definitionCount{}, paintCount{}, pairCount{};
    bool operator==(const CatalogKey &) const = default;
};
inline CatalogKey Key(const Catalog *c) noexcept {
    return c ? CatalogKey{c,
                          c->definitions.data(),
                          c->paints.data(),
                          c->pairs.data(),
                          c->definitions.size(),
                          c->paints.size(),
                          c->pairs.size()}
             : CatalogKey{};
}
inline const Definition *DefinitionById(const Catalog *catalog, unsigned id) noexcept {
    if (catalog)
        for (const auto &item : catalog->definitions)
            if (item.id == id)
                return &item;
    return nullptr;
}
inline const PaintKit *PaintById(const Catalog *catalog, unsigned id) noexcept {
    if (catalog)
        for (const auto &paint : catalog->paints)
            if (paint.id == id)
                return &paint;
    return nullptr;
}
inline bool Supports(const Catalog *catalog, unsigned definition, unsigned paint) noexcept {
    if (!paint)
        return true;
    if (catalog)
        for (const auto &pair : catalog->pairs)
            if (pair.definition == definition && pair.paintKit == paint)
                return true;
    return false;
}
struct PaintCache {
    CatalogKey key;
    unsigned definition{0xFFFFFFFF};
    std::array<char, 96> search{};
    std::vector<unsigned> allowed;
    std::vector<std::size_t> compatible, visible;
    void Refresh(const Catalog *catalog, unsigned selected, bool searchChanged) {
        const auto next = Key(catalog);
        const bool changed = key != next || definition != selected;
        if (changed) {
            key = next;
            definition = selected;
            allowed.clear();
            compatible.clear();
            visible.clear();
            // Catalog parsing is bounded; storage grows only on catalog/item or
            // search changes, never while drawing a stable list each frame.
            if (catalog) {
                for (const auto &pair : catalog->pairs)
                    if (pair.definition == selected)
                        allowed.push_back(pair.paintKit);
                std::sort(allowed.begin(), allowed.end());
                allowed.erase(std::unique(allowed.begin(), allowed.end()), allowed.end());
                for (std::size_t i = 0; i < catalog->paints.size(); ++i)
                    if (std::binary_search(allowed.begin(), allowed.end(), catalog->paints[i].id))
                        compatible.push_back(i);
                std::sort(compatible.begin(), compatible.end(),
                          [&](auto a, auto b) { return catalog->paints[a].name < catalog->paints[b].name; });
                visible.reserve(compatible.size());
            }
        }
        if ((changed || searchChanged) && catalog) {
            visible.clear();
            for (const auto i : compatible) {
                const auto &paint = catalog->paints[i];
                char id[16]{};
                std::snprintf(id, sizeof(id), "%u", paint.id);
                if (Contains(paint.name, search.data()) || Contains(paint.internalName, search.data()) ||
                    Contains(id, search.data()))
                    visible.push_back(i);
            }
        }
    }
};
inline bool NumberRow(const char *label, std::uint32_t &value, std::uint32_t maximum) {
    FormRow(label);
    bool changed = ImGui::InputScalar("##value", ImGuiDataType_U32, &value);
    if (changed)
        value = (std::min)(value, maximum);
    ImGui::PopID();
    return changed;
}
inline bool FinishPicker(Finish &finish, const Catalog *catalog, unsigned definition, PaintCache &cache) {
    cache.Refresh(catalog, definition, false);
    const auto *selected = PaintById(catalog, finish.paintKit);
    char unavailable[48]{};
    std::snprintf(unavailable, sizeof(unavailable), "Finish #%u", finish.paintKit);
    const char *label = !finish.paintKit ? "Default finish" : selected ? selected->name.c_str() : unavailable;
    FormRow("Finish");
    bool changed{};
    ImGui::BeginDisabled(!catalog || !definition);
    ImGui::SetNextWindowSizeConstraints({0, 0}, {FLT_MAX, 350 * ImGui::GetStyle().FontScaleMain});
    if (ImGui::BeginCombo("##finish", label, ImGuiComboFlags_HeightLarge)) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool queryChanged =
            ImGui::InputTextWithHint("##search", "Search finishes or ID", cache.search.data(), cache.search.size());
        cache.Refresh(catalog, definition, queryChanged);
        if (ImGui::Selectable("Default finish", finish.paintKit == 0)) {
            finish.paintKit = 0;
            changed = true;
        }
        ImGui::Separator();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(cache.visible.size()));
        while (clipper.Step())
            for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n) {
                const auto &paint = catalog->paints[cache.visible[n]];
                ImGui::PushID(static_cast<int>(paint.id));
                if (ImGui::Selectable(paint.name.c_str(), finish.paintKit == paint.id)) {
                    finish.paintKit = paint.id;
                    const auto lo = std::clamp(paint.minWear, 0.f, 1.f), hi = std::clamp(paint.maxWear, lo, 1.f);
                    finish.wear = std::clamp(finish.wear, lo, hi);
                    changed = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("Finish %u", paint.id);
                ImGui::PopID();
            }
        if (cache.visible.empty())
            ImGui::TextDisabled(cache.compatible.empty() ? "No finishes for this item" : "No matching finishes");
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    return changed;
}
inline bool FinishEditor(Finish &finish, const Catalog *catalog, unsigned definition, PaintCache &cache,
                         bool namedAndTracked = true) {
    bool changed = FlagControl("Custom finish", finish.enabled);
    ImGui::BeginDisabled(!finish.enabled);
    if (BeginForm("Finish settings")) {
        changed |= FinishPicker(finish, catalog, definition, cache);
        const auto *paint = PaintById(catalog, finish.paintKit);
        const float lo = paint ? std::clamp(paint->minWear, 0.f, 1.f) : 0;
        const float hi = paint ? std::clamp(paint->maxWear, lo, 1.f) : 1;
        if (hi - lo > .00001f)
            changed |= FloatRow("Wear", finish.wear, lo, hi, "%.4f");
        else {
            FormRow("Wear");
            ImGui::Text("%.4f", lo);
            ImGui::PopID();
        }
        changed |= NumberRow("Pattern seed", finish.seed, 1000);
        if (namedAndTracked) {
            FormRow("Name tag");
            const auto before = finish.name;
            if (ImGui::InputTextWithHint("##name", "Original name", finish.name.data(), finish.name.size())) {
                if (awareness::cosmetics::ValidName(finish.name))
                    changed = true;
                else
                    finish.name = before;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (namedAndTracked) {
        changed |= FlagControl("StatTrak", finish.statTrak);
        if (finish.statTrak && BeginForm("StatTrak settings")) {
            changed |= NumberRow("Kills", finish.kills, 999999);
            ImGui::EndTable();
        }
    }
    ImGui::EndDisabled();
    return changed;
}
struct DefinitionCache {
    CatalogKey key;
    ItemKind kind{ItemKind::Weapon};
    unsigned team{999};
    std::vector<std::size_t> filtered;
    void Refresh(const Catalog *catalog, ItemKind requested, unsigned requestedTeam) {
        const auto next = Key(catalog);
        if (key == next && kind == requested && team == requestedTeam)
            return;
        key = next;
        kind = requested;
        team = requestedTeam;
        filtered.clear();
        if (!catalog)
            return;
        for (std::size_t i = 0; i < catalog->definitions.size(); ++i) {
            const auto &d = catalog->definitions[i];
            if (d.kind == kind && !d.model.empty() && (!team || !d.team || d.team == team))
                filtered.push_back(i);
        }
        std::sort(filtered.begin(), filtered.end(),
                  [&](auto a, auto b) { return catalog->definitions[a].name < catalog->definitions[b].name; });
    }
};
inline bool DefinitionPicker(const char *label, unsigned &value, const Catalog *catalog, ItemKind kind, unsigned team,
                             DefinitionCache &cache) {
    cache.Refresh(catalog, kind, team);
    const auto *selected = DefinitionById(catalog, value);
    FormRow(label);
    bool changed{};
    ImGui::BeginDisabled(!catalog);
    if (ImGui::BeginCombo("##model",
                          !value     ? "Original"
                          : selected ? selected->name.c_str()
                                     : "Unavailable model",
                          ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("Original", value == 0)) {
            value = 0;
            changed = true;
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(cache.filtered.size()));
        while (clipper.Step())
            for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n) {
                const auto &d = catalog->definitions[cache.filtered[n]];
                ImGui::PushID(static_cast<int>(d.id));
                if (ImGui::Selectable(d.name.c_str(), value == d.id)) {
                    value = d.id;
                    changed = true;
                }
                ImGui::PopID();
            }
        if (cache.filtered.empty())
            ImGui::TextDisabled("No compatible models");
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    return changed;
}
inline bool AppearanceEditor(awareness::cosmetics::Appearance &appearance, const Catalog *catalog, ItemKind kind,
                             DefinitionCache &models, PaintCache &paints) {
    ImGui::BeginDisabled(!appearance.definition);
    bool changed = FlagControl("Replace model", appearance.enabled);
    ImGui::EndDisabled();
    if (!appearance.definition)
        studio::Tip("Select a model first.");
    if (BeginForm("Model settings")) {
        if (DefinitionPicker("Model", appearance.definition, catalog, kind, 0, models)) {
            appearance.enabled = appearance.definition != 0;
            if (appearance.definition && !Supports(catalog, appearance.definition, appearance.finish.paintKit))
                appearance.finish.paintKit = 0;
            changed = true;
        }
        ImGui::EndTable();
    }
    ImGui::Separator();
    ImGui::BeginDisabled(!appearance.definition);
    changed |= FinishEditor(appearance.finish, catalog, appearance.definition, paints, kind != ItemKind::Glove);
    ImGui::EndDisabled();
    if (studio::Button("Reset item")) {
        appearance = {};
        changed = true;
    }
    return changed;
}
} // namespace cosmetics_panel_detail
inline bool DrawCosmeticsPanel(awareness::cosmetics::Options &options, const awareness::cosmetics::Catalog *catalog,
                               const char *status) {
    using namespace cosmetics_panel_detail;
    static unsigned weapon{};
    static std::array<PaintCache, 3> paints;
    static std::array<DefinitionCache, 4> models;
    bool changed{};
    ImGui::PushID("LoadoutPanel");
    studio::Card("Loadout");
    changed |= FlagControl("Enabled", options.enabled);
    if (status && *status)
        ImGui::TextDisabled("%s", status);
    studio::EndCard();
    if (ImGui::BeginTabBar("LoadoutSections")) {
        if (studio::Tab("Weapons")) {
            studio::Card("Weapon finish");
            if (BeginForm("Weapon selection")) {
                FormRow("Weapon");
                weapon = (std::min)(weapon, static_cast<unsigned>(awareness::cosmetics::WeaponCount - 1));
                if (ImGui::BeginCombo("##weapon", awareness::WeaponIcons[weapon].name, ImGuiComboFlags_HeightLarge)) {
                    for (unsigned i = 0; i < awareness::cosmetics::WeaponCount; ++i)
                        if (ImGui::Selectable(awareness::WeaponIcons[i].name, weapon == i))
                            weapon = i;
                    ImGui::EndCombo();
                }
                ImGui::PopID();
                ImGui::EndTable();
            }
            changed |= FinishEditor(options.weapons[weapon], catalog, awareness::WeaponIcons[weapon].id, paints[0]);
            if (studio::Button("Reset item")) {
                options.weapons[weapon] = {};
                changed = true;
            }
            studio::EndCard();
            ImGui::EndTabItem();
        }
        if (studio::Tab("Knife")) {
            studio::Card("Knife");
            changed |= AppearanceEditor(options.knife, catalog, ItemKind::Knife, models[0], paints[1]);
            studio::EndCard();
            ImGui::EndTabItem();
        }
        if (studio::Tab("Gloves")) {
            studio::Card("Gloves");
            changed |= AppearanceEditor(options.glove, catalog, ItemKind::Glove, models[1], paints[2]);
            studio::EndCard();
            ImGui::EndTabItem();
        }
        if (studio::Tab("Agents")) {
            studio::Card("Team models");
            if (BeginForm("Agent selection")) {
                changed |= DefinitionPicker("Terrorists", options.agents[0], catalog, ItemKind::Agent, 2, models[2]);
                changed |=
                    DefinitionPicker("Counter-Terrorists", options.agents[1], catalog, ItemKind::Agent, 3, models[3]);
                ImGui::EndTable();
            }
            if (studio::Button("Original agents")) {
                options.agents = {};
                changed = true;
            }
            studio::EndCard();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopID();
    return changed;
}
