#pragma once
#include "settings.hpp"
namespace awareness {
enum class Preset { Signature, Focus, Broadcast };
inline constexpr Color VortexViolet{.62f, .46f, 1.f, 1.f};
inline constexpr Color VortexWhite{.96f, .97f, 1.f, 1.f};
struct ProfileState {
    Configuration config;
    VisualOptions visual;
    TrackingConfiguration tracking;
    EffectsConfiguration effects;
};
inline void Harmonize(Configuration &c, VisualOptions &v, EffectsConfiguration &e) noexcept {
    const auto purple = VortexViolet, white = VortexWhite;
    v.theme = 4;
    v.accent = purple;
    v.backgroundEnabled = 0;
    v.backgroundPath[0] = 0;
    v.backgroundOpacity = 0;
    v.menuOpacity = 1;
    v.panelOpacity = 1;
    v.labelBackground = 0;
    c.teammate = white;
    c.opponent = purple;
    c.neutral = white;
    c.healthy = white;
    c.medium = purple;
    c.low = purple;
    c.text = white;
    c.outline = {.015f, .015f, .018f, .8f};
    c.barBackground = {.035f, .035f, .04f, .7f};
    e.materialColor = white;
    e.glowColor = purple;
    e.fovColor = {purple.r, purple.g, purple.b, .5f};
    if (e.visibility != EffectVisibility::TwoColor)
        e.glowColor = e.materialColor;
    v.haloColor = purple;
    v.lineups.color = {purple.r, purple.g, purple.b, .9f};
    v.awarenessColor = white;
    auto &p = v.paths;
    p.trailHE = p.trailSmoke = p.trailFlash = p.trailFire = p.trailDecoy = purple;
    p.previewHE = p.previewSmoke = p.previewFlash = p.previewFire = p.previewDecoy = white;
    p.shotStart = purple;
    p.shotEnd = white;
    p.shotCore = white;
    auto &o = v.combat;
    o.markerColor = white;
    o.damageColor = purple;
    o.bombSafe = white;
    o.bombWarning = purple;
    o.bombDanger = purple;
    o.ghostFriend = white;
    o.ghostEnemy = purple;
    o.fireColor = {purple.r, purple.g, purple.b, .38f};
    o.smokeColor = {white.r, white.g, white.b, .14f};
    o.blastColor = {purple.r, purple.g, purple.b, .22f};
}
inline void ApplyPreset(Preset preset, Configuration &c, VisualOptions &v, TrackingConfiguration &t,
                        EffectsConfiguration &e) noexcept {
    // Keep personal bindings, fonts, sound files and coordinate units.
    Harmonize(c, v, e);
    const auto shootKey = v.assists.shootKey;
    v.assists = {};
    v.assists.shootKey = shootKey;
    v.combat.recoilGroups = {0, 0, 1, 1, 0, 0};
    v.trackingProfiles = {};
    v.worldVisuals = {};
    c.enabled = 1;
    c.boxes = c.lines = c.distances = c.colorBoxesByHealth = 0;
    c.names = c.healthBars = 1;
    c.healthBar = HealthBar::Horizontal;
    c.opacity = .85f;
    c.boxThickness = 1.25f;
    c.lineThickness = 1;
    c.barThickness = 3;
    c.fontPixels = 15;
    c.teamFilter = TeamFilter::All;
    c.fadeStartMeters = 160;
    c.maxDistanceMeters = 300;
    c.staleFrameMilliseconds = 250;
    v.cornerBoxes = 1;
    v.fillBoxes = 0;
    v.fillOpacity = .08f;
    v.healthNumbers = 0;
    v.weaponIcons = 1;
    v.previewRotate = 0;
    v.awarenessEnabled = 0;
    v.awarenessOffscreen = 1;
    v.awarenessTeams = 1;
    v.awarenessRadius = 100;
    v.awarenessSize = 12;
    v.grenadePrediction = v.grenadeTrails = v.bulletTracers = 1;
    v.tracerTeams = 1;
    v.cameraVisuals = {};
    v.weather = {};
    v.scoreboard = {};
    // Personal loadout choices persist when applying visual presets.
    v.scene = {};
    v.lineups.enabled = 0;
    v.cameraFovEnabled = 0;
    v.cameraFov = 100;
    v.softGlow = 0;
    v.shadedFill = 1;
    v.glowStrength = .35f;
    v.statusHud = 0;
    v.paths.trailWidth = 1.35f;
    v.paths.previewWidth = 1.35f;
    v.paths.shotWidth = 1.15f;
    v.paths.shotLifetime = .5f;
    v.paths.trailGlow = v.paths.shotGlow = 0;
    v.paths.previewGlow = 0;
    v.paths.trailStrength = .55f;
    v.paths.shotStrength = .7f;
    v.paths.previewStrength = .4f;
    auto &o = v.combat;
    o.recoil = o.bombTimer = o.hitMarker = o.areas = 1;
    o.recoilSelection = 0;
    o.recoilInput = 0;
    for (auto &weapon : o.weapons)
        weapon = {0, 1, 0, 1, 1, .008f};
    o.damageNumbers = o.ghosts = o.contrast = 0;
    o.hideParticles = 0;
    o.ghostDirection = 0;
    o.ghostDuration = .18f;
    o.ghostOpacity = .32f;
    o.fireArea = o.smokeArea = o.blastArea = 1;
    o.areaFill = 1;
    o.areaGlow = 0;
    o.areaOutline = 1.5f;
    o.markerSize = 7;
    o.markerDuration = .45f;
    o.markerHold = .25f;
    o.damageDuration = 1;
    o.worldDarkness = 0;
    o.sceneContrast = o.sceneSaturation = 1;
    o.sceneExposure = o.sceneVignette = o.sceneTintStrength = 0;
    o.sceneTint = VortexWhite;
    o.sceneGamma = 1;
    o.sceneVibrance = o.sceneTemperature = o.sceneShadows = o.sceneHighlights = 0;
    v.playerStyle = {};
    v.sky = {};
    o.entityBrightness = 1.15f;
    o.entitySaturation = 1;
    t.enabled = 1;
    t.fovDegrees = 1;
    t.interpolationSpeed = 24;
    v.activeTargetBone = 0;
    v.trackingTeams = 1;
    e.materialEnabled = e.glowEnabled = 1;
    e.visibility = EffectVisibility::AlwaysVisible;
    e.materialColor = VortexWhite;
    e.materialColor.a = .35f;
    e.glowColor = VortexViolet;
    e.glowColor.a = .3f;
    e.glowWidth = 3;
    e.fovCircle = 0;
    e.fovThickness = 1;
    if (preset == Preset::Focus) {
        c.teamFilter = TeamFilter::OpponentsOnly;
        v.worldVisuals.footsteps = v.worldVisuals.dropped = 0;
        c.opacity = .95f;
        c.maxDistanceMeters = 180;
        c.fadeStartMeters = 100;
        c.names = 0;
        v.grenadeTrails = 0;
        v.tracerTeams = 0;
        v.paths.trailGlow = v.paths.shotGlow = 0;
        v.awarenessSize = 10;
        o.areas = o.hideParticles = 0;
        e.materialEnabled = e.glowEnabled = 0;
        e.fovCircle = 0;
    } else if (preset == Preset::Broadcast) {
        t.enabled = o.recoil = 0;
        v.worldVisuals.footTeams = 2;
        c.maxDistanceMeters = 400;
        c.fadeStartMeters = 240;
        c.distances = 1;
        o.damageNumbers = 1;
        o.hideParticles = 0;
        e.visibility = EffectVisibility::AlwaysVisible;
        e.materialColor = VortexWhite;
        e.materialColor.a = .45f;
        e.glowColor = VortexViolet;
        e.glowColor.a = .4f;
        e.fovCircle = 0;
    }
}
// These looks change only player appearance; input and world preferences stay personal.
inline void ApplyPlayerLook(int look, Configuration &c, VisualOptions &v, EffectsConfiguration &e) {
    v.playerStyle = {};
    c.colorBoxesByHealth = 0;
    e.materialEnabled = e.glowEnabled = 1;
    e.visibility = EffectVisibility::AlwaysVisible;
    v.shadedFill = 0;
    v.softGlow = 1;
    v.glowStrength = .55f;
    v.cornerBoxes = 1;
    v.fillBoxes = 0;
    if (look == 0) {
        e.materialColor = {.015f, .018f, .024f, .95f};
        e.glowColor = {.02f, .02f, .025f, .7f};
        v.haloColor = {1, 1, 1, 1};
        v.glowStrength = .8f;
        c.boxes = 0;
    } else if (look == 1) {
        e.materialColor = {.75f, .88f, 1, .22f};
        e.glowColor = {.4f, .55f, 1, .2f};
        v.haloColor = {.75f, .85f, 1, 1};
        v.glowStrength = .4f;
        c.boxes = 1;
        v.playerStyle.boxGlow = .15f;
        c.opponent = {.75f, .85f, 1, 1};
    } else {
        e.materialColor = {.4f, 1, .65f, .28f};
        e.glowColor = {.25f, .8f, .45f, .23f};
        v.haloColor = {.5f, 1, .65f, 1};
        c.opponent = {.5f, 1, .65f, 1};
        c.boxes = 1;
        v.fillBoxes = v.playerStyle.gradientFill = 1;
        v.fillOpacity = .25f;
        v.playerStyle.fillTop = {.3f, .9f, .5f, 0};
        v.playerStyle.fillBottom = {.4f, 1, .55f, 1};
        v.playerStyle.boxGlow = .25f;
        v.playerStyle.cornerLength = .22f;
    }
}
} // namespace awareness
