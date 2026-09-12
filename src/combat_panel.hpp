#pragma once
// Included after the shared controls in overlay_panel.hpp.
inline bool CombatPanel(int page, awareness::combat::Options &o, PanelActions &actions, const char *soundStatus) {
    bool changed{};
    if (page == 7) {
        studio::Card("Recoil");
        changed |= FlagControl("Enabled", o.recoil);
        studio::Caption("Weapon groups");
        if (ImGui::BeginTable("Recoil groups", 2, ImGuiTableFlags_SizingStretchSame)) {
            const char *groups[]{"Other weapons", "Pistols", "SMGs", "Rifles", "Snipers", "Heavy"};
            for (int i = 1; i < 6; ++i) {
                ImGui::TableNextColumn();
                changed |= FlagControl(groups[i], o.recoilGroups[i]);
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (BeginForm("Recoil profile")) {
            FormRow("Weapon");
            const auto index = o.recoilSelection;
            if (ImGui::BeginCombo("##weapon", index ? awareness::WeaponIcons[index - 1].name : "Default")) {
                if (ImGui::Selectable("Default", index == 0)) {
                    o.recoilSelection = 0;
                    changed = true;
                }
                for (std::size_t i = 0; i < std::size(awareness::WeaponIcons); ++i)
                    if (ImGui::Selectable(awareness::WeaponIcons[i].name, index == i + 1)) {
                        o.recoilSelection = static_cast<std::uint32_t>(i + 1);
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            ImGui::PopID();
            ImGui::EndTable();
        }
        auto &custom = o.weapons[o.recoilSelection];
        if (o.recoilSelection) {
            if (BeginForm("Weapon activation")) {
                int mode = static_cast<int>(custom.activation);
                if (ComboRow("Activation", mode, "Use weapon group\0Enabled\0Disabled\0")) {
                    custom.activation = mode;
                    changed = true;
                }
                ImGui::EndTable();
            }
            changed |= FlagControl("Use custom profile", custom.overrideDefault);
        }
        if (o.recoilSelection && !custom.overrideDefault)
            ImGui::TextDisabled("Default tuning");
        auto &p = o.recoilSelection && !custom.overrideDefault ? o.weapons[0] : custom;
        ImGui::BeginDisabled(o.recoilSelection && !custom.overrideDefault);
        if (BeginForm("Recoil tuning")) {
            changed |= FloatRow("Vertical", p.vertical, 0, 1.5f, "%.2f");
            changed |= FloatRow("Horizontal", p.horizontal, 0, 1.5f, "%.2f");
            float smoothing = p.smoothing * 1000.f;
            if (FloatRow("Smoothing", smoothing, 0, 300, "%.0f ms")) {
                p.smoothing = smoothing / 1000.f;
                changed = true;
            }
            float shot = static_cast<float>(p.startShot);
            if (FloatRow("Start shot", shot, 1, 10)) {
                p.startShot = static_cast<std::uint32_t>(std::lround(shot));
                changed = true;
            }
            int curve = p.curve;
            if (ComboRow("Response", curve, "Even\0Quick\0Soft\0")) {
                p.curve = curve;
                changed = true;
            }
            ImGui::EndTable();
        }
        ImGui::EndDisabled();
        studio::EndCard();
        studio::Card("Input");
        if (BeginForm("Input backend")) {
            int mode = static_cast<int>(o.recoilInput);
            if (ComboRow("Method", mode, "Windows mouse\0Native angles\0")) {
                o.recoilInput = mode;
                changed = true;
            }
            ImGui::EndTable();
        }
        studio::Tip("Pauses in menus or when the game loses focus.");
        if (o.recoilInput == 0 && ImGui::CollapsingHeader("Mouse calibration")) {
            ImGui::TextWrapped("Match these to m_yaw and m_pitch if you changed them in the game. Sensitivity and zoom "
                               "are read automatically.");
            if (BeginForm("Mouse scale")) {
                changed |= FloatRow("Yaw", o.mouseYaw, .001f, .1f, "%.3f");
                changed |= FloatRow("Pitch", o.mousePitch, .001f, .1f, "%.3f");
                ImGui::EndTable();
            }
        }
        studio::EndCard();
    } else if (page == 4) {
        studio::Card("Color grading", "Optional screen-wide adjustments. These do not change world materials.");
        if (studio::Button("Neutral")) {
            o.contrast = 1;
            o.worldDarkness = o.sceneExposure = o.sceneVignette = o.sceneTintStrength = 0;
            o.sceneContrast = o.sceneSaturation = o.sceneGamma = 1;
            o.sceneVibrance = o.sceneTemperature = o.sceneShadows = o.sceneHighlights = 0;
            changed = true;
        }
        ImGui::SameLine();
        if (studio::Button("Crisp")) {
            o.contrast = 1;
            o.worldDarkness = 0;
            o.sceneExposure = .08f;
            o.sceneContrast = 1.08f;
            o.sceneSaturation = 1;
            o.sceneGamma = 1.03f;
            o.sceneVibrance = .2f;
            o.sceneTemperature = 0;
            o.sceneShadows = .025f;
            o.sceneHighlights = -.05f;
            o.sceneVignette = o.sceneTintStrength = 0;
            changed = true;
        }
        ImGui::SameLine();
        if (studio::Button("Dusk")) {
            o.contrast = 1;
            o.worldDarkness = .16f;
            o.sceneExposure = -.1f;
            o.sceneContrast = 1.05f;
            o.sceneSaturation = .85f;
            o.sceneGamma = .95f;
            o.sceneVibrance = 0;
            o.sceneTemperature = -.35f;
            o.sceneShadows = .02f;
            o.sceneHighlights = -.1f;
            o.sceneVignette = .12f;
            o.sceneTintStrength = 0;
            changed = true;
        }
        changed |= FlagControl("Enabled", o.contrast);
        if (BeginForm("World contrast")) {
            changed |= FloatRow("Darkness", o.worldDarkness, 0, .85f, "%.2f");
            changed |= FloatRow("Exposure", o.sceneExposure, -1, 1, "%+.2f");
            changed |= FloatRow("Contrast", o.sceneContrast, .5f, 1.5f, "%.2f");
            changed |= FloatRow("Saturation", o.sceneSaturation, 0, 2, "%.2f");
            ImGui::EndTable();
        }
        const bool finishing = ImGui::CollapsingHeader("Finishing");
        awareness::testing::Record("Finishing");
        if (finishing) {
            if (BeginForm("Scene finishing")) {
                changed |= FloatRow("Gamma", o.sceneGamma, .5f, 2, "%.2f");
                changed |= FloatRow("Vibrance", o.sceneVibrance, -1, 1, "%+.2f");
                changed |= FloatRow("Temperature", o.sceneTemperature, -1, 1, "%+.2f");
                changed |= FloatRow("Shadows", o.sceneShadows, -.3f, .3f, "%+.2f");
                changed |= FloatRow("Highlights", o.sceneHighlights, -.5f, .5f, "%+.2f");
                changed |= FloatRow("Vignette", o.sceneVignette, 0, 1, "%.2f");
                changed |= FloatRow("Tint strength", o.sceneTintStrength, 0, 1, "%.2f");
                ImGui::EndTable();
            }
            changed |= ColorControl("Tint", o.sceneTint);
        }
        if (studio::Button("Reset scene")) {
            o.worldDarkness = o.sceneExposure = o.sceneVignette = o.sceneTintStrength = 0;
            o.sceneContrast = o.sceneSaturation = 1;
            o.sceneTint = {1, 1, 1, 1};
            o.sceneGamma = 1;
            o.sceneVibrance = o.sceneTemperature = o.sceneShadows = o.sceneHighlights = 0;
            changed = true;
        }
        studio::EndCard();
    } else if (page == 8) {
        studio::Card("Utility timers");
        changed |= FlagControl("Countdowns", o.utilityTimers);
        ImGui::BeginDisabled(!o.utilityTimers);
        changed |= FlagControl("Fire timer", o.timerFire);
        changed |= FlagControl("Smoke timer", o.timerSmoke);
        studio::Tip("Smoke uses an estimated standard lifetime (~). Fire follows the engine lifetime. Timers work "
                    "without area fills.");
        if (BeginForm("Timer style")) {
            changed |= FloatRow("Size", o.timerScale, .75f, 1.5f, "%.2f");
            changed |= FloatRow("Range", o.timerRange, 5, 150, "%.0f m");
            ImGui::EndTable();
        }
        ImGui::EndDisabled();
        studio::EndCard();
        studio::Card("Utility areas");
        changed |= FlagControl("Show areas", o.areas);
        changed |= FlagControl("Replace particles", o.hideParticles);
        changed |= FlagControl("Filled footprints", o.areaFill);
        changed |= FlagControl("Soft border", o.areaGlow);
        if (BeginForm("Area stroke")) {
            changed |= FloatRow("Border width", o.areaOutline, 0, 4, "%.1f");
            ImGui::EndTable();
        }
        if (ImGui::BeginTable("Area colors", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            changed |= FlagControl("Fire", o.fireArea);
            ImGui::TableNextColumn();
            changed |= ColorControl("Fire color", o.fireColor);
            ImGui::TableNextColumn();
            changed |= FlagControl("Smoke", o.smokeArea);
            ImGui::TableNextColumn();
            changed |= ColorControl("Smoke color", o.smokeColor);
            ImGui::TableNextColumn();
            changed |= FlagControl("Explosions", o.blastArea);
            ImGui::TableNextColumn();
            changed |= ColorControl("Explosion color", o.blastColor);
            ImGui::EndTable();
        }
        studio::EndCard();
    } else if (page == 6) {
        studio::Card("Player replay", "A delayed pose that follows movement and fades when the player stops.");
        changed |= FlagControl("Show replay", o.ghosts);
        if (BeginForm("Replay style")) {
            changed |= FloatRow("Delay", o.ghostDuration, .05f, 2.f, "%.2f s");
            changed |= FloatRow("Opacity", o.ghostOpacity, .1f, 1.f, "%.2f");
            ImGui::EndTable();
        }
        changed |= ColorControl("Teammates", o.ghostFriend);
        changed |= ColorControl("Opponents", o.ghostEnemy);
        studio::EndCard();
    } else if (page == 5) {
        if (ImGui::BeginTabBar("Feedback tabs")) {
            if (studio::Tab("Hits")) {
                studio::Card("Hit feedback", "Markers stay at each hit location, then fade independently.");
                if (ImGui::BeginTable("Hit toggles", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    changed |= FlagControl("Hitmarker", o.hitMarker);
                    ImGui::TableNextColumn();
                    changed |= FlagControl("Damage numbers", o.damageNumbers);
                    ImGui::EndTable();
                }
                if (BeginForm("Hit style")) {
                    changed |= FloatRow("Marker size", o.markerSize, 3, 24);
                    changed |= FloatRow("Marker hold", o.markerHold, 0, 2, "%.2f s");
                    changed |= FloatRow("Marker fade", o.markerDuration, .1f, 2, "%.2f s");
                    changed |= FloatRow("Damage fade", o.damageDuration, .3f, 3, "%.1f");
                    ImGui::EndTable();
                }
                if (ImGui::BeginTable("Hit colors", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    changed |= ColorControl("Marker", o.markerColor);
                    ImGui::TableNextColumn();
                    changed |= ColorControl("Damage", o.damageColor);
                    ImGui::EndTable();
                }
                studio::EndCard();
                studio::Card("Hit sound");
                changed |= FlagControl("Enabled", o.hitSound);
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::InputTextWithHint("##HitSoundPath", "Built-in sound", o.hitSoundPath,
                                                    sizeof(o.hitSoundPath));
                if (studio::Button("Browse##hit"))
                    actions.browseHitSound = true;
                ImGui::SameLine();
                if (studio::Button("Test##hit"))
                    actions.testHitSound = true;
                ImGui::SameLine();
                if (studio::Button("Built-in##hit")) {
                    o.hitSoundPath[0] = 0;
                    changed = true;
                }
                if (BeginForm("Hit volume")) {
                    float volume = o.hitVolume * 100;
                    if (FloatRow("Volume", volume, 0, 100)) {
                        o.hitVolume = volume / 100;
                        changed = true;
                    }
                    ImGui::EndTable();
                }
                if (soundStatus && *soundStatus)
                    ImGui::TextWrapped("%s", soundStatus);
                studio::EndCard();
                ImGui::EndTabItem();
            }
            if (studio::Tab("Hit feed")) {
                studio::Card("Hit feed");
                changed |= FlagControl("Show recent hits", o.hitLog);
                ImGui::BeginDisabled(!o.hitLog);
                changed |= FlagControl("Panel background", o.hitLogBackground);
                if (BeginForm("Hit feed style")) {
                    changed |= FloatRow("Duration", o.hitLogDuration, 1, 10, "%.1f s");
                    float rows = static_cast<float>(o.hitLogRows);
                    if (FloatRow("Rows", rows, 1, 8, "%.0f")) {
                        o.hitLogRows = static_cast<std::uint32_t>(std::lround(rows));
                        changed = true;
                    }
                    changed |= FloatRow("Size", o.hitLogScale, .75f, 1.5f, "%.2f");
                    changed |= FloatRow("Horizontal position", o.hitLogX, 0, 1, "%.2f");
                    changed |= FloatRow("Vertical position", o.hitLogY, 0, 1, "%.2f");
                    ImGui::EndTable();
                }
                ImGui::EndDisabled();
                studio::EndCard();
                ImGui::EndTabItem();
            }
            if (studio::Tab("Objective")) {
                studio::Card("Bomb timer");
                changed |= FlagControl("Show timer", o.bombTimer);
                changed |= ColorControl("Normal", o.bombSafe);
                changed |= ColorControl("Below 20", o.bombWarning);
                changed |= ColorControl("Below 10", o.bombDanger);
                studio::EndCard();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    return changed;
}
