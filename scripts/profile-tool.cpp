#include "presets.hpp"
#include <filesystem>
#include <iostream>
HMODULE OverlayModule() noexcept {
    return GetModuleHandleW(nullptr);
}
void OverlayLog(const char *) noexcept {}
int wmain(int argc, wchar_t **argv) {
    if (argc != 4) {
        std::cerr << "Usage: profile-tool input.ini output.ini signature|focus|broadcast|palette|refine|migrate\n";
        return 2;
    }
    const std::wstring mode = argv[3];
    if (mode != L"signature" && mode != L"focus" && mode != L"broadcast" && mode != L"palette" && mode != L"refine" &&
        mode != L"migrate")
        return 2;
    awareness::Configuration c;
    awareness::VisualOptions v;
    awareness::TrackingConfiguration t;
    awareness::EffectsConfiguration e;
    if (!awareness::LoadSettings(std::filesystem::absolute(argv[1]).wstring(), c, v, &t, &e))
        return 3;
    if (mode == L"migrate") {
        // Loading supplies defaults for new fields; retain every existing choice.
    } else if (mode == L"refine") {
        // Target the visual defaults changed in 3.18; keep personal input settings,
        // bindings, sounds, fonts and chosen colors.
        v.combat.ghostDuration = .18f;
        v.combat.ghostOpacity = .32f;
        v.combat.fireColor.a = .38f;
        v.combat.areaFill = 1;
        v.combat.areaGlow = 0;
        v.combat.areaOutline = 1.5f;
        v.combat.worldDarkness = .12f;
        v.paths.shotLifetime = .75f;
        v.paths.shotWidth = 1.6f;
        v.shadedFill = 1;
        v.softGlow = 0;
        if (e.visibility != awareness::EffectVisibility::TwoColor)
            e.glowColor = v.accent;
        e.visibility = awareness::EffectVisibility::TwoColor;
        e.materialColor.a = .35f;
        e.glowColor.a = .3f;
    } else if (mode == L"palette") {
        awareness::Harmonize(c, v, e);
        v.paths.shotWidth = 2;
        v.paths.shotStrength = .7f;
        v.paths.trailWidth = 1.8f;
        v.paths.trailStrength = .55f;
        v.combat.markerSize = 7;
        v.combat.recoilInput = 0;
        // Start from calibrated 1:1 compensation after changing the input backend.
        for (auto &weapon : v.combat.weapons) {
            weapon.vertical = weapon.horizontal = 1;
            weapon.smoothing = .025f;
        }
        c.staleFrameMilliseconds = 250;
    } else {
        const auto kind = mode == L"focus"       ? awareness::Preset::Focus
                          : mode == L"broadcast" ? awareness::Preset::Broadcast
                                                 : awareness::Preset::Signature;
        awareness::ApplyPreset(kind, c, v, t, e);
        // Public presets contain no publisher-specific absolute file paths.
        v.killSoundPath[0] = v.combat.hitSoundPath[0] = c.fontPath[0] = 0;
    }
    const auto parent = std::filesystem::path(argv[2]).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);
    return awareness::SaveSettings(std::filesystem::absolute(argv[2]).wstring(), c, v, t, e) ? 0 : 4;
}
