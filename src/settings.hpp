#pragma once
#include "configuration.hpp"
#include <awareness/TrackingApi.hpp>
#include <awareness/EffectsApi.hpp>
#include <string>
#include "trajectory_style.hpp"
#include "combat_features.hpp"
#include "tracking_profiles.hpp"
#include "world_visuals.hpp"
#include "assist_features.hpp"
#include "visual_styles.hpp"
#include "font_catalog.hpp"
namespace awareness {
struct VisualOptions {
    std::uint32_t cornerBoxes{1}, fillBoxes{}, healthNumbers{1}, labelBackground{}, statusHud{}, lineOrigin{1};
    float fillOpacity{.12f}, uiScale{1.f};
    std::uint32_t weaponIcons{1};
    std::uint32_t autoSave{1};
    std::uint32_t activeTargetBone{}; // Dropdown ordinal, see TargetBones.
    std::uint32_t trackingTeams{};    // camera::TargetTeams; independent of the HUD team filter.
    std::uint32_t theme{4}, backgroundEnabled{0}, backgroundFit{}, previewRotate{1};
    float menuOpacity{1}, panelOpacity{1}, backgroundOpacity{0};
    Color accent{.62f, .46f, 1.f, 1};
    char backgroundPath[2048]{}; // UTF-8; encoded in the INI.
    std::uint32_t awarenessEnabled{1}, awarenessOffscreen{1}, awarenessTeams{1};
    float awarenessRadius{150.f}, awarenessSize{14.f};
    Color awarenessColor{.95f, .95f, .95f, .9f};
    std::uint32_t killSoundEnabled{};
    float killSoundVolume{.7f};
    char killSoundPath[2048]{}; // Empty selects the built-in sound.
    std::uint32_t grenadePrediction{}, grenadeTrails{}, bulletTracers{}, tracerTeams{};
    std::uint32_t cameraFovEnabled{};
    float cameraFov{90};
    std::uint32_t softGlow{}, shadedFill{1};
    float glowStrength{.5f};
    Color haloColor{.22f, .82f, 1.f, 1.f};
    flight::PathStyle paths;
    combat::Options combat;
    tracking::Options trackingProfiles;
    worldvisuals::Options worldVisuals;
    assist::Options assists;
    styling::Player playerStyle;
    styling::Sky sky;
    std::uint32_t menuFont{}, hudFont{}, menuAnimations{1};
    std::uint32_t sessionBadge{1}, badgeLight{1};
    float badgeScale{1.f}, badgeOpacity{.94f};
    std::uint32_t spectators{1}, keepAwake{}, autoAccept{};
    float spectatorX{.98f}, spectatorY{.18f}, spectatorScale{1.f}, spectatorOpacity{.92f};
};
inline bool ValidVisualOptions(const VisualOptions &v) noexcept {
    if (!assist::Valid(v.assists) || !styling::Valid(v.playerStyle) || !styling::Valid(v.sky))
        return false;
    if (v.sessionBadge > 1 || v.badgeLight > 1 || !std::isfinite(v.badgeScale) || v.badgeScale < .75f ||
        v.badgeScale > 1.5f || !std::isfinite(v.badgeOpacity) || v.badgeOpacity < .25f || v.badgeOpacity > 1)
        return false;
    if (!tracking::Valid(v.trackingProfiles) || !worldvisuals::Valid(v.worldVisuals) || !combat::Valid(v.combat) ||
        !flight::ValidPathStyle(v.paths) || v.menuFont >= FontPresets.size() || v.hudFont >= FontPresets.size() ||
        !std::isfinite(v.spectatorX) || v.spectatorX < 0 || v.spectatorX > 1 || !std::isfinite(v.spectatorY) ||
        v.spectatorY < 0 || v.spectatorY > 1 || !std::isfinite(v.spectatorScale) || v.spectatorScale < .75f ||
        v.spectatorScale > 1.5f || !std::isfinite(v.spectatorOpacity) || v.spectatorOpacity < .2f ||
        v.spectatorOpacity > 1 || v.menuAnimations > 1 || v.spectators > 1 || v.keepAwake > 1 || v.autoAccept > 1)
        return false;
    const auto colorValid = [](Color c) {
        return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a) && c.r >= 0 &&
               c.r <= 1 && c.g >= 0 && c.g <= 1 && c.b >= 0 && c.b <= 1 && c.a >= 0 && c.a <= 1;
    };
    if (v.shadedFill > 1 || v.softGlow > 1 || !std::isfinite(v.glowStrength) || v.glowStrength < 0 ||
        v.glowStrength > 1 || !colorValid(v.haloColor))
        return false;
    return v.grenadePrediction <= 1 && v.grenadeTrails <= 1 && v.bulletTracers <= 1 && v.tracerTeams <= 5 &&
           v.cameraFovEnabled <= 1 && std::isfinite(v.cameraFov) && v.cameraFov >= 60 && v.cameraFov <= 140 &&
           camera::ValidTargetTeams(static_cast<camera::TargetTeams>(v.trackingTeams)) && v.activeTargetBone < 4 &&
           v.autoSave <= 1 && v.weaponIcons <= 1 && v.cornerBoxes <= 1 && v.fillBoxes <= 1 && v.healthNumbers <= 1 &&
           v.labelBackground <= 1 && v.statusHud <= 1 && v.lineOrigin <= 2 && std::isfinite(v.fillOpacity) &&
           v.fillOpacity >= 0.f && v.fillOpacity <= .5f && std::isfinite(v.uiScale) && v.uiScale >= .75f &&
           v.uiScale <= 1.5f && v.theme <= 4 && v.backgroundEnabled <= 1 && v.backgroundFit <= 1 &&
           v.previewRotate <= 1 && std::isfinite(v.menuOpacity) && v.menuOpacity >= .25f && v.menuOpacity <= 1 &&
           std::isfinite(v.panelOpacity) && v.panelOpacity >= 0 && v.panelOpacity <= 1 &&
           std::isfinite(v.backgroundOpacity) && v.backgroundOpacity >= 0 && v.backgroundOpacity <= 1 &&
           std::isfinite(v.accent.r) && v.accent.r >= 0 && v.accent.r <= 1 && std::isfinite(v.accent.g) &&
           v.accent.g >= 0 && v.accent.g <= 1 && std::isfinite(v.accent.b) && v.accent.b >= 0 && v.accent.b <= 1 &&
           std::isfinite(v.accent.a) && v.accent.a >= 0 && v.accent.a <= 1 &&
           std::memchr(v.backgroundPath, 0, sizeof(v.backgroundPath)) && v.awarenessEnabled <= 1 &&
           v.awarenessTeams <= 4 && v.awarenessOffscreen <= 1 && std::isfinite(v.awarenessRadius) &&
           v.awarenessRadius >= 50 && v.awarenessRadius <= 400 && std::isfinite(v.awarenessSize) &&
           v.awarenessSize >= 6 && v.awarenessSize <= 30 && std::isfinite(v.awarenessColor.r) &&
           v.awarenessColor.r >= 0 && v.awarenessColor.r <= 1 && std::isfinite(v.awarenessColor.g) &&
           v.awarenessColor.g >= 0 && v.awarenessColor.g <= 1 && std::isfinite(v.awarenessColor.b) &&
           v.awarenessColor.b >= 0 && v.awarenessColor.b <= 1 && std::isfinite(v.awarenessColor.a) &&
           v.awarenessColor.a >= 0 && v.awarenessColor.a <= 1 && v.killSoundEnabled <= 1 &&
           std::isfinite(v.killSoundVolume) && v.killSoundVolume >= 0 && v.killSoundVolume <= 1 &&
           std::memchr(v.killSoundPath, 0, sizeof(v.killSoundPath));
}
std::wstring SettingsPath();
inline EffectsConfiguration NativeHalo(const EffectsConfiguration &fill, const VisualOptions &visual,
                                       double now = 0) noexcept {
    EffectsConfiguration halo;
    halo.materialEnabled = halo.glowEnabled = (fill.materialEnabled || fill.glowEnabled) &&
                                              fill.visibility != EffectVisibility::OccludedOnly && visual.softGlow;
    halo.materialColor = halo.glowColor = visual.haloColor;
    halo.materialColor.a *= visual.glowStrength * styling::Pulse(visual.playerStyle, now);
    halo.glowColor.a = halo.materialColor.a;
    return halo;
}
std::wstring DefaultSettingsPath(const std::wstring &profilePath);
bool LoadStartupSettings(const std::wstring &, Configuration &, VisualOptions &, TrackingConfiguration * = nullptr,
                         EffectsConfiguration * = nullptr) noexcept;
bool SaveSettings(const std::wstring &, const Configuration &, const VisualOptions &,
                  const TrackingConfiguration & = {}, const EffectsConfiguration & = {},
                  bool replaceExisting = true) noexcept;
bool LoadSettings(const std::wstring &, Configuration &, VisualOptions &, TrackingConfiguration * = nullptr,
                  EffectsConfiguration * = nullptr) noexcept;
} // namespace awareness
