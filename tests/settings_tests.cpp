#include "settings.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
HMODULE OverlayModule() noexcept {
    return GetModuleHandleW(nullptr);
}
int main(int argc, char **argv) {
    using namespace awareness;
    // Optional release-profile validation, also used after extracting a share copy.
    if (argc == 2) {
        Configuration c;
        VisualOptions v;
        TrackingConfiguration t;
        EffectsConfiguration e;
        if (!LoadStartupSettings(std::filesystem::path(argv[1]).wstring(), c, v, &t, &e))
            return 1;
        for (const auto *asset : {c.fontPath, v.backgroundPath, v.killSoundPath, v.combat.hitSoundPath}) {
            if (*asset && !std::filesystem::is_regular_file(
                              std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t *>(asset)))))
                return 1;
        }
        std::printf("Release profile and referenced files are valid.\n");
        return 0;
    }
    const auto path = SettingsPath() + L".test";
    Configuration config;
    VisualOptions visual;
    config.boxThickness = 3.25f;
    config.opacity = .67f;
    config.teamFilter = TeamFilter::TeammatesOnly;
    strcpy_s(config.fontPath, "C:\\fonts\\custom.ttf");
    visual.playerStyle = {1, .18f, 7, .6f, {.1f, .2f, .3f, .4f}, {.5f, .6f, .7f, .8f}, 1.3f, .5f, .4f, 2};
    visual.skeleton = {1, 0, 1, 1, 2.1f, .65f, {.2f, .4f, .6f, .8f}};
    visual.sky = {1, .4f, {.2f, .3f, .4f, 1}};
    visual.combat.sceneGamma = 1.3f;
    visual.combat.sceneVibrance = .3f;
    visual.combat.sceneTemperature = -.2f;
    visual.combat.sceneShadows = .1f;
    visual.combat.sceneHighlights = -.2f;
    visual.assists.shoot = visual.assists.jumper = visual.assists.strafer = visual.assists.scopeOnly = 1;
    visual.assists.shootKey = assist::Mouse5;
    visual.assists.shootMode = 2;
    visual.assists.preserveForward = 0;
    visual.assists.strafeStrength = .42f;
    visual.menuPage = 4;
    visual.cosmetics.enabled = 1;
    visual.cosmetics.weapons[0] = {1, 37, 999, .375f, 1, 1234, {}};
    strcpy_s(visual.cosmetics.weapons[0].name.data(), 64, "Vortex | Sapphire");
    visual.cosmetics.knife = {1, 507, {1, 415, 4, .12f, 0, 0, {}}};
    visual.cosmetics.glove = {1, 5030, {1, 10006, 22, .3f, 0, 0, {}}};
    visual.cosmetics.agents = {4613, 4619};
    visual.weather = {1, 2, 0};
    visual.scoreboard = {1, 0, 1, 0, 1.25f};
    visual.scene.enabled = 1;
    visual.scene.brightness = .65f;
    visual.scene.tint = {.2f, .4f, .6f, 1};
    visual.lineups.enabled = 1;
    visual.lineups.heldOnly = 0;
    visual.lineups.range = 75;
    visual.lineups.standTolerance = 12;
    visual.lineups.aimTolerance = 2;
    visual.lineups.color = {.3f, .5f, .7f, .8f};
    strcpy_s(visual.lineups.mapOverride, "de_mirage");
    visual.cameraVisuals.thirdPerson = 1;
    visual.cameraVisuals.whileScoped = 1;
    visual.cameraVisuals.removeRecoil = 1;
    visual.cameraVisuals.scopedFovEnabled = 1;
    visual.cameraVisuals.distance = 130;
    visual.cameraVisuals.shoulder = -25;
    visual.cameraVisuals.height = 15;
    visual.cameraVisuals.scopedFov = 55;
    visual.cameraVisuals.viewmodelEnabled = 1;
    visual.cameraVisuals.hideScoped = 1;
    visual.cameraVisuals.viewmodelFov = 82;
    visual.cameraVisuals.viewmodelOffset = {1.5f, -2, 3};
    visual.assists.strafeMode = 1;
    visual.assists.autoPistol = 1;
    visual.assists.pistolIntervalMs = 150;
    visual.worldVisuals.dropAmmo = 1;
    visual.assists.strafeWalkPause = 0;
    visual.assists.strafeRampMs = 125;
    visual.combat.utilityTimers = visual.combat.hitLog = 1;
    visual.combat.timerFire = 0;
    visual.combat.timerSmoke = 1;
    visual.combat.timerScale = 1.25f;
    visual.combat.timerRange = 95;
    visual.combat.hitLogRows = 6;
    visual.combat.hitLogBackground = 0;
    visual.combat.hitLogDuration = 4.5f;
    visual.combat.hitLogScale = 1.2f;
    visual.combat.hitLogX = .4f;
    visual.combat.hitLogY = .25f;
    for (unsigned i = 0; i < worldvisuals::DropGroupCount; ++i)
        visual.worldVisuals.dropGroups[i] = {i % 2, 1, i % 4, 25.f + i * 10, {.1f, .2f, .3f, .4f}};
    visual.combat.recoilGroups = {0, 1, 0, 1, 0, 1};
    visual.combat.weapons[1].activation = 2;
    visual.assists.cappedAcceleration = 0;
    visual.assists.delayMs = 60;
    visual.assists.intervalMs = 180;
    visual.assists.pressMs = 30;
    visual.assists.turnRate = 420;
    visual.assists.minSpeed = 100;
    visual.assists.airAcceleration = 14;
    visual.assists.airSpeedCap = 32;
    visual.assists.tickRate = 128;
    visual.cornerBoxes = 1;
    visual.fillOpacity = .3f;
    visual.uiScale = 1.2f;
    visual.weaponIcons = 0;
    visual.activeTargetBone = 3;
    visual.trackingTeams = static_cast<std::uint32_t>(camera::TargetTeams::AllTeams);
    visual.awarenessEnabled = 1;
    visual.awarenessTeams = 4;
    visual.shadedFill = 0;
    visual.paths.shotLifetime = 3.25f;
    visual.sessionBadge = 0;
    visual.badgeLight = 0;
    visual.badgeScale = 1.25f;
    visual.badgeOpacity = .7f;
    visual.spectatorX = .3f;
    visual.spectatorY = .7f;
    visual.spectatorScale = 1.3f;
    visual.spectatorOpacity = .6f;
    visual.combat.sceneExposure = .25f;
    visual.combat.sceneSaturation = .8f;
    visual.combat.sceneContrast = 1.2f;
    visual.combat.sceneVignette = .3f;
    visual.paths.shotCore = {.1f, .2f, .3f, .4f};
    visual.awarenessOffscreen = 0;
    visual.awarenessRadius = 220;
    visual.awarenessSize = 19;
    visual.awarenessColor = {.8f, .4f, .2f, .6f};
    visual.theme = 4;
    visual.softGlow = 1;
    visual.glowStrength = .36f;
    visual.haloColor = {.2f, .8f, 1, .9f};
    visual.accent = {.8f, .1f, .2f, 1};
    visual.menuOpacity = .7f;
    visual.panelOpacity = .35f;
    visual.backgroundOpacity = .6f;
    visual.backgroundFit = 1;
    visual.previewRotate = 0;
    visual.killSoundEnabled = 1;
    visual.killSoundVolume = .42f;
    visual.grenadePrediction = visual.grenadeTrails = visual.bulletTracers = visual.cameraFovEnabled = 1;
    visual.tracerTeams = 5;
    visual.cameraFov = 137;
    visual.combat.recoil = visual.combat.hitMarker = visual.combat.hitSound = visual.combat.bombTimer =
        visual.combat.ghosts = visual.combat.areas = visual.combat.hideParticles = visual.combat.contrast = 1;
    visual.combat.weapons[combat::WeaponProfile(7)] = {1, 3, 2, .8f, .6f, .12f};
    visual.combat.markerColor = {.1f, .2f, .3f, .4f};
    visual.combat.markerHold = .6f;
    visual.combat.markerDuration = 1.6f;
    visual.combat.ghostDuration = 1.7f;
    strcpy_s(visual.combat.hitSoundPath, "C:\\Sounds\\hit-\xE7\x8C\xAB.mp3");
    visual.menuFont = 1;
    visual.hudFont = 5;
    visual.menuAnimations = 0;
    visual.paths.trailGlow = 0;
    visual.paths.shotGlow = 1; // Explicit custom preference, independent of the quiet shipped defaults.
    visual.paths.previewGlow = 1;
    visual.paths.shotStrength = 2.75f;
    visual.paths.previewWidth = 3.5f;
    visual.paths.trailHE = {.1f, .2f, .3f, .4f};
    visual.paths.previewHE = {.9f, .8f, .7f, .6f};
    visual.paths.shotEnd = {.3f, .7f, .9f, .5f};
    strcpy_s(visual.killSoundPath, "C:\\Sounds\\\xE7\x8C\xAB kill.mp3");
    strcpy_s(visual.backgroundPath, "C:\\Pictures\\\xE7\x8C\xAB background.png");
    visual.trackingProfiles.groups[3] = {1, 1, 4, 32};
    visual.trackingProfiles.weapons[awareness::tracking::WeaponSlot(7)] = {1, 0, 2, 50};
    visual.trackingProfiles.compensation = 1;
    visual.trackingProfiles.latencyMs = 35;
    visual.worldVisuals.footDuration = 1.8f;
    visual.worldVisuals.dropNames = 1;
    visual.worldVisuals.dropRange = 85;
    TrackingConfiguration tracking;
    tracking.enabled = 1;
    tracking.fovDegrees = 24;
    tracking.interpolationSpeed = 12;
    tracking.hotkey = VK_XBUTTON1;
    int failures{};
    const auto check = [&](bool ok, const char *why) {
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", why);
        }
    };
    EffectsConfiguration effects;
    effects.materialEnabled = 1;
    effects.glowEnabled = 1;
    effects.visibility = EffectVisibility::OccludedOnly;
    effects.glowWidth = 9.5f;
    effects.materialColor = effects.glowColor = {.2f, .4f, .6f, .35f};
    effects.fovCircle = 0;
    EffectsConfiguration legacy;
    legacy.glowEnabled = 1;
    legacy.materialEnabled = 0;
    legacy.glowColor = {.1f, .2f, .3f, .4f};
    legacy.geometry = EffectGeometry::BoundsFallback;
    const auto migrated = UnifiedHighlight(legacy);
    check(migrated.materialEnabled && migrated.glowEnabled && migrated.materialColor.a == .4f &&
              migrated.materialColor.r == migrated.glowColor.r && migrated.geometry == EffectGeometry::MeshOnly,
          "legacy glow-only and bounds settings migrate to one model highlight");
    effects.fovThickness = 2.5f;
    check(SaveSettings(path, config, visual, tracking, effects), "write atomic profile");
    Configuration loaded;
    VisualOptions loadedVisual;
    TrackingConfiguration loadedTracking;
    loaded.worldUnitsPerMeter = 39.3700787f;
    EffectsConfiguration loadedEffects;
    check(LoadSettings(path, loaded, loadedVisual, &loadedTracking, &loadedEffects), "load profile");
    check(loadedVisual.scene.enabled && loadedVisual.scene.brightness == .65f && loadedVisual.scene.tint.b == .6f &&
              loadedVisual.lineups.enabled && !loadedVisual.lineups.heldOnly && loadedVisual.lineups.range == 75 &&
              loadedVisual.lineups.standTolerance == 12 && loadedVisual.lineups.aimTolerance == 2 &&
              loadedVisual.lineups.color.a == .8f && !std::strcmp(loadedVisual.lineups.mapOverride, "de_mirage"),
          "native scene and lineup settings round-trip including map and color");
    check(loadedVisual.cosmetics == visual.cosmetics, "every loadout setting and bounded name round-trip");
    auto nameBoundary = visual;
    for (std::size_t i = 0; i < 21; ++i)
        std::memcpy(nameBoundary.cosmetics.weapons[0].name.data() + i * 3, "\xE2\x82\xAC", 3);
    nameBoundary.cosmetics.weapons[0].name[63] = 0;
    check(SaveSettings(path, config, nameBoundary, tracking, effects) &&
              LoadSettings(path, loaded, loadedVisual, &loadedTracking, &loadedEffects) &&
              loadedVisual.cosmetics == nameBoundary.cosmetics,
          "maximum-length UTF-8 name survives INI hex serialization");
    const auto finishSection = L"Finish." + std::to_wstring(WeaponIcons[0].id);
    for (const auto invalidName : {L"GG", L"00", L"C0AF", L"41A", L"0A"}) {
        WritePrivateProfileStringW(finishSection.c_str(), L"NameHex", invalidName, path.c_str());
        check(!LoadSettings(path, loaded, loadedVisual, &loadedTracking, &loadedEffects) &&
                  loadedVisual.cosmetics == nameBoundary.cosmetics,
              "malformed, control, NUL or invalid UTF-8 names reject the entire profile atomically");
    }
    check(SaveSettings(path, config, visual, tracking, effects), "restore loadout fixture");
    WritePrivateProfileStringW(L"Cosmetics", nullptr, nullptr, path.c_str());
    WritePrivateProfileStringW(L"Finish.Knife", nullptr, nullptr, path.c_str());
    WritePrivateProfileStringW(L"Finish.Glove", nullptr, nullptr, path.c_str());
    for (const auto &weapon : WeaponIcons) {
        const auto section = L"Finish." + std::to_wstring(weapon.id);
        WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, path.c_str());
    }
    check(LoadSettings(path, loaded, loadedVisual, &loadedTracking, &loadedEffects) &&
              loadedVisual.cosmetics == cosmetics::Options{},
          "older profiles reset missing loadout fields to disabled defaults");
    check(SaveSettings(path, config, visual, tracking, effects) &&
              LoadSettings(path, loaded, loadedVisual, &loadedTracking, &loadedEffects),
          "restore complete profile after compatibility checks");

    check(loadedVisual.weather.enabled == 1 && loadedVisual.weather.kind == 2 && loadedVisual.weather.density == 0 &&
              loadedVisual.scoreboard == visual.scoreboard,
          "weather and scoreboard profile round-trip");
    check(loadedVisual.cameraVisuals.thirdPerson && loadedVisual.cameraVisuals.whileScoped &&
              loadedVisual.cameraVisuals.removeRecoil && loadedVisual.cameraVisuals.scopedFovEnabled &&
              loadedVisual.cameraVisuals.distance == 130 && loadedVisual.cameraVisuals.shoulder == -25 &&
              loadedVisual.cameraVisuals.height == 15 && loadedVisual.cameraVisuals.scopedFov == 55 &&
              loadedVisual.cameraVisuals.viewmodelEnabled && loadedVisual.cameraVisuals.hideScoped &&
              loadedVisual.cameraVisuals.viewmodelFov == 82 && loadedVisual.cameraVisuals.viewmodelOffset.y == -2 &&
              loadedVisual.assists.strafeMode == 1 && loadedVisual.assists.autoPistol &&
              loadedVisual.assists.pistolIntervalMs == 150 && loadedVisual.worldVisuals.dropAmmo,
          "camera, movement mode, pistol cadence and magazine settings round-trip");
    check(loadedVisual.sessionBadge == 0 && loadedVisual.badgeLight == 0 && loadedVisual.badgeScale == 1.25f &&
              loadedVisual.badgeOpacity == .7f,
          "badge appearance round trips");
    check(loadedVisual.menuPage == 4 && loadedVisual.assists.shootMode == 2 && !loadedVisual.assists.preserveForward &&
              loadedVisual.assists.strafeStrength == .42f &&
              loadedVisual.combat.recoilGroups == visual.combat.recoilGroups &&
              loadedVisual.combat.weapons[1].activation == 2,
          "menu, activation modes, movement strength and recoil groups survive saving");
    check(!loadedVisual.assists.strafeWalkPause && loadedVisual.assists.strafeRampMs == 125,
          "walk intent and steering ramp survive saving");
    const auto &newCombat = loadedVisual.combat;
    check(newCombat.utilityTimers && newCombat.hitLog && !newCombat.timerFire && newCombat.timerSmoke &&
              newCombat.timerScale == 1.25f && newCombat.timerRange == 95 && newCombat.hitLogRows == 6 &&
              !newCombat.hitLogBackground && newCombat.hitLogDuration == 4.5f && newCombat.hitLogScale == 1.2f &&
              newCombat.hitLogX == .4f && newCombat.hitLogY == .25f,
          "timer and feed options round trip");
    for (unsigned i = 0; i < worldvisuals::DropGroupCount; ++i) {
        const auto &g = loadedVisual.worldVisuals.dropGroups[i];
        check(g.enabled == i % 2 && g.custom == 1 && g.display == i % 4 && g.range == 25.f + i * 10 &&
                  g.color.r == .1f && g.color.g == .2f && g.color.b == .3f && g.color.a == .4f,
              "all dropped categories survive saving independently");
    }
    const auto &assists = loadedVisual.assists;
    check(assists.shoot && assists.jumper && assists.strafer && assists.scopeOnly &&
              assists.shootKey == assist::Mouse5 && !assists.cappedAcceleration && assists.delayMs == 60 &&
              assists.intervalMs == 180 && assists.pressMs == 30 && assists.turnRate == 420 &&
              assists.minSpeed == 100 && assists.airAcceleration == 14 && assists.airSpeedCap == 32 &&
              assists.tickRate == 128,
          "all independent assist settings survive a profile round trip");
    check(loadedVisual.combat.markerHold == .6f && loadedVisual.combat.markerDuration == 1.6f,
          "world marker hold and extended fade survive saving");
    check(loadedVisual.playerStyle.gradientFill && loadedVisual.playerStyle.cornerLength == .18f &&
              loadedVisual.playerStyle.boxRounding == 7 && loadedVisual.playerStyle.boxGlow == .6f &&
              loadedVisual.playerStyle.fillTop.a == .4f && loadedVisual.playerStyle.fillBottom.g == .6f &&
              loadedVisual.playerStyle.tintBrightness == 1.3f && loadedVisual.playerStyle.tintSaturation == .5f &&
              loadedVisual.playerStyle.haloPulse == .4f && loadedVisual.playerStyle.pulseSpeed == 2 &&
              loadedVisual.sky.enabled && loadedVisual.sky.brightness == .4f && loadedVisual.sky.tint.b == .4f &&
              loadedVisual.combat.sceneGamma == 1.3f && loadedVisual.combat.sceneVibrance == .3f &&
              loadedVisual.combat.sceneTemperature == -.2f && loadedVisual.combat.sceneShadows == .1f &&
              loadedVisual.combat.sceneHighlights == -.2f,
          "extended player and world styling survives save/load");
    check(loadedVisual.skeleton.enabled && !loadedVisual.skeleton.teamColor && loadedVisual.skeleton.outline &&
              loadedVisual.skeleton.joints && loadedVisual.skeleton.width == 2.1f &&
              loadedVisual.skeleton.opacity == .65f && loadedVisual.skeleton.color.a == .8f,
          "skeleton settings survive profile save/load");
    auto invalidSkeleton = loadedVisual;
    invalidSkeleton.skeleton.width = std::numeric_limits<float>::quiet_NaN();
    check(!ValidVisualOptions(invalidSkeleton), "nonfinite skeleton width rejected");
    auto invalidBadge = loadedVisual;
    invalidBadge.badgeScale = 0;
    check(!ValidVisualOptions(invalidBadge), "zero badge scale rejected");
    invalidBadge = loadedVisual;
    invalidBadge.badgeOpacity = std::numeric_limits<float>::quiet_NaN();
    check(!ValidVisualOptions(invalidBadge), "nonfinite badge opacity rejected");
    check(loadedVisual.trackingProfiles.groups[3].fov == 4 &&
              loadedVisual.trackingProfiles.weapons[awareness::tracking::WeaponSlot(7)].enabled == 0 &&
              loadedVisual.trackingProfiles.weapons[awareness::tracking::WeaponSlot(7)].custom == 1 &&
              loadedVisual.trackingProfiles.latencyMs == 35 && loadedVisual.worldVisuals.footDuration == 1.8f &&
              loadedVisual.worldVisuals.dropRange == 85,
          "weapon profiles, prediction and world visuals round trip");
    check(loadedVisual.combat.recoil && loadedVisual.combat.hitMarker && loadedVisual.combat.hitSound &&
              loadedVisual.combat.ghosts && loadedVisual.combat.areas && loadedVisual.combat.hideParticles &&
              loadedVisual.combat.contrast && loadedVisual.combat.bombTimer &&
              loadedVisual.combat.ghostDuration == 1.7f && loadedVisual.combat.Profile(7).startShot == 3 &&
              loadedVisual.combat.Profile(7).vertical == .8f && loadedVisual.combat.markerColor.a == .4f &&
              !std::strcmp(visual.combat.hitSoundPath, loadedVisual.combat.hitSoundPath),
          "new features, per-weapon profiles and hit sound round trip");
    check(loadedVisual.paths.shotLifetime == 3.25f && loadedVisual.spectatorX == .3f &&
              loadedVisual.spectatorY == .7f && loadedVisual.spectatorScale == 1.3f &&
              loadedVisual.spectatorOpacity == .6f && loadedVisual.combat.sceneExposure == .25f &&
              loadedVisual.combat.sceneSaturation == .8f && loadedVisual.combat.sceneContrast == 1.2f &&
              loadedVisual.combat.sceneVignette == .3f,
          "tracer lifetime, spectator card and scene controls round trip");
    WritePrivateProfileStringW(L"Visual", L"motionVersion", nullptr, path.c_str());
    WritePrivateProfileStringW(L"Visual", L"combat.ghostOpacity", L"1.5", path.c_str());
    check(LoadSettings(path, loaded, loadedVisual) && loadedVisual.combat.ghostDuration == 1.f &&
              loadedVisual.combat.ghostOpacity == 1.f,
          "older motion profiles migrate to a one-second replay with bounded opacity");
    loadedVisual.combat.ghostDuration = .3f;
    check(SaveSettings(path, loaded, loadedVisual, tracking, effects) && LoadSettings(path, loaded, loadedVisual) &&
              loadedVisual.combat.ghostDuration == .3f,
          "subsequent saved replay duration is not migrated again");
    check(SaveSettings(path, config, visual, tracking, effects) && LoadSettings(path, loaded, loadedVisual),
          "restore round-trip profile after migration check");
    for (const auto &entry : {std::pair{L"combat.ghostDuration", L"nan"},
                              {L"combat.hideParticles", L"2"},
                              {L"assists.tickRate", L"0"},
                              {L"playerStyle.boxGlow", L"nan"},
                              {L"sky.brightness", L"-1"},
                              {L"combat.sceneGamma", L"0"},
                              {L"combat.markerHold", L"-1"},
                              {L"combat.markerHold", L"nan"},
                              {L"combat.markerDuration", L"2.1"},
                              {L"assists.shootKey", L"1"},
                              {L"assists.turnRate", L"nan"},
                              {L"worldVisuals.footDuration", L"nan"},
                              {L"trackingProfiles.maxPredictionMs", L"999"},
                              {L"trackingProfiles.weaponSelection", L"999"},
                              {L"combat.recoilSelection", L"999"},
                              {L"HitSoundHex", L"GG"}}) {
        WritePrivateProfileStringW(L"Visual", entry.first, entry.second, path.c_str());
        check(!LoadSettings(path, loaded, loadedVisual) && loadedVisual.combat.ghostDuration == 1.7f,
              "invalid feature data rejected atomically");
        SaveSettings(path, config, visual, tracking, effects);
    }
    WritePrivateProfileStringW(L"Recoil.7", L"smoothing", L"-1", path.c_str());
    check(!LoadSettings(path, loaded, loadedVisual), "invalid per-weapon profile rejected");
    SaveSettings(path, config, visual, tracking, effects);
    check(loadedVisual.menuFont == 1 && loadedVisual.hudFont == 5 && !loadedVisual.menuAnimations &&
              !loadedVisual.paths.trailGlow && loadedVisual.paths.previewGlow && loadedVisual.paths.shotGlow &&
              loadedVisual.paths.shotStrength == 2.75f && loadedVisual.paths.previewWidth == 3.5f &&
              loadedVisual.paths.trailHE.a == .4f && loadedVisual.paths.previewHE.r == .9f &&
              loadedVisual.paths.shotEnd.g == .7f,
          "independent path appearance, fonts and animation round trip");
    for (const auto &entry : {std::pair{L"paths.shotLifetime", L"0"},
                              {L"paths.shotLifetime", L"5.01"},
                              {L"spectatorX", L"-1"},
                              {L"spectatorScale", L"nan"},
                              {L"combat.sceneContrast", L"10"},
                              {L"paths.shotStrength", L"nan"},
                              {L"paths.trailGlow", L"2"},
                              {L"paths.previewWidth", L"8"},
                              {L"paths.trailHE.a", L"1.1"},
                              {L"menuFont", L"12"},
                              {L"hudFont", L"-1"},
                              {L"menuAnimations", L"2"}}) {
        WritePrivateProfileStringW(L"Visual", entry.first, entry.second, path.c_str());
        check(!LoadSettings(path, loaded, loadedVisual) && loadedVisual.menuFont == 1 &&
                  loadedVisual.paths.shotStrength == 2.75f,
              "invalid appearance settings leave the active profile intact");
        check(SaveSettings(path, config, visual, tracking, effects), "restore appearance fixture");
    }
    for (const auto key :
         {L"paths.trailGlow", L"paths.previewGlow", L"paths.shotGlow", L"menuFont", L"hudFont", L"menuAnimations"})
        WritePrivateProfileStringW(L"Visual", key, nullptr, path.c_str());
    loadedVisual.paths.trailGlow = loadedVisual.paths.shotGlow = loadedVisual.paths.previewGlow = 1;
    check(LoadSettings(path, loaded, loadedVisual) && !loadedVisual.paths.trailGlow && !loadedVisual.paths.shotGlow &&
              !loadedVisual.paths.previewGlow && loadedVisual.menuFont == 0 && loadedVisual.hudFont == 0 &&
              loadedVisual.menuAnimations && loadedVisual.paths.shotStrength == 2.75f &&
              loadedVisual.paths.previewWidth == 3.5f,
          "missing legacy glow flags get quiet defaults without inheriting active flags or losing saved tuning");
    check(SaveSettings(path, config, visual, tracking, effects) && LoadSettings(path, loaded, loadedVisual),
          "restore appearance round trip");
    check(loadedVisual.softGlow && loadedVisual.glowStrength == .36f && loadedVisual.haloColor.a == .9f,
          "soft glow appearance survives saving");
    WritePrivateProfileStringW(L"Visual", L"glowStrength", L"nan", path.c_str());
    check(!LoadSettings(path, loaded, loadedVisual) && loadedVisual.glowStrength == .36f,
          "invalid glow strength leaves profile unchanged");
    check(SaveSettings(path, config, visual, tracking, effects), "restore soft glow fixture");
    WritePrivateProfileStringW(L"Visual", L"labelBackground", L"1", path.c_str());
    check(LoadSettings(path, loaded, loadedVisual) && !loadedVisual.labelBackground,
          "old profiles no longer restore label backplates");
    auto fill = effects;
    fill.visibility = EffectVisibility::TwoColor;
    const auto halo = NativeHalo(fill, visual);
    check(halo.materialEnabled && halo.visibility == EffectVisibility::AlwaysVisible &&
              std::abs(halo.materialColor.a - .2592f) < .0001f && halo.materialColor.b == 1,
          "native halo uses independent color and strength around the two-color fill");
    fill.visibility = EffectVisibility::OccludedOnly;
    check(!NativeHalo(fill, visual).materialEnabled, "behind-only mode never enables an always-visible halo");
    check(SaveSettings(path, config, visual, tracking, effects), "restore label fixture");
    check(loadedVisual.grenadePrediction && loadedVisual.grenadeTrails && loadedVisual.bulletTracers &&
              loadedVisual.tracerTeams == 5 && loadedVisual.cameraFovEnabled && loadedVisual.cameraFov == 137,
          "paths, shot filter and camera FOV survive save/load");
    WritePrivateProfileStringW(L"Visual", L"cameraFov", L"141", path.c_str());
    check(!LoadSettings(path, loaded, loadedVisual) && loadedVisual.cameraFov == 137,
          "camera FOV range rejected atomically");
    check(SaveSettings(path, config, visual, tracking, effects), "restore FOV fixture");
    WritePrivateProfileStringW(L"Visual", L"tracerTeams", L"6", path.c_str());
    check(!LoadSettings(path, loaded, loadedVisual), "invalid tracer team rejected");
    for (auto key :
         {L"grenadePrediction", L"grenadeTrails", L"bulletTracers", L"tracerTeams", L"cameraFovEnabled", L"cameraFov"})
        WritePrivateProfileStringW(L"Visual", key, nullptr, path.c_str());
    check(LoadSettings(path, loaded, loadedVisual) && !loadedVisual.grenadePrediction && !loadedVisual.grenadeTrails &&
              !loadedVisual.bulletTracers && !loadedVisual.cameraFovEnabled && loadedVisual.cameraFov == 90,
          "older profiles default new effects off");
    check(SaveSettings(path, config, visual, tracking, effects) && LoadSettings(path, loaded, loadedVisual),
          "restore trajectory fixture");
    check(loadedVisual.killSoundEnabled && loadedVisual.killSoundVolume == .42f &&
              std::strcmp(visual.killSoundPath, loadedVisual.killSoundPath) == 0,
          "kill sound and Unicode path round trip");
    WritePrivateProfileStringW(L"Visual", L"KillSoundHex", L"GG", path.c_str());
    check(!LoadSettings(path, loaded, loadedVisual), "malformed sound path rejected atomically");
    check(SaveSettings(path, config, visual, tracking, effects), "restore sound path");
    WritePrivateProfileStringW(L"Visual", L"killSoundVolume", L"1.2", path.c_str());
    check(!LoadSettings(path, loaded, loadedVisual), "excessive sound volume rejected");
    check(SaveSettings(path, config, visual, tracking, effects), "restore sound volume");
    for (auto key : {std::uint32_t(VK_ESCAPE), std::uint32_t(VK_INSERT), std::uint32_t(VK_HOME),
                     std::uint32_t(VK_RSHIFT), binding::WheelDown}) {
        auto custom = tracking;
        custom.hotkey = key;
        check(SaveSettings(path, config, visual, custom, effects) &&
                  LoadSettings(path, loaded, loadedVisual, &loadedTracking) && loadedTracking.hotkey == key,
              "custom bindings round trip");
    }
    check(SaveSettings(path, config, visual, tracking, effects), "restore tracking fixture");
    WritePrivateProfileStringW(L"Visual", L"killSoundEnabled", nullptr, path.c_str());
    WritePrivateProfileStringW(L"Visual", L"killSoundVolume", nullptr, path.c_str());
    WritePrivateProfileStringW(L"Visual", L"KillSoundHex", nullptr, path.c_str());
    check(LoadSettings(path, loaded, loadedVisual) && !loadedVisual.killSoundEnabled && !loadedVisual.killSoundPath[0],
          "old profiles leave kill sound disabled");
    check(SaveSettings(path, config, visual, tracking, effects), "restore sound fixture");
    check(loadedVisual.awarenessEnabled && loadedVisual.awarenessTeams == 4 && !loadedVisual.shadedFill &&
              loadedVisual.paths.shotCore.a == .4f && !loadedVisual.awarenessOffscreen &&
              loadedVisual.awarenessRadius == 220 && loadedVisual.awarenessSize == 19 &&
              loadedVisual.awarenessColor.a == .6f,
          "awareness settings round trip");
    WritePrivateProfileStringW(L"Visual", L"awarenessRadius", L"nan", path.c_str());
    check(!LoadSettings(path, loaded, loadedVisual) && loadedVisual.awarenessRadius == 220,
          "invalid awareness setting rejected atomically");
    check(SaveSettings(path, config, visual, tracking, effects), "restore awareness fixture");
    auto split = effects;
    split.visibility = EffectVisibility::TwoColor;
    split.materialColor = {1, 1, 1, .75f};
    split.glowColor = {.65f, .15f, .95f, .85f};
    check(SaveSettings(path, config, visual, tracking, split) &&
              LoadSettings(path, loaded, loadedVisual, &loadedTracking, &loadedEffects) &&
              loadedEffects.visibility == EffectVisibility::TwoColor && loadedEffects.materialColor.g == 1 &&
              loadedEffects.glowColor.g == .15f,
          "visible and hidden colors survive native INI normalization");
    check(SaveSettings(path, config, visual, tracking, effects) &&
              LoadSettings(path, loaded, loadedVisual, &loadedTracking, &loadedEffects),
          "restore effects fixture");
    check(loadedVisual.trackingTeams == 1 && loaded.teamFilter == TeamFilter::TeammatesOnly,
          "camera team choice round trips independently of HUD teams");
    WritePrivateProfileStringW(L"Visual", L"trackingTeams", L"2", path.c_str());
    check(!LoadSettings(path, loaded, loadedVisual) && loadedVisual.trackingTeams == 1,
          "invalid camera team choice rejected atomically");
    WritePrivateProfileStringW(L"Visual", L"trackingTeams", nullptr, path.c_str());
    check(LoadSettings(path, loaded, loadedVisual) && loadedVisual.trackingTeams == 0,
          "old profiles default to opponents even after all teams was selected");
    check(SaveSettings(path, config, visual, tracking, effects), "restore team choice fixture");
    check(loadedVisual.theme == 4 && loadedVisual.accent.r == .8f && loadedVisual.menuOpacity == .7f &&
              loadedVisual.panelOpacity == .35f && loadedVisual.backgroundOpacity == .6f &&
              loadedVisual.backgroundFit == 1 && !loadedVisual.previewRotate &&
              std::strcmp(visual.backgroundPath, loadedVisual.backgroundPath) == 0,
          "theme, opacity, Unicode image path and preview preference survive saving");
    auto instant = tracking;
    instant.interpolationSpeed = camera::InstantFollowSpeed;
    check(SaveSettings(path, config, visual, instant, effects) &&
              LoadSettings(path, loaded, loadedVisual, &loadedTracking) &&
              loadedTracking.interpolationSpeed == camera::InstantFollowSpeed,
          "instant speed survives saving");
    check(SaveSettings(path, config, visual, tracking, effects), "restore normal speed fixture");
    WritePrivateProfileStringW(L"Visual", L"menuOpacity", L"nan", path.c_str());
    check(!LoadSettings(path, loaded, loadedVisual) && loadedVisual.menuOpacity == .7f,
          "invalid theme rejected atomically");
    check(SaveSettings(path, config, visual, tracking, effects), "restore theme fixture");
    WritePrivateProfileStringW(L"Visual", L"BackgroundHex", L"GG", path.c_str());
    check(!LoadSettings(path, loaded, loadedVisual), "malformed image path rejected");
    check(SaveSettings(path, config, visual, tracking, effects), "restore image fixture");
    check(loadedVisual.activeTargetBone == 3, "target node selection survives saving and loading");
    WritePrivateProfileStringW(L"Visual", L"activeTargetBone", L"9", path.c_str());
    check(!LoadSettings(path, loaded, loadedVisual) && loadedVisual.activeTargetBone == 3,
          "out-of-range target choice rejected atomically");
    WritePrivateProfileStringW(L"Visual", L"activeTargetBone", nullptr, path.c_str());
    check(LoadSettings(path, loaded, loadedVisual) && loadedVisual.activeTargetBone == 0,
          "old profiles default to Head");
    check(loadedEffects.materialEnabled && loadedEffects.glowEnabled &&
              loadedEffects.visibility == EffectVisibility::OccludedOnly && loadedEffects.glowWidth == 9.5f &&
              loadedEffects.glowColor.a == .35f && !loadedEffects.fovCircle && loadedEffects.fovThickness == 2.5f &&
              loadedVisual.autoSave,
          "material, glow, FOV and autosave settings round trip");
    WritePrivateProfileStringW(L"Effects", L"glowWidth", L"nan", path.c_str());
    check(!LoadSettings(path, loaded, loadedVisual, &loadedTracking, &loadedEffects) && loadedEffects.glowWidth == 9.5f,
          "invalid effects rejected atomically");
    WritePrivateProfileStringW(L"Effects", nullptr, nullptr, path.c_str());
    check(LoadSettings(path, loaded, loadedVisual, &loadedTracking, &loadedEffects) && !loadedEffects.materialEnabled &&
              loadedEffects.fovCircle,
          "old profiles get safe effect defaults");
    check(loadedTracking.enabled == 1 && loadedTracking.hotkey == VK_XBUTTON1 && loadedTracking.fovDegrees == 24 &&
              loadedTracking.interpolationSpeed == 12,
          "DLL tracking settings survive round trip");
    WritePrivateProfileStringW(L"CameraTracking", L"fovDegrees", L"999", path.c_str());
    check(!LoadSettings(path, loaded, loadedVisual, &loadedTracking) && loadedTracking.fovDegrees == 24,
          "invalid tracking profile is rejected atomically");
    WritePrivateProfileStringW(L"CameraTracking", nullptr, nullptr, path.c_str());
    check(LoadSettings(path, loaded, loadedVisual, &loadedTracking) && loadedTracking.enabled == 0 &&
              loadedTracking.hotkey == VK_RBUTTON,
          "old profiles default tracking to disabled");
    check(loaded.worldUnitsPerMeter == 39.3700787f, "visual profiles preserve the host distance scale");
    check(loaded.boxThickness == config.boxThickness && loaded.opacity == config.opacity &&
              loaded.teamFilter == config.teamFilter,
          "numeric and enum configuration survives round trip");
    check(std::strcmp(config.fontPath, loaded.fontPath) == 0 && loadedVisual.cornerBoxes == 1 &&
              loadedVisual.uiScale == visual.uiScale && loadedVisual.weaponIcons == 0,
          "font path and visual settings survive round trip");
    WritePrivateProfileStringW(L"Overlay", L"opacity", L"nan", path.c_str());
    const auto previous = loaded;
    check(!LoadSettings(path, loaded, loadedVisual) && loaded.opacity == previous.opacity,
          "invalid profile does not modify current settings");
    check(!std::filesystem::exists(path + L".tmp"), "atomic save leaves no temporary file");
    DeleteFileW(path.c_str());
    check(!LoadSettings(path, loaded, loadedVisual), "missing profile rejected");
    const auto temporaryRoot = std::filesystem::path(path).parent_path() /
                               (L"profile-portability-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto originalDirectory = temporaryRoot / L"original";
    const auto movedDirectory = temporaryRoot / L"moved";
    std::filesystem::create_directories(originalDirectory / L"profile");
    const auto initialProfile = (originalDirectory / L"OverlaySettings.ini").wstring();
    const auto initialDefaults = DefaultSettingsPath(initialProfile);
    config = {};
    visual = {};
    config.opacity = .73f;
    visual.theme = 2;
    const auto setUtf8 = [](auto &out, const std::filesystem::path &value) {
        const auto bytes = value.u8string();
        strcpy_s(out, reinterpret_cast<const char *>(bytes.c_str()));
    };
    setUtf8(config.fontPath, originalDirectory / L"profile" / L"font.ttf");
    setUtf8(visual.backgroundPath, originalDirectory / L"profile" / L"background.png");
    setUtf8(visual.killSoundPath, originalDirectory / L"profile" / L"sound.wav");
    check(SaveSettings(initialDefaults, config, visual, tracking, effects), "save named defaults with portable assets");
    loaded = {};
    loaded.worldUnitsPerMeter = 39.3700787f;
    check(LoadStartupSettings(initialProfile, loaded, loadedVisual, &loadedTracking, &loadedEffects) &&
              loaded.opacity == .73f && loadedVisual.theme == 2 && loaded.worldUnitsPerMeter == 39.3700787f,
          "first run loads saved defaults and preserves host scale");
    config.opacity = .41f;
    check(SaveSettings(initialProfile, config, visual, tracking, effects) &&
              LoadStartupSettings(initialProfile, loaded, loadedVisual) && loaded.opacity == .41f,
          "existing profile takes precedence over defaults");
    std::filesystem::rename(originalDirectory, movedDirectory);
    const auto movedProfile = (movedDirectory / L"OverlaySettings.ini").wstring();
    check(LoadStartupSettings(movedProfile, loaded, loadedVisual), "profile loads after moving its whole folder");
    const auto asPath = [](const char *text) {
        return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t *>(text)));
    };
    check(asPath(loaded.fontPath) == movedDirectory / L"profile" / L"font.ttf" &&
              asPath(loadedVisual.backgroundPath) == movedDirectory / L"profile" / L"background.png" &&
              asPath(loadedVisual.killSoundPath) == movedDirectory / L"profile" / L"sound.wav",
          "font, image and sound resolve against the INI folder, not the working directory");
    check(SaveSettings(movedProfile, loaded, loadedVisual, loadedTracking, loadedEffects), "resave moved profile");
    std::ifstream portableFile{std::filesystem::path(movedProfile)};
    const std::string portableText{std::istreambuf_iterator<char>{portableFile}, {}};
    portableFile.close();
    check(portableText.find("BackgroundHex=70726F66696C655C6261636B67726F756E642E706E67") != std::string::npos,
          "automatic saves retain a relative image path");
    check(LoadSettings(DefaultSettingsPath(movedProfile), loaded, loadedVisual) && loaded.opacity == .73f,
          "saved defaults remain unchanged when the active profile changes");
    WritePrivateProfileStringW(L"Overlay", L"opacity", L"nan", movedProfile.c_str());
    check(!LoadStartupSettings(movedProfile, loaded, loadedVisual) && loaded.opacity == .73f,
          "invalid active profile leaves current values intact");
    DeleteFileW(movedProfile.c_str());
    WritePrivateProfileStringW(L"Visual", L"BackgroundHex", L"433A666F6F", DefaultSettingsPath(movedProfile).c_str());
    check(!LoadStartupSettings(movedProfile, loaded, loadedVisual) && loaded.opacity == .73f,
          "ambiguous drive-relative media paths reject the default atomically");
    std::filesystem::remove_all(temporaryRoot);
    std::printf("Profile checks: %d failures\n", failures);
    return failures ? 1 : 0;
}
