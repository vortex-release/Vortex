#pragma once
#include "settings.hpp"
#include "player_decoration.hpp"
#include "presets.hpp"
#include "cs2_offsets.hpp"
#include "spectator_reader.hpp"
#include "weapon_icons.hpp"
#include "ui_theme.hpp"
#include "targeting.hpp"
#include "preview_pose.hpp"
#include "font_catalog.hpp"
#include "profile_panel.hpp"
#include "../app/app_paths.hpp"
#include <cstdio>
#include <cstring>
struct PanelInformation {
    const char *message{"Waiting for entity data"};
    bool cs2{}, ready{};
    std::uint32_t controllers{}, pawns{}, failedReads{}, gameBuild{}, offsetBuild{};
    bool buildVerified{};
    std::uint32_t rawBuildValue{}, inactivePawns{}, drawn{};
    std::uintptr_t buildRva{};
    std::uint64_t ageMs{};
    awareness::assist::Diagnostics assists;
    unsigned skyCount{}, skyFailed{};
    int pingMs{-1};
    float fps{}, renderMs{}, readMs{}, totalMs{};
    const char *settingsMessage{};
    bool profileIoBusy{};
    const awareness::profiles::View *profiles{};
    awareness::TrackingState trackingState{};
    awareness::EffectsState effectsState{};
    std::uint32_t bonePositions{}, failedBoneReads{};
    bool waitingForBind{};
    std::uint64_t tracerCallbacks{}, acceptedTracers{}, recoilWrites{};
    bool recoilInput{};
    HRESULT recoilResult{S_FALSE};
    const char *soundStatus{}, *hitSoundStatus{};
    bool pathsConnected{}, viewConnected{}, eventsConnected{}, tracerConnected{}, punchConnected{};
    std::uint64_t fireSamples{}, fireEvents{}, impactEvents{}, rejectedTracers{}, recoilReadFailures{};
    std::uint32_t recoilWeapon{}, recoilShots{};
    awareness::Vector3 recoilPunch{};
    std::uint32_t visibleTracers{}, clippedTracers{};
    bool bulletConnected{}, particleConnected{};
    std::uint64_t bulletCallbacks{}, particleCallbacks{};
    const char *appVersion{};
    const awareness::cs2::SpectatorFrame *spectators{};
    const char *sessionStatus{"Off"};
    unsigned acceptedMatches{};
    std::uint32_t trackingWeapon{}, droppedWeapons{};
    std::uint64_t footstepEvents{};
    bool keepAwakeActive{};
    std::uint32_t utilityEntities{}, fireEntities{}, burningCells{}, fireReadFailures{}, areaCount{};
};
struct PanelActions {
    awareness::profiles::Request profile;
    ImVec4 menuBounds{};
    bool rescan{}, save{}, load{}, reset{}, browse{}, reloadBackground{};
    bool beginBind{}, browseSound{}, testSound{}, previewVisible{};
    bool browseHitSound{}, testHitSound{};
    float previewDrag{};
};
struct PanelMedia {
    ImTextureRef background, model;
    ImVec2 backgroundSize{};
    const char *backgroundStatus{};
    const awareness::PreviewPose *pose{};
    ImTextureRef steamAvatar;
    const char *steamName{"Steam user"};
    bool steamConnected{};
};
inline bool FlagControl(const char *label, std::uint32_t &v) {
    return studio::Toggle(label, v);
}
inline bool ColorControl(const char *label, awareness::Color &c, bool alpha = true) {
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                         28 * ImGui::GetStyle().FontScaleMain);
    float color[]{c.r, c.g, c.b, c.a};
    const bool changed = ImGui::ColorEdit4("##color", color,
                                           ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel |
                                               (alpha ? ImGuiColorEditFlags_AlphaBar : ImGuiColorEditFlags_NoAlpha));
    if (changed)
        c = {color[0], color[1], color[2], color[3]};
    ImGui::PopID();
    return changed;
}
inline bool BeginForm(const char *name) {
    if (!ImGui::BeginTable(name, 2, ImGuiTableFlags_SizingStretchProp))
        return false;
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed,
                            (std::min)(142 * ImGui::GetStyle().FontScaleMain, width * .39f));
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}
inline void FormRow(const char *label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextWrapped("%s", label);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushID(label);
}
inline bool FloatRow(const char *label, float &value, float lo, float hi, const char *format = "%.0f") {
    FormRow(label);
    const float scale = ImGui::GetStyle().FontScaleMain;
    const float available = ImGui::GetContentRegionAvail().x;
    char text[64]{};
    std::snprintf(text, sizeof(text), format, value);
    const float numberWidth =
        (std::min)(available * .46f, (std::max)(48 * scale, ImGui::CalcTextSize(text).x + 16 * scale));
    ImGui::SetNextItemWidth((std::max)(16.f, available - numberWidth - 8 * scale));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, {0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, {0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, {0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, {0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, {0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_Text, {0, 0, 0, 0});
    bool changed =
        ImGui::SliderFloat("##value", &value, lo, hi, format, ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
    awareness::testing::Record(label);
    ImGui::PopStyleColor(6);
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const float y = (a.y + b.y) * .5f,
                x = a.x + 5 * scale + (b.x - a.x - 10 * scale) * std::clamp((value - lo) / (hi - lo), 0.f, 1.f);
    auto *draw = ImGui::GetWindowDrawList();
    const float hover =
        studio::Animate(ImGui::GetItemID(), ImGui::IsItemActive() || ImGui::IsItemHovered() ? 1.f : 0.f);
    draw->AddLine({a.x + 5 * scale, y}, {b.x - 5 * scale, y}, studio::Packed({1, 1, 1, .08f}), 3 * scale);
    draw->AddLine({a.x + 5 * scale, y}, {x, y}, studio::Packed(studio::Alpha(studio::Accent, .7f)), 3 * scale);
    if (hover > .001f)
        draw->AddCircleFilled({x, y}, (8 + hover) * scale, studio::Packed(studio::Alpha(studio::Accent, .1f * hover)),
                              20);
    draw->AddCircleFilled({x, y}, 5 * scale, studio::Packed(studio::Accent), 20);
    studio::Interaction();
    ImGui::SameLine(0, 8 * scale);
    ImGui::SetNextItemWidth((std::max)(1.f, numberWidth));
    changed |= ImGui::DragFloat("##number", &value, (hi - lo) * .002f, lo, hi, format, ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Drag the value, or Ctrl+click to type.");
    ImGui::PopID();
    return changed;
}
inline bool ComboRow(const char *label, int &value, const char *choices) {
    FormRow(label);
    const bool changed = ImGui::Combo("##value", &value, choices);
    awareness::testing::Record(label);
    ImGui::PopID();
    return changed;
}
inline bool FontRow(const char *label, std::uint32_t &selected) {
    FormRow(label);
    bool changed = false;
    const auto index = (std::min)(selected, static_cast<std::uint32_t>(awareness::FontPresets.size() - 1));
    if (ImGui::BeginCombo("##font", awareness::FontPresets[index].name)) {
        for (std::uint32_t i = 0; i < awareness::FontPresets.size(); ++i) {
            static const auto installed = [] {
                std::array<bool, awareness::FontPresets.size()> result{};
                for (std::size_t n = 0; n < result.size(); ++n)
                    result[n] = n == 0 || !awareness::FontFile(n).empty();
                return result;
            }();
            const bool available = installed[i];
            ImGui::BeginDisabled(!available);
            if (ImGui::Selectable(awareness::FontPresets[i].name, selected == i)) {
                selected = i;
                changed = true;
            }
            if (selected == i)
                ImGui::SetItemDefaultFocus();
            ImGui::EndDisabled();
            if (!available && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("This font is not installed on this PC.");
        }
        ImGui::EndCombo();
    }
    awareness::testing::Record(label);
    ImGui::PopID();
    return changed;
}
inline bool UtilityStyle(awareness::flight::PathStyle &s, bool preview) {
    bool changed = FlagControl("Glow", preview ? s.previewGlow : s.trailGlow);
    if (BeginForm("Stroke")) {
        changed |= FloatRow("Width", preview ? s.previewWidth : s.trailWidth, 1, 5, "%.1f");
        if (preview ? s.previewGlow : s.trailGlow)
            changed |= FloatRow("Glow strength", preview ? s.previewStrength : s.trailStrength, 0, 3, "%.1f");
        ImGui::EndTable();
    }
    if (ImGui::BeginTable("Utility colors", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) {
        const char *labels[]{"HE", "Smoke", "Flash", "Molotov", "Decoy"};
        awareness::Color *colors[]{preview ? &s.previewHE : &s.trailHE, preview ? &s.previewSmoke : &s.trailSmoke,
                                   preview ? &s.previewFlash : &s.trailFlash, preview ? &s.previewFire : &s.trailFire,
                                   preview ? &s.previewDecoy : &s.trailDecoy};
        for (int i = 0; i < 5; ++i) {
            ImGui::TableNextColumn();
            changed |= ColorControl(labels[i], *colors[i]);
        }
        ImGui::EndTable();
    }
    return changed;
}
inline void SteamCard(const PanelMedia &media, float scale) {
    auto *draw = ImGui::GetWindowDrawList();
    const auto p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    draw->AddRectFilled(p, {p.x + width, p.y + 62 * scale}, IM_COL32(255, 255, 255, 7), 7 * scale);
    const ImVec2 a{p.x + 8 * scale, p.y + 13 * scale}, b{a.x + 36 * scale, a.y + 36 * scale};
    if (media.steamAvatar.GetTexID())
        draw->AddImageRounded(media.steamAvatar, a, b, {0, 0}, {1, 1}, IM_COL32_WHITE, 18 * scale);
    else {
        draw->AddCircleFilled({a.x + 18 * scale, a.y + 18 * scale}, 18 * scale,
                              studio::Packed(studio::Alpha(studio::Accent, .13f)), 32);
        draw->AddCircle({a.x + 18 * scale, a.y + 13 * scale}, 5 * scale, studio::Packed(studio::Muted), 20,
                        1.4f * scale);
        draw->AddBezierQuadratic({a.x + 8 * scale, a.y + 29 * scale}, {a.x + 18 * scale, a.y + 16 * scale},
                                 {a.x + 28 * scale, a.y + 29 * scale}, studio::Packed(studio::Muted), 1.4f * scale);
    }
    draw->AddCircle({a.x + 18 * scale, a.y + 18 * scale}, 19 * scale,
                    studio::Packed(studio::Alpha(studio::Accent, .3f)), 32);
    const float x = b.x + 9 * scale;
    draw->PushClipRect({x, p.y}, {p.x + width - 6 * scale, p.y + 62 * scale}, true);
    draw->AddText({x, p.y + 15 * scale}, studio::Packed(studio::Text),
                  media.steamName ? media.steamName : "Steam user");
    draw->AddText(ImGui::GetFont(), 12 * scale, {x, p.y + 34 * scale}, studio::Packed(studio::Muted),
                  media.steamConnected ? "Steam" : "Steam offline");
    draw->PopClipRect();
    ImGui::InvisibleButton("Steam profile", {width, 62 * scale});
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", media.steamName ? media.steamName : "Steam user");
}
inline void DrawMenuBackground(const awareness::VisualOptions &v, const PanelMedia &media) {
    if (!v.backgroundEnabled || !media.background.GetTexID() || media.backgroundSize.x <= 0 ||
        media.backgroundSize.y <= 0)
        return;
    const auto a = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    ImVec2 p = a, q{a.x + size.x, a.y + size.y}, uv0{}, uv1{1, 1};
    const float source = media.backgroundSize.x / media.backgroundSize.y, dest = size.x / size.y;
    if (v.backgroundFit == 0) {
        if (source > dest) {
            const float margin = (1 - dest / source) * .5f;
            uv0.x = margin;
            uv1.x = 1 - margin;
        } else {
            const float margin = (1 - source / dest) * .5f;
            uv0.y = margin;
            uv1.y = 1 - margin;
        }
    } else {
        if (source > dest) {
            const float height = size.x / source;
            p.y += (size.y - height) * .5f;
            q.y = p.y + height;
        } else {
            const float width = size.y * source;
            p.x += (size.x - width) * .5f;
            q.x = p.x + width;
        }
    }
    ImGui::GetWindowDrawList()->PushClipRect(a, {a.x + size.x, a.y + size.y}, false);
    ImGui::GetWindowDrawList()->AddImageRounded(media.background, p, q, uv0, uv1,
                                                IM_COL32(255, 255, 255, static_cast<int>(255 * v.backgroundOpacity)),
                                                ImGui::GetStyle().WindowRounding);
    ImGui::GetWindowDrawList()->PopClipRect();
}
inline void DrawPreview(const awareness::Configuration &c, awareness::VisualOptions &v, ImTextureRef atlas,
                        const PanelMedia &media, PanelActions &actions, bool &changed) {
    actions.previewVisible = true;
    const float s = ImGui::GetStyle().FontScaleMain, w = ImGui::GetContentRegionAvail().x, h = w * 1.5f;
    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("Rotate model", {w, h});
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        actions.previewDrag = ImGui::GetIO().MouseDelta.x * .012f;
    auto *d = ImGui::GetWindowDrawList();
    const float cx = origin.x + w * .5f;
    d->AddEllipseFilled({cx, origin.y + h * .9f}, {w * .28f, 9 * s}, IM_COL32(0, 0, 0, 80));
    if (media.model.GetTexID())
        d->AddImage(media.model, origin, {origin.x + w, origin.y + h});
    else
        d->AddText({origin.x + 8 * s, origin.y + 30 * s}, studio::Packed(studio::Muted), "Preview unavailable");
    const bool live = media.pose && media.pose->valid;
    const auto *entity = live ? &media.pose->entity : nullptr;
    const float health = entity ? std::clamp(entity->health / entity->maxHealth, 0.f, 1.f) : 1.f;
    const auto col = c.colorBoxesByHealth ? (health <= c.lowHealthThreshold      ? c.low
                                             : health <= c.mediumHealthThreshold ? c.medium
                                                                                 : c.healthy)
                                          : c.opponent;
    const ImVec2 a{cx - w * .26f, origin.y + h * .105f}, b{cx + w * .26f, origin.y + h * .885f};
    if (c.enabled && c.boxes) {
        const awareness::ScreenBox box{{a.x, a.y}, {b.x, b.y}};
        if (v.fillBoxes)
            awareness::styling::Fill(*d, box, col, v.fillOpacity * c.opacity, v.playerStyle);
        awareness::styling::Border(*d, box, col, c.outline, c.opacity, c.boxThickness, v.cornerBoxes != 0,
                                   v.playerStyle);
    }
    const auto text = ImGui::ColorConvertFloat4ToU32({c.text.r, c.text.g, c.text.b, c.opacity});
    const auto label = [&](const char *value, float y) {
        const auto size = ImGui::CalcTextSize(value);
        d->AddText({cx - size.x * .5f, y}, text, value);
    };
    if (c.enabled && c.names)
        label(entity ? entity->name : "SAS", origin.y + 2 * s);
    if (c.enabled && c.healthBars) {
        const auto hp = ImGui::ColorConvertFloat4ToU32({c.healthy.r, c.healthy.g, c.healthy.b, c.opacity});
        if (c.healthBar == awareness::HealthBar::Vertical) {
            d->AddRectFilled({a.x - 7 * s, a.y}, {a.x - 4 * s, b.y}, IM_COL32(0, 0, 0, 130));
            d->AddRectFilled({a.x - 7 * s, b.y - (b.y - a.y) * health}, {a.x - 4 * s, b.y}, hp);
        } else {
            d->AddRectFilled({a.x, b.y + 5 * s}, {b.x, b.y + 8 * s}, IM_COL32(0, 0, 0, 130));
            d->AddRectFilled({a.x, b.y + 5 * s}, {a.x + (b.x - a.x) * health, b.y + 8 * s}, hp);
        }
        if (v.healthNumbers) {
            char number[16]{};
            std::snprintf(number, sizeof(number), "%.0f", entity ? entity->health : 100.f);
            d->AddText({a.x - 30 * s, a.y}, text, number);
        }
    }
    if (c.enabled && v.weaponIcons)
        awareness::DrawWeaponIcon(d, atlas, live ? media.pose->weapon : 7, {cx - 35 * s, b.y + 14 * s},
                                  {70 * s, 24 * s}, text);
    changed |= FlagControl("Auto rotate", v.previewRotate);
    if (live)
        ImGui::TextDisabled("Live");
}
#include "combat_panel.hpp"

inline bool DrawFeatureSection(int section, awareness::Configuration &c, awareness::VisualOptions &v,
                               const PanelInformation &info, PanelActions &actions,
                               awareness::TrackingConfiguration &tracking, awareness::EffectsConfiguration &effects,
                               ImTextureRef atlas) {
    using namespace awareness;
    bool changed{};
    if (section == 0) {
        if (ImGui::BeginTabBar("PlayerTabs")) {
            if (studio::Tab("General")) {
                studio::Card("Display");
                changed |= FlagControl("Enabled", c.enabled);
                if (ImGui::BeginTable("Flags", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    changed |= FlagControl("Boxes", c.boxes);
                    ImGui::TableNextColumn();
                    changed |= FlagControl("Names", c.names);
                    ImGui::TableNextColumn();
                    changed |= FlagControl("Health", c.healthBars);
                    ImGui::TableNextColumn();
                    changed |= FlagControl("Distance", c.distances);
                    ImGui::TableNextColumn();
                    changed |= FlagControl("Weapons", v.weaponIcons);
                    ImGui::TableNextColumn();
                    changed |= FlagControl("Lines", c.lines);
                    ImGui::EndTable();
                }
                studio::EndCard();
                if (ImGui::CollapsingHeader("Visibility")) {
                    if (BeginForm("Visibility")) {
                        int team = static_cast<int>(c.teamFilter);
                        if (ComboRow("Players", team, "All\0Opponents\0Teammates\0")) {
                            c.teamFilter = static_cast<TeamFilter>(team);
                            changed = true;
                        }
                        changed |= FloatRow("Range", c.maxDistanceMeters, 10, 500);
                        if (c.fadeStartMeters >= c.maxDistanceMeters) {
                            c.fadeStartMeters = c.maxDistanceMeters * .7f;
                            changed = true;
                        }
                        changed |= FloatRow("Fade start", c.fadeStartMeters, 0, c.maxDistanceMeters - 1);
                        changed |= FloatRow("Opacity", c.opacity, .05f, 1, "%.2f");
                        ImGui::EndTable();
                    }
                }
                ImGui::EndTabItem();
            }
            if (studio::Tab("Models")) {
                studio::Card("Model looks", "Start with a look, then fine-tune its colors and strength.");
                for (int look = 0; look < 3; ++look) {
                    if (look)
                        ImGui::SameLine();
                    const char *names[]{"Silhouette", "Glass", "Neon"};
                    if (studio::Button(names[look])) {
                        ApplyPlayerLook(look, c, v, effects);
                        changed = true;
                    }
                }
                studio::EndCard();
                studio::Card("Player models");
                if (FlagControl("Enabled", effects.materialEnabled)) {
                    effects.glowEnabled = effects.materialEnabled;
                    changed = true;
                }
                const bool split = effects.visibility == EffectVisibility::TwoColor;
                changed |= ColorControl(split ? "Visible" : "Color", effects.materialColor);
                if (split)
                    changed |= ColorControl("Hidden", effects.glowColor);
                if (effects.visibility != EffectVisibility::OccludedOnly) {
                    changed |= FlagControl("Soft glow", v.softGlow);
                    if (v.softGlow) {
                        changed |= ColorControl("Glow color", v.haloColor);
                        if (BeginForm("Halo")) {
                            changed |= FloatRow("Strength", v.glowStrength, 0, 1, "%.2f");
                            changed |= FloatRow("Pulse amount", v.playerStyle.haloPulse, 0, .9f, "%.2f");
                            changed |= FloatRow("Pulse speed (Hz)", v.playerStyle.pulseSpeed, .1f, 3, "%.1f");
                            ImGui::EndTable();
                        }
                    }
                }
                if (BeginForm("Model appearance")) {
                    if (info.cs2) {
                        int style = v.shadedFill ? 0 : 1;
                        if (ComboRow("Style", style, "Textured\0Flat\0")) {
                            v.shadedFill = style == 0;
                            changed = true;
                        }
                    }
                    changed |= FloatRow(split ? "Visible strength" : "Strength", effects.materialColor.a, 0, 1, "%.2f");
                    if (split)
                        changed |= FloatRow("Hidden strength", effects.glowColor.a, 0, 1, "%.2f");
                    ImGui::EndTable();
                }
                if (BeginForm("Mode")) {
                    int mode = static_cast<int>(effects.visibility);
                    if (ComboRow("Fill", mode, "Single color\0Behind walls\0Two colors\0")) {
                        if (mode == 2 && effects.visibility != EffectVisibility::TwoColor)
                            effects.glowColor = {.65f, .15f, .95f, .85f};
                        effects.visibility = static_cast<EffectVisibility>(mode);
                        changed = true;
                    }
                    if (!info.cs2)
                        changed |= FloatRow("Soft edge", effects.glowWidth, 1, 12, "%.1f");
                    ImGui::EndTable();
                }
                if (BeginForm("Tint finish")) {
                    changed |= FloatRow("Tint brightness", v.playerStyle.tintBrightness, 0, 2, "%.2f");
                    changed |= FloatRow("Tint saturation", v.playerStyle.tintSaturation, 0, 2, "%.2f");
                    ImGui::EndTable();
                }
                if (effects.visibility != EffectVisibility::TwoColor)
                    effects.glowColor = effects.materialColor;
                if (info.cs2 && effects.materialEnabled && effects.visibility != EffectVisibility::AlwaysVisible &&
                    info.effectsState.status != EffectsStatus::Ready &&
                    info.effectsState.status != EffectsStatus::NativeMaterialReady)
                    ImGui::TextWrapped("%s", EffectsStatusText(info.effectsState.status));
                studio::EndCard();
                ImGui::EndTabItem();
            }
            if (studio::Tab("Style")) {
                studio::Card("Boxes & labels");
                changed |= FlagControl("Corners", v.cornerBoxes);
                changed |= FlagControl("Fill", v.fillBoxes);
                if (FlagControl("Gradient fill", v.playerStyle.gradientFill)) {
                    if (v.playerStyle.gradientFill)
                        v.fillBoxes = 1;
                    changed = true;
                }
                if (v.playerStyle.gradientFill) {
                    changed |= ColorControl("Fill top", v.playerStyle.fillTop);
                    changed |= ColorControl("Fill bottom", v.playerStyle.fillBottom);
                }
                changed |= FlagControl("Health numbers", v.healthNumbers);
                changed |= FlagControl("Health colors", c.colorBoxesByHealth);
                if (BeginForm("Geometry")) {
                    int bar = static_cast<int>(c.healthBar), line = static_cast<int>(v.lineOrigin);
                    if (ComboRow("Health bar", bar, "Vertical\0Horizontal\0")) {
                        c.healthBar = static_cast<HealthBar>(bar);
                        changed = true;
                    }
                    if (ComboRow("Line origin", line, "Top\0Bottom\0Center\0")) {
                        v.lineOrigin = line;
                        changed = true;
                    }
                    changed |= FloatRow("Fill opacity", v.fillOpacity, 0, .5f, "%.2f");
                    changed |= FloatRow("Box width", c.boxThickness, .5f, 5, "%.1f");
                    changed |= FloatRow("Corner length", v.playerStyle.cornerLength, .08f, .5f, "%.2f");
                    changed |= FloatRow("Rounding", v.playerStyle.boxRounding, 0, 12, "%.0f");
                    changed |= FloatRow("Box glow", v.playerStyle.boxGlow, 0, 1, "%.2f");
                    changed |= FloatRow("Line width", c.lineThickness, .5f, 4, "%.1f");
                    changed |= FloatRow("Health width", c.barThickness, 2, 12);
                    ImGui::EndTable();
                }
                studio::EndCard();
                ImGui::EndTabItem();
            }
            if (studio::Tab("Colors")) {
                studio::Card("Players");
                changed |= ColorControl("Opponents", c.opponent);
                changed |= ColorControl("Teammates", c.teammate);
                changed |= ColorControl("Neutral", c.neutral);
                changed |= ColorControl("Text", c.text);
                changed |= ColorControl("Shadow", c.outline);
                changed |= ColorControl("Bar background", c.barBackground);
                studio::EndCard();
                studio::Card("Health");
                changed |= ColorControl("High", c.healthy);
                changed |= ColorControl("Medium", c.medium);
                changed |= ColorControl("Low", c.low);
                if (BeginForm("Thresholds")) {
                    changed |= FloatRow("Low", c.lowHealthThreshold, 0, c.mediumHealthThreshold - .01f, "%.2f");
                    changed |= FloatRow("Medium", c.mediumHealthThreshold, c.lowHealthThreshold + .01f, 1, "%.2f");
                    ImGui::EndTable();
                }
                studio::EndCard();
                ImGui::EndTabItem();
            }
            if (studio::Tab("Awareness")) {
                studio::Card("Direction arrows");
                changed |= FlagControl("Enabled", v.awarenessEnabled);
                changed |= FlagControl("Offscreen only", v.awarenessOffscreen);
                changed |= ColorControl("Color", v.awarenessColor);
                if (BeginForm("Arrows")) {
                    int teams = static_cast<int>(v.awarenessTeams);
                    if (ComboRow("Players", teams, "All\0Opponents\0Teammates\0T\0CT\0")) {
                        v.awarenessTeams = static_cast<std::uint32_t>(teams);
                        changed = true;
                    }
                    changed |= FloatRow("Radius", v.awarenessRadius, 50, 400);
                    changed |= FloatRow("Size", v.awarenessSize, 6, 30);
                    ImGui::EndTable();
                }
                studio::EndCard();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    } else if (section == 1) {
        studio::Card("Tracking");
        changed |= FlagControl("Enabled", tracking.enabled);
        if (const auto *weapon = FindWeaponIcon(info.trackingWeapon))
            ImGui::TextDisabled("Active: %s / %s", weapon->name,
                                awareness::tracking::GroupNames[static_cast<unsigned>(
                                    awareness::tracking::WeaponGroup(info.trackingWeapon))]);
        else
            ImGui::TextDisabled("Active: no firearm");
        auto &profiles = v.trackingProfiles;
        if (ImGui::BeginTable("Weapon groups", 6, ImGuiTableFlags_SizingStretchSame)) {
            for (unsigned i = 0; i < 6; ++i) {
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(i));
                auto at = ImGui::GetCursorScreenPos();
                DrawWeaponIcon(ImGui::GetWindowDrawList(), atlas, awareness::tracking::GroupIcons[i], at, {36, 16},
                               profiles.selection == i ? studio::Packed(studio::Accent) : studio::Packed(studio::Text));
                ImGui::Dummy({36, 20});
                const bool selected = profiles.selection == i;
                if (selected)
                    ImGui::PushStyleColor(ImGuiCol_Button, studio::Alpha(studio::Accent, .25f));
                if (studio::Button(awareness::tracking::GroupNames[i], {-FLT_MIN, 28 * v.uiScale})) {
                    profiles.selection = i;
                    profiles.weaponSelection = 0;
                    changed = true;
                }
                if (selected)
                    ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (profiles.selection && BeginForm("Weapon choice")) {
            FormRow("Weapon");
            const auto *chosen = FindWeaponIcon(profiles.weaponSelection);
            if (ImGui::BeginCombo("##trackingWeapon", chosen ? chosen->name : "All in this group")) {
                if (ImGui::Selectable("All in this group", !profiles.weaponSelection)) {
                    profiles.weaponSelection = 0;
                    changed = true;
                }
                for (const auto &weapon : WeaponIcons)
                    if (static_cast<unsigned>(awareness::tracking::WeaponGroup(weapon.id)) == profiles.selection &&
                        ImGui::Selectable(weapon.name, profiles.weaponSelection == weapon.id)) {
                        profiles.weaponSelection = weapon.id;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            ImGui::PopID();
            ImGui::EndTable();
        }
        auto &parent = profiles.groups[profiles.selection];
        auto &group = profiles.weaponSelection
                          ? profiles.weapons[awareness::tracking::WeaponSlot(profiles.weaponSelection)]
                          : parent;
        if (profiles.selection) {
            if (ImGui::BeginTable("Profile toggles", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                changed |= FlagControl("Customize this profile", group.custom);
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(!group.custom);
                changed |= FlagControl("Enable this profile", group.enabled);
                ImGui::EndDisabled();
                ImGui::EndTable();
            }
            if (!group.custom)
                ImGui::TextDisabled("%s", profiles.weaponSelection && parent.custom ? "Uses this weapon group."
                                                                                    : "Uses Default settings.");
        }
        auto &effectiveGroup = profiles.weaponSelection && !group.custom ? parent : group;
        float &fov = profiles.selection && effectiveGroup.custom ? effectiveGroup.fov : tracking.fovDegrees;
        float &speed = profiles.selection && effectiveGroup.custom ? effectiveGroup.speed : tracking.interpolationSpeed;
        ImGui::BeginDisabled(profiles.selection && !group.custom);
        if (BeginForm("Group tuning")) {
            changed |= FloatRow("FOV", fov, 1, 90, "%.1f deg");
            float smooth = awareness::tracking::SmoothMilliseconds(speed);
            if (FloatRow("Smoothness", smooth, 0, 500, smooth <= 5 ? "Instant" : "%.0f ms")) {
                speed = awareness::tracking::ResponseSpeed(smooth);
                changed = true;
            }
            ImGui::EndTable();
        }
        ImGui::EndDisabled();
        if (profiles.selection && studio::Button("Copy Default")) {
            group.fov = tracking.fovDegrees;
            group.speed = tracking.interpolationSpeed;
            group.enabled = 1;
            group.custom = 1;
            changed = true;
        }
        studio::EndCard();
        if (ImGui::CollapsingHeader("Activation & target", ImGuiTreeNodeFlags_DefaultOpen) && BeginForm("Tracking")) {
            FormRow("Bind");
            if (info.waitingForBind)
                ImGui::TextUnformatted("Waiting for input...");
            else {
                ImGui::TextUnformatted(binding::Name(tracking.hotkey).c_str());
                ImGui::SameLine();
                if (studio::Button("Set bind"))
                    actions.beginBind = true;
            }
            ImGui::PopID();
            FormRow("Target");
            if (ImGui::Combo("##TargetBone", &g_ActiveTargetBone, TargetBoneNames, 4)) {
                v.activeTargetBone = g_ActiveTargetBone;
                changed = true;
            }
            ImGui::PopID();
            int teams = static_cast<int>(v.trackingTeams);
            if (ComboRow("Teams", teams, "Opponents\0All teams\0")) {
                v.trackingTeams = static_cast<std::uint32_t>(teams);
                changed = true;
            }
            ImGui::EndTable();
        }
        if (tracking.hotkey >= binding::WheelUp)
            ImGui::TextDisabled("Scroll to follow briefly.");
        if (tracking.hotkey == VK_INSERT)
            ImGui::TextDisabled("Open the menu with Ctrl + Insert.");
        if (tracking.hotkey == c.toggleKey || tracking.hotkey == c.alternateToggleKey)
            ImGui::TextDisabled("Toggle drawing with Ctrl + %s.", binding::Name(tracking.hotkey).c_str());
        studio::Card("FOV circle");
        changed |= FlagControl("Show circle", effects.fovCircle);
        changed |= ColorControl("Color", effects.fovColor);
        if (BeginForm("Circle")) {
            changed |= FloatRow("Width", effects.fovThickness, .5f, 4, "%.1f");
            ImGui::EndTable();
        }
        studio::EndCard();

    } else if (section == 2) {
        studio::Card("Colors");
        if (BeginForm("Theme")) {
            int theme = v.theme;
            if (ComboRow("Theme", theme, "Red\0Blue\0Purple\0Graphite\0Custom\0")) {
                v.theme = theme;
                changed = true;
            }
            changed |= FloatRow("Window opacity", v.menuOpacity, .25f, 1, "%.2f");
            changed |= FloatRow("Panel opacity", v.panelOpacity, 0, 1, "%.2f");
            ImGui::EndTable();
        }
        if (v.theme == 4)
            changed |= ColorControl("Accent", v.accent);
        studio::EndCard();

    } else if (section == 3) {
        studio::Card("Menu & text");
        if (BeginForm("Fonts")) {
            changed |= FloatRow("Menu size", v.uiScale, .75f, 1.5f, "%.2f");
            changed |= FloatRow("HUD text size", c.fontPixels, 10, 30);
            if (FontRow("HUD font", v.hudFont)) {
                c.fontPath[0] = 0;
                changed = true;
            }
            changed |= FontRow("Menu font", v.menuFont);
            ImGui::EndTable();
        }
        changed |= FlagControl("Menu animation", v.menuAnimations);
        changed |= FlagControl("Status HUD", v.statusHud);
        studio::EndCard();

    } else if (section == 8) {

        studio::Card("Paths");
        if (ImGui::BeginTable("Path toggles", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            changed |= FlagControl("Throw preview", v.grenadePrediction);
            ImGui::TableNextColumn();
            changed |= FlagControl("Flight trails", v.grenadeTrails);
            ImGui::TableNextColumn();
            changed |= FlagControl("Bullet tracers", v.bulletTracers);
            ImGui::EndTable();
        }
        studio::EndCard();
        if (ImGui::BeginTabBar("Path appearance")) {
            if (studio::Tab("Preview")) {
                studio::Card("Throw preview");
                changed |= UtilityStyle(v.paths, true);
                studio::EndCard();
                ImGui::EndTabItem();
            }
            if (studio::Tab("Trails")) {
                studio::Card("Flight trails");
                changed |= UtilityStyle(v.paths, false);
                studio::EndCard();
                ImGui::EndTabItem();
            }
            if (studio::Tab("Bullets")) {
                studio::Card("Bullet tracers");
                changed |= FlagControl("Glow", v.paths.shotGlow);
                if (BeginForm("Tracers")) {
                    int filter = static_cast<int>(v.tracerTeams);
                    if (ComboRow("Players", filter,
                                 "Local player\0All players\0Opponents\0Teammates\0T team\0CT team\0")) {
                        v.tracerTeams = filter;
                        changed = true;
                    }
                    changed |= FloatRow("Lifetime", v.paths.shotLifetime, .1f, 5.f, "%.2f s");
                    changed |= FloatRow("Width", v.paths.shotWidth, 1, 5, "%.1f");
                    if (v.paths.shotGlow)
                        changed |= FloatRow("Glow strength", v.paths.shotStrength, 0, 3, "%.1f");
                    ImGui::EndTable();
                }
                changed |= ColorControl("Start", v.paths.shotStart);
                changed |= ColorControl("End", v.paths.shotEnd);
                changed |= ColorControl("Core", v.paths.shotCore);
                studio::EndCard();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    } else if (section == 9) {
        studio::Card("Runtime diagnostics");
        ImGui::Text("Vortex %s", info.appVersion ? info.appVersion : "unknown");
        static const auto profilePath = vortex::Utf8(awareness::SettingsPath());
        ImGui::TextWrapped("Profile: %s", profilePath.c_str());
        {
            if (info.settingsMessage)
                ImGui::TextWrapped("%s", info.settingsMessage);
            ImGui::TextWrapped("%s", info.message);
            ImGui::Text("Frames %.0f   Read %.2f   Draw %.2f", info.fps, info.readMs, info.renderMs);
            ImGui::Text("Players %u   Skipped %u", info.pawns, info.failedReads);
            ImGui::TextWrapped("%s", EffectsStatusText(info.effectsState.status));
            ImGui::TextWrapped("%s", TrackingStatusText(info.trackingState.status));
            if (info.cs2) {
                ImGui::Text("Build %u / %u", info.gameBuild, info.offsetBuild);
                ImGui::Text("Utility %u   Infernos %u   Burning cells %u", info.utilityEntities, info.fireEntities,
                            info.burningCells);
                ImGui::Text("Fire read failures %u   Draw areas %u", info.fireReadFailures, info.areaCount);
                ImGui::Text("Hooks: view %s / events %s / tracers %s / punch %s", info.viewConnected ? "on" : "off",
                            info.eventsConnected ? "on" : "off", info.tracerConnected ? "on" : "off",
                            info.punchConnected ? "on" : "off");
                ImGui::Text("Direct shots %llu [%s]   Particle effects %llu [%s]", info.bulletCallbacks,
                            info.bulletConnected ? "on" : "off", info.particleCallbacks,
                            info.particleConnected ? "on" : "off");
                ImGui::Text("Shots %llu   Fire events %llu   Impacts %llu", info.fireSamples, info.fireEvents,
                            info.impactEvents);
                ImGui::Text("Effects %llu   Accepted %llu   Rejected %llu", info.tracerCallbacks, info.acceptedTracers,
                            info.rejectedTracers);
                ImGui::Text("Projected %u   Outside view %u", info.visibleTracers, info.clippedTracers);
                ImGui::Text("Weapon %u   Burst %u   Punch %.3f / %.3f", info.recoilWeapon, info.recoilShots,
                            info.recoilPunch.x, info.recoilPunch.y);
                ImGui::Text("Failed recoil reads %llu", info.recoilReadFailures);
                ImGui::Text("Recoil %s   Updates %llu", info.recoilInput ? "ready" : "waiting", info.recoilWrites);
                if (FAILED(info.recoilResult))
                    ImGui::Text("Recoil write %08X", static_cast<unsigned>(info.recoilResult));
                if (studio::Button("Rescan"))
                    actions.rescan = true;
            }
            int timeout = static_cast<int>((std::min)(5000u, c.staleFrameMilliseconds));
            if (ImGui::SliderInt("Data timeout", &timeout, 0, 5000)) {
                c.staleFrameMilliseconds = timeout;
                changed = true;
            }
        }
        studio::EndCard();

    } else {
        changed |= CombatPanel(section, v.combat, actions, info.hitSoundStatus);
        if (section == 5) {
            studio::Card("Kill sound");
            changed |= FlagControl("Enabled", v.killSoundEnabled);
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |=
                ImGui::InputTextWithHint("##KillSoundPath", "Built-in sound", v.killSoundPath, sizeof(v.killSoundPath));
            if (studio::Button("Browse##Sound"))
                actions.browseSound = true;
            ImGui::SameLine();
            if (studio::Button("Test sound"))
                actions.testSound = true;
            ImGui::SameLine();
            if (studio::Button("Built-in")) {
                v.killSoundPath[0] = 0;
                changed = true;
            }
            if (BeginForm("Sound")) {
                float volume = v.killSoundVolume * 100;
                if (FloatRow("Volume", volume, 0, 100)) {
                    v.killSoundVolume = volume / 100;
                    changed = true;
                }
                ImGui::EndTable();
            }
            if (info.soundStatus && *info.soundStatus)
                ImGui::TextWrapped("%s", info.soundStatus);
            studio::EndCard();
        }
    }
    return changed;
}

inline bool PresetPage(awareness::Configuration &c, awareness::VisualOptions &v, awareness::TrackingConfiguration &t,
                       awareness::EffectsConfiguration &e) {
    using namespace awareness;
    static ProfileState previous;
    static bool canUndo{};
    bool changed{};
    ImGui::TextWrapped("A consistent violet and white palette, without background images. Choose a complete profile or "
                       "restyle your current settings.");
    const auto remember = [&] {
        previous = {c, v, t, e};
        canUndo = true;
        changed = true;
    };
    if (studio::Button("Apply palette only")) {
        remember();
        Harmonize(c, v, e);
    }
    if (canUndo) {
        ImGui::SameLine();
        if (studio::Button("Undo last preset")) {
            c = previous.config;
            v = previous.visual;
            t = previous.tracking;
            e = previous.effects;
            canUndo = false;
            changed = true;
        }
    }
    ImGui::Dummy({0, 12 * v.uiScale});
    const char *names[]{"Signature", "Focus", "Broadcast"};
    const char *labels[]{"YOUR SETUP, REFINED", "MINIMAL VISUALS", "OBSERVER VIEW"};
    const char *descriptions[]{
        "White model fill, violet tracers and utility paths. Recoil and compact hit feedback.",
        "Opponent health, local tracers and throw preview. Model fill, halos and area volumes off.",
        "Both teams, two-tone models, distances and damage. Target tracking and recoil off."};
    const bool wide = ImGui::GetContentRegionAvail().x >= 610 * v.uiScale;
    if (ImGui::BeginTable("Preset cards", wide ? 3 : 1, ImGuiTableFlags_SizingStretchSame)) {
        for (int i = 0; i < 3; ++i) {
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            studio::Card(names[i], labels[i]);
            ImGui::TextWrapped("%s", descriptions[i]);
            if (wide)
                ImGui::SetCursorPosY(172 * v.uiScale);
            if (studio::Button("Apply profile", {-FLT_MIN, 34 * v.uiScale})) {
                remember();
                ApplyPreset(static_cast<Preset>(i), c, v, t, e);
            }
            studio::EndCard();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Dummy({0, 12 * v.uiScale});
    studio::Card("Fine-tune every module");
    ImGui::TextWrapped("Players handles labels and model effects. Assists keeps input controls together. "
                       "Trajectories, World and Feedback each have their own space.");
    ImGui::TextDisabled("INSERT opens the menu. Changes save automatically when Auto-save is on.");
    studio::EndCard();
    return changed;
}
inline bool DrawOverlayPanel(awareness::Configuration &c, awareness::VisualOptions &v, const PanelInformation &info,
                             bool &open, PanelActions &actions, ImTextureRef atlas,
                             awareness::TrackingConfiguration &tracking, awareness::EffectsConfiguration &effects,
                             const PanelMedia &media, bool interactive = true) {
    using namespace awareness;
    bool changed{};
    const float s = v.uiScale;
    studio::Animations = v.menuAnimations != 0;
    const auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({20, 20}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({std::min(1100.f * s, display.x - 40), std::min(760.f * s, display.y - 40)},
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({std::min(720.f, display.x - 16), std::min(480.f, display.y - 16)}, display);
    if (ImGui::Begin("Observer", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoScrollbar |
                         (interactive ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoInputs))) {
        const auto menuAt = ImGui::GetWindowPos(), menuSize = ImGui::GetWindowSize();
        actions.menuBounds = {menuAt.x, menuAt.y, menuAt.x + menuSize.x, menuAt.y + menuSize.y};
        auto *storage = ImGui::GetStateStorage();
        const auto key = ImGui::GetID("WorkspacePage");
        int page = std::clamp(storage->GetInt(key, 0), 0, 6);
        const auto pos = ImGui::GetWindowPos();
        auto *draw = ImGui::GetWindowDrawList();
        const bool compact = ImGui::GetWindowWidth() < 840 * s;
        const float side = (compact ? 154.f : 176.f) * s;
        draw->AddRectFilled(pos, {pos.x + side + 20 * s, pos.y + ImGui::GetWindowHeight()},
                            studio::Packed({.059f, .059f, .071f, 1}), 14 * s, ImDrawFlags_RoundCornersLeft);
        draw->AddLine({pos.x + side + 20 * s, pos.y + 20 * s},
                      {pos.x + side + 20 * s, pos.y + ImGui::GetWindowHeight() - 20 * s},
                      studio::Packed(studio::Border));
        ImGui::BeginChild("Sidebar", {side, 0}, 0, ImGuiWindowFlags_NoScrollbar);
        auto p = ImGui::GetCursorScreenPos();
        draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(p, {p.x + 34 * s, p.y + 34 * s}, studio::Packed(studio::Alpha(studio::Accent, .16f)),
                            10 * s);
        draw->AddLine({p.x + 9 * s, p.y + 10 * s}, {p.x + 17 * s, p.y + 25 * s}, studio::Packed(studio::Accent),
                      2.5f * s);
        draw->AddLine({p.x + 17 * s, p.y + 25 * s}, {p.x + 26 * s, p.y + 9 * s}, studio::Packed(studio::Text),
                      2.5f * s);
        ImGui::Dummy({34 * s, 34 * s});
        ImGui::SameLine(0, 12 * s);
        ImGui::BeginGroup();
        ImGui::PushFont(nullptr, 21);
        ImGui::TextUnformatted("Vortex");
        ImGui::PopFont();
        ImGui::TextDisabled("COUNTER-STRIKE 2");
        ImGui::EndGroup();
        ImGui::Dummy({0, 24 * s});
        ImGui::TextDisabled("CONTROLS");
        const char *pages[]{"Overview", "Players", "Assists", "Trajectories", "World", "Feedback", "Settings"};
        const int icons[]{3, 0, 1, 6, 4, 5, 3};
        for (int i = 0; i < 7; ++i)
            if (studio::Nav(pages[i], icons[i], page == i))
                page = i;
        const bool fullFooter = ImGui::GetContentRegionAvail().y >= 110 * s;
        ImGui::SetCursorPosY(
            std::max(ImGui::GetCursorPosY() + 4 * s, ImGui::GetWindowHeight() - (fullFooter ? 108 : 66) * s));
        if (fullFooter) {
            ImGui::TextDisabled("INSERT  Menu");
            ImGui::TextDisabled("HOME   Overlay");
            ImGui::Dummy({0, 5 * s});
        }
        SteamCard(media, s);
        ImGui::EndChild();
        storage->SetInt(key, page);
        ImGui::SameLine(0, 28 * s);
        ImGui::BeginGroup();
        ImGui::TextColored(studio::Accent, "VORTEX  /  %s", pages[page]);
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 80 * s);
        if (studio::Button("Close", {58 * s, 25 * s}))
            open = false;
        const char *titles[]{"Make it yours.",     "Player visuals",      "Assists",         "Every path, in view.",
                             "World & atmosphere", "Feedback that fits.", "Your preferences"};
        const char *descriptions[]{"Complete profiles, ready to personalize.",
                                   "Player labels, highlights, and direction indicators.",
                                   "Tracking, shooting and movement assistance.",
                                   "Bullet tracers, grenade previews and flight trails.",
                                   "Scene contrast, utility areas and motion.",
                                   "Hits, sounds and objective information.",
                                   "Appearance, interface controls and live diagnostics."};
        ImGui::PushFont(nullptr, 27);
        ImGui::TextUnformatted(titles[page]);
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, studio::Muted);
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 24 * s);
        ImGui::TextWrapped("%s", descriptions[page]);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Dummy({0, 10 * s});
        ImGui::BeginChild("Page", {0, -74 * s});
        const bool wide = (page == 1) && ImGui::GetContentRegionAvail().x >= 650 * s;
        if (ImGui::BeginTable("Workspace", wide ? 2 : 1, ImGuiTableFlags_SizingStretchProp)) {
            if (wide) {
                ImGui::TableSetupColumn("Controls", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, 220 * s);
            }
            ImGui::TableNextColumn();
            auto section = [&](int id) {
                changed |= DrawFeatureSection(id, c, v, info, actions, tracking, effects, atlas);
            };
            if (page == 0 && ImGui::BeginTabBar("OverviewSections")) {
                if (studio::Tab("Presets")) {
                    changed |= PresetPage(c, v, tracking, effects);
                    ImGui::EndTabItem();
                }
                if (studio::Tab("My profiles")) {
                    if (info.profiles)
                        awareness::profiles::DrawProfilePanel(*info.profiles, actions.profile);
                    else
                        ImGui::TextDisabled("Profile library is starting.");
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            } else if (page == 1)
                section(0);
            else if (page == 2 && ImGui::BeginTabBar("AimSections")) {
                if (studio::Tab("Tracking")) {
                    section(1);
                    ImGui::EndTabItem();
                }
                if (studio::Tab("Assisted Shoot")) {
                    studio::Card("Assisted Shoot",
                                 "Hold your chosen key to fire when an enemy is under the crosshair.");
                    auto &o = v.assists;
                    changed |= FlagControl("Enable Assisted Shoot", o.shoot);
                    int selected =
                        static_cast<int>(std::find(assist::ShootKeys.begin(), assist::ShootKeys.end(), o.shootKey) -
                                         assist::ShootKeys.begin());
                    if (BeginForm("Shoot controls")) {
                        if (ComboRow("Hold key", selected, "Mouse 4\0Mouse 5\0Left Alt\0Left Shift\0Left Ctrl\0")) {
                            o.shootKey = assist::ShootKeys[std::clamp(selected, 0, 4)];
                            changed = true;
                        }
                        changed |= FloatRow("Reaction delay", o.delayMs, 0, 300, "%.0f ms");
                        changed |= FloatRow("Click interval", o.intervalMs, 40, 600, "%.0f ms");
                        changed |= FloatRow("Press duration", o.pressMs, 8, 40, "%.0f ms");
                        ImGui::EndTable();
                    }
                    changed |= FlagControl("Scoped only", o.scopeOnly);
                    ImGui::TextDisabled("%s  /  Clicks: %llu", assist::Name(info.assists.shoot), info.assists.shots);
                    ImGui::TextWrapped(
                        "Enemies only. Waits for a loaded, ready firearm. Manual firing takes priority.");
                    studio::EndCard();
                    ImGui::EndTabItem();
                }
                if (studio::Tab("Jumper")) {
                    studio::Card("Jumper", "Hold Space to jump again on the next detected landing.");
                    changed |= FlagControl("Enable Jumper", v.assists.jumper);
                    ImGui::TextWrapped("Uses your Space binding. Releases between jumps and pauses on ladders, in "
                                       "water or while frozen.");
                    ImGui::Separator();
                    ImGui::TextDisabled("%s  /  Jumps: %llu", assist::Name(info.assists.jump), info.assists.jumps);
                    studio::EndCard();
                    ImGui::EndTabItem();
                }
                if (studio::Tab("Strafer")) {
                    studio::Card("Strafer", "Hold A or D in the air for velocity-based steering.");
                    auto &o = v.assists;
                    changed |= FlagControl("Enable Strafer", o.strafer);
                    if (BeginForm("Strafe controls")) {
                        changed |= FloatRow("Turn limit (deg/s)", o.turnRate, 30, 720, "%.0f");
                        changed |= FloatRow("Minimum speed", o.minSpeed, 10, 400, "%.0f u/s");
                        ImGui::EndTable();
                    }
                    ImGui::TextWrapped("Temporarily removes W/S input while strafing, then restores keys you still "
                                       "hold. Pauses during aiming or firing.");
                    const bool tuningOpen = ImGui::CollapsingHeader("Server movement tuning");
                    awareness::testing::Record("Server movement tuning");
                    if (tuningOpen) {
                        if (BeginForm("Movement physics")) {
                            changed |= FloatRow("Air acceleration", o.airAcceleration, 1, 200, "%.1f");
                            changed |= FloatRow("Air speed cap", o.airSpeedCap, 1, 100, "%.1f");
                            changed |= FloatRow("Tick rate", o.tickRate, 30, 128, "%.0f");
                            ImGui::EndTable();
                        }
                        changed |= FlagControl("Use capped acceleration", o.cappedAcceleration);
                        ImGui::TextWrapped(
                            "Match these to the server. Different movement rules change the ideal angle.");
                    }
                    ImGui::TextDisabled("%s  /  Speed: %.0f u/s", assist::Name(info.assists.strafe),
                                        info.assists.speed);
                    studio::EndCard();
                    ImGui::EndTabItem();
                }
                if (studio::Tab("Latency")) {
                    studio::Card("Latency compensation",
                                 "Predict short motion from sample age and your added latency.");
                    auto &prediction = v.trackingProfiles;
                    changed |= FlagControl("Motion prediction", prediction.compensation);
                    if (BeginForm("Prediction tuning")) {
                        changed |= FloatRow("Added latency", prediction.latencyMs, 0, 200, "%.0f ms");
                        changed |= FloatRow("Prediction limit", prediction.maxPredictionMs, 0, 200, "%.0f ms");
                        changed |= FloatRow("Strength", prediction.strength, 0, 1, "%.2f");
                        ImGui::EndTable();
                    }
                    ImGui::TextWrapped(
                        "Use a small offset. Sudden turns and stops can make long predictions overshoot.");
                    ImGui::TextDisabled("Server ping and hit registration are unchanged.");
                    studio::EndCard();
                    ImGui::EndTabItem();
                }
                if (studio::Tab("Recoil")) {
                    section(7);
                    ImGui::EndTabItem();
                }
                if (studio::Tab("Camera")) {
                    studio::Card("Camera view");
                    changed |= FlagControl("Camera FOV", v.cameraFovEnabled);
                    if (BeginForm("Camera view")) {
                        changed |= FloatRow("View FOV", v.cameraFov, 60, 140);
                        ImGui::EndTable();
                    }
                    studio::EndCard();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            } else if (page == 3)
                section(8);
            else if (page == 4 && ImGui::BeginTabBar("WorldSections")) {
                if (studio::Tab("Scene")) {
                    section(4);
                    ImGui::EndTabItem();
                }
                if (studio::Tab("Sky")) {
                    studio::Card("Sky", "Color and brightness for the map's sky.");
                    if (studio::Button("Original sky")) {
                        v.sky = {};
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (studio::Button("Midnight")) {
                        v.sky.enabled = 1;
                        v.sky.tint = {0, 0, 0, 1};
                        v.sky.brightness = 1;
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (studio::Button("Twilight")) {
                        v.sky.enabled = 1;
                        v.sky.tint = {.4f, .35f, .7f, 1};
                        v.sky.brightness = .7f;
                        changed = true;
                    }
                    changed |= FlagControl("Custom sky", v.sky.enabled);
                    changed |= ColorControl("Sky tint", v.sky.tint, false);
                    if (BeginForm("Sky values")) {
                        changed |= FloatRow("Sky brightness", v.sky.brightness, 0, 3, "%.2f");
                        ImGui::EndTable();
                    }
                    ImGui::TextDisabled("%s", !info.cs2        ? "Available in game"
                                              : !v.sky.enabled ? "Original map sky"
                                              : info.skyFailed ? "Sky update unavailable"
                                              : info.skyCount  ? "Custom sky applied"
                                                               : "Waiting for map sky");
                    studio::EndCard();
                    ImGui::EndTabItem();
                }
                if (studio::Tab("Utility areas")) {
                    changed |= CombatPanel(8, v.combat, actions, info.hitSoundStatus);
                    ImGui::EndTabItem();
                }
                if (studio::Tab("Footsteps")) {
                    studio::Card("Footstep rings", "Expanding rings at received footstep positions.");
                    auto &o = v.worldVisuals;
                    changed |= FlagControl("Show footsteps", o.footsteps);
                    changed |= FlagControl("Double ring", o.footDouble);
                    if (BeginForm("Footstep style")) {
                        int teams = o.footTeams;
                        if (ComboRow("Players", teams, "Opponents\0Teammates\0All players\0")) {
                            o.footTeams = teams;
                            changed = true;
                        }
                        changed |= FloatRow("Duration", o.footDuration, .2f, 3, "%.2f s");
                        changed |= FloatRow("Radius", o.footRadius, .2f, 5, "%.1f m");
                        changed |= FloatRow("Width", o.footWidth, .5f, 4, "%.1f");
                        changed |= FloatRow("Range", o.footRange, 5, 150, "%.0f m");
                        ImGui::EndTable();
                    }
                    changed |= ColorControl("Color", o.footColor);
                    ImGui::TextDisabled("Received events: %llu", info.footstepEvents);
                    studio::EndCard();
                    ImGui::EndTabItem();
                }
                if (studio::Tab("Dropped weapons")) {
                    studio::Card("Dropped weapons", "Firearms on the ground. Held weapons are excluded.");
                    auto &o = v.worldVisuals;
                    changed |= FlagControl("Show dropped weapons", o.dropped);
                    changed |= FlagControl("3D bounds", o.dropBoxes);
                    changed |= FlagControl("Weapon icons", o.dropIcons);
                    changed |= FlagControl("Names", o.dropNames);
                    changed |= FlagControl("Distance", o.dropDistance);
                    if (BeginForm("Dropped style")) {
                        changed |= FloatRow("Range", o.dropRange, 5, 150, "%.0f m");
                        changed |= FloatRow("Width", o.dropWidth, .5f, 3, "%.1f");
                        ImGui::EndTable();
                    }
                    changed |= ColorControl("Color", o.dropColor);
                    ImGui::TextDisabled("On the ground: %u", info.droppedWeapons);
                    studio::EndCard();
                    ImGui::EndTabItem();
                }
                if (studio::Tab("Player replay")) {
                    section(6);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            } else if (page == 5)
                section(5);
            else if (page == 6 && ImGui::BeginTabBar("PreferenceSections")) {
                if (studio::Tab("Appearance")) {
                    section(2);
                    ImGui::EndTabItem();
                }
                if (studio::Tab("Interface")) {
                    studio::Card("Spectators", "Drag the floating card by its header while the menu is open.");
                    changed |= FlagControl("Spectator list", v.spectators);
                    if (BeginForm("Spectator appearance")) {
                        changed |= FloatRow("Size", v.spectatorScale, .75f, 1.5f, "%.2fx");
                        changed |= FloatRow("Background", v.spectatorOpacity, .2f, 1.f, "%.2f");
                        ImGui::EndTable();
                    }
                    if (studio::Button("Move panel"))
                        ImGui::SetWindowFocus("Spectators##floating");
                    ImGui::SameLine();
                    if (studio::Button("Reset position")) {
                        v.spectatorX = .98f;
                        v.spectatorY = .18f;
                        changed = true;
                    }
                    studio::EndCard();
                    studio::Card("Session badge", "Your Steam profile and connection, in the top-right corner.");
                    changed |= FlagControl("Show session badge", v.sessionBadge);
                    changed |= FlagControl("Light surface", v.badgeLight);
                    if (BeginForm("Badge appearance")) {
                        changed |= FloatRow("Scale", v.badgeScale, .75f, 1.5f, "%.2fx");
                        changed |= FloatRow("Opacity", v.badgeOpacity, .25f, 1.f, "%.2f");
                        ImGui::EndTable();
                    }
                    studio::EndCard();
                    section(3);
                    ImGui::EndTabItem();
                }
                if (studio::Tab("Session")) {
                    studio::Card("Session", "While Vortex is loaded");
                    changed |= FlagControl("Keep Windows awake", v.keepAwake);
                    ImGui::TextDisabled("%s", info.keepAwakeActive ? "Sleep prevention active"
                                                                   : "Prevents sleep without moving your player");
                    ImGui::Spacing();
                    changed |= FlagControl("Auto-accept matches", v.autoAccept);
                    ImGui::TextDisabled("%s", info.sessionStatus);
                    if (info.acceptedMatches)
                        ImGui::TextDisabled("Accepted this session: %u", info.acceptedMatches);
                    studio::EndCard();
                    ImGui::EndTabItem();
                }
                if (studio::Tab("Diagnostics")) {
                    section(9);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            if (wide) {
                ImGui::TableNextColumn();
                studio::Card("Live preview", "Drag to rotate");
                DrawPreview(c, v, atlas, media, actions, changed);
                studio::EndCard();
            } else if ((page == 1 || page == 2) && ImGui::CollapsingHeader("Live preview"))
                DrawPreview(c, v, atlas, media, actions, changed);
            ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::BeginDisabled(info.profileIoBusy);
        if (studio::Button("Save", {68 * s, 30 * s}))
            actions.save = true;
        ImGui::SameLine();
        if (studio::Button("Load", {68 * s, 30 * s}))
            actions.load = true;
        ImGui::EndDisabled();
        ImGui::SameLine();
        bool save = v.autoSave != 0;
        if (ImGui::Checkbox("Auto-save", &save)) {
            v.autoSave = save;
            changed = true;
        }
        awareness::testing::Record("Auto-save");
        if (ImGui::GetContentRegionAvail().x > 178 * s) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", info.ready ? "Connected" : "Preview / waiting");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, studio::Muted);
        ImGui::TextWrapped("%s",
                           info.settingsMessage && *info.settingsMessage
                               ? info.settingsMessage
                               : "Save and Load use your working profile. Keep snapshots in Overview > My profiles.");
        ImGui::PopStyleColor();

        ImGui::EndGroup();
    }
    ImGui::End();
    return changed;
}
