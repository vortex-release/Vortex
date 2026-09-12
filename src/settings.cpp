#include "settings.hpp"
#include "runtime_support.hpp"
#include "../app/app_paths.hpp"
#include <charconv>
#include <string_view>
#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <filesystem>
#include <atomic>

namespace awareness {
namespace {
std::filesystem::path MediaPath(std::string_view text) {
    const int count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!count)
        throw std::runtime_error("Invalid media path");
    std::wstring wide(count, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(),
                             count))
        throw std::runtime_error("Invalid media path");
    return std::filesystem::path(wide);
}
std::string PathUtf8(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}
// Files shipped beside the profile stay portable, including after an automatic save.
std::string PortableMediaPath(const std::wstring &profile, std::string_view text) {
    if (text.empty())
        return {};
    const auto path = MediaPath(text);
    if (!path.is_absolute()) {
        if (path.has_root_path())
            throw std::runtime_error("Ambiguous media path");
        return std::string(text);
    }
    const auto directory = std::filesystem::absolute(profile).parent_path().lexically_normal();
    const auto relative = path.lexically_normal().lexically_relative(directory);
    if (!relative.empty() && *relative.begin() != L"..")
        return PathUtf8(relative);
    return std::string(text);
}
template <std::size_t N> bool ResolveMediaPath(const std::wstring &profile, char (&text)[N]) {
    if (!text[0])
        return true;
    auto path = MediaPath(text);
    if (path.is_absolute())
        return true;
    if (path.has_root_path())
        return false;
    path = (std::filesystem::absolute(profile).parent_path() / path).lexically_normal();
    const auto resolved = PathUtf8(path);
    if (resolved.size() >= N)
        return false;
    std::memcpy(text, resolved.c_str(), resolved.size() + 1);
    return true;
}
struct Field {
    const char *name;
    std::size_t offset;
    bool floating;
};
#define U(name)                                                                                                        \
    Field {                                                                                                            \
        #name, offsetof(Configuration, name), false                                                                    \
    }
#define F(name)                                                                                                        \
    Field {                                                                                                            \
        #name, offsetof(Configuration, name), true                                                                     \
    }
#define C(name) F(name.r), F(name.g), F(name.b), F(name.a)
constexpr Field fields[]{U(enabled),
                         U(boxes),
                         U(lines),
                         U(healthBars),
                         U(names),
                         U(distances),
                         U(toggleKey),
                         U(alternateToggleKey),
                         U(teamFilter),
                         U(healthBar),
                         U(colorBoxesByHealth),
                         U(clearOverlayDepth),
                         U(staleFrameMilliseconds),
                         F(boxThickness),
                         F(lineThickness),
                         F(barThickness),
                         F(fontPixels),
                         F(fadeStartMeters),
                         F(maxDistanceMeters),
                         F(opacity),
                         F(lowHealthThreshold),
                         F(mediumHealthThreshold),
                         C(teammate),
                         C(opponent),
                         C(neutral),
                         C(healthy),
                         C(medium),
                         C(low),
                         C(text),
                         C(outline),
                         C(barBackground)};
#undef U
#undef F
#undef C
#define U(name)                                                                                                        \
    Field {                                                                                                            \
        #name, offsetof(VisualOptions, name), false                                                                    \
    }
#define F(name)                                                                                                        \
    Field {                                                                                                            \
        #name, offsetof(VisualOptions, name), true                                                                     \
    }
constexpr Field visuals[]{
    U(weather.enabled),
    U(weather.kind),
    U(weather.density),
    U(scoreboard.enabled),
    U(scoreboard.weapons),
    U(scoreboard.armor),
    U(scoreboard.objective),
    F(scoreboard.scale),
    U(skeleton.enabled),
    U(skeleton.teamColor),
    U(skeleton.outline),
    U(skeleton.joints),
    F(skeleton.width),
    F(skeleton.opacity),
    F(skeleton.color.r),
    F(skeleton.color.g),
    F(skeleton.color.b),
    F(skeleton.color.a),
    U(playerStyle.gradientFill),
    F(playerStyle.cornerLength),
    F(playerStyle.boxRounding),
    F(playerStyle.boxGlow),
    F(playerStyle.fillTop.r),
    F(playerStyle.fillTop.g),
    F(playerStyle.fillTop.b),
    F(playerStyle.fillTop.a),
    F(playerStyle.fillBottom.r),
    F(playerStyle.fillBottom.g),
    F(playerStyle.fillBottom.b),
    F(playerStyle.fillBottom.a),
    F(playerStyle.tintBrightness),
    F(playerStyle.tintSaturation),
    F(playerStyle.haloPulse),
    F(playerStyle.pulseSpeed),
    U(sky.enabled),
    F(sky.brightness),
    F(sky.tint.r),
    F(sky.tint.g),
    F(sky.tint.b),
    F(sky.tint.a),
    F(combat.sceneGamma),
    F(combat.sceneVibrance),
    F(combat.sceneTemperature),
    F(combat.sceneShadows),
    F(combat.sceneHighlights),
    U(trackingProfiles.selection),
    U(trackingProfiles.weaponSelection),
    U(trackingProfiles.compensation),
    F(trackingProfiles.latencyMs),
    F(trackingProfiles.maxPredictionMs),
    F(trackingProfiles.strength),
    U(worldVisuals.footsteps),
    U(worldVisuals.footTeams),
    U(worldVisuals.footDouble),
    U(worldVisuals.dropped),
    U(worldVisuals.dropBoxes),
    U(worldVisuals.dropIcons),
    U(worldVisuals.dropNames),
    U(worldVisuals.dropDistance),
    F(worldVisuals.footDuration),
    F(worldVisuals.footRadius),
    F(worldVisuals.footWidth),
    F(worldVisuals.footRange),
    F(worldVisuals.dropRange),
    F(worldVisuals.dropWidth),
    F(worldVisuals.footColor.r),
    F(worldVisuals.footColor.g),
    F(worldVisuals.footColor.b),
    F(worldVisuals.footColor.a),
    F(worldVisuals.dropColor.r),
    F(worldVisuals.dropColor.g),
    F(worldVisuals.dropColor.b),
    F(worldVisuals.dropColor.a),
    U(combat.recoil),
    U(combat.recoilInput),
    F(combat.mouseYaw),
    F(combat.mousePitch),
    U(combat.recoilSelection),
    U(combat.bombTimer),
    U(combat.hitMarker),
    U(combat.hitSound),
    U(combat.damageNumbers),
    U(combat.ghosts),
    U(combat.ghostDirection),
    U(combat.contrast),
    U(combat.areas),
    U(combat.areaFill),
    U(combat.areaGlow),
    F(combat.areaOutline),
    U(combat.utilityTimers),
    U(combat.timerFire),
    U(combat.timerSmoke),
    F(combat.timerScale),
    F(combat.timerRange),
    U(combat.hitLog),
    U(combat.hitLogRows),
    U(combat.hitLogBackground),
    F(combat.hitLogDuration),
    F(combat.hitLogScale),
    F(combat.hitLogX),
    F(combat.hitLogY),
    U(combat.hideParticles),
    U(combat.fireArea),
    U(combat.smokeArea),
    U(combat.blastArea),
    F(combat.hitVolume),
    F(combat.markerSize),
    F(combat.markerDuration),
    F(combat.markerHold),
    F(combat.damageDuration),
    F(combat.ghostDuration),
    F(combat.ghostOpacity),
    F(combat.worldDarkness),
    F(combat.sceneContrast),
    F(combat.sceneSaturation),
    F(combat.sceneExposure),
    F(combat.sceneVignette),
    F(combat.sceneTintStrength),
    F(combat.sceneTint.r),
    F(combat.sceneTint.g),
    F(combat.sceneTint.b),
    F(combat.sceneTint.a),
    F(combat.entityBrightness),
    F(combat.entitySaturation),
    F(combat.markerColor.r),
    F(combat.markerColor.g),
    F(combat.markerColor.b),
    F(combat.markerColor.a),
    F(combat.damageColor.r),
    F(combat.damageColor.g),
    F(combat.damageColor.b),
    F(combat.damageColor.a),
    F(combat.bombSafe.r),
    F(combat.bombSafe.g),
    F(combat.bombSafe.b),
    F(combat.bombSafe.a),
    F(combat.bombWarning.r),
    F(combat.bombWarning.g),
    F(combat.bombWarning.b),
    F(combat.bombWarning.a),
    F(combat.bombDanger.r),
    F(combat.bombDanger.g),
    F(combat.bombDanger.b),
    F(combat.bombDanger.a),
    F(combat.ghostFriend.r),
    F(combat.ghostFriend.g),
    F(combat.ghostFriend.b),
    F(combat.ghostFriend.a),
    F(combat.ghostEnemy.r),
    F(combat.ghostEnemy.g),
    F(combat.ghostEnemy.b),
    F(combat.ghostEnemy.a),
    F(combat.fireColor.r),
    F(combat.fireColor.g),
    F(combat.fireColor.b),
    F(combat.fireColor.a),
    F(combat.smokeColor.r),
    F(combat.smokeColor.g),
    F(combat.smokeColor.b),
    F(combat.smokeColor.a),
    F(combat.blastColor.r),
    F(combat.blastColor.g),
    F(combat.blastColor.b),
    F(combat.blastColor.a),
    U(cornerBoxes),
    U(fillBoxes),
    U(healthNumbers),
    U(labelBackground),
    U(statusHud),
    U(lineOrigin),
    F(fillOpacity),
    F(uiScale),
    U(weaponIcons),
    U(autoSave),
    U(activeTargetBone),
    U(theme),
    U(backgroundEnabled),
    U(backgroundFit),
    U(previewRotate),
    F(menuOpacity),
    F(panelOpacity),
    F(backgroundOpacity),
    F(accent.r),
    F(accent.g),
    F(accent.b),
    F(accent.a),
    U(trackingTeams),
    U(awarenessEnabled),
    U(awarenessTeams),
    U(awarenessOffscreen),
    F(awarenessRadius),
    F(awarenessSize),
    F(awarenessColor.r),
    F(awarenessColor.g),
    F(awarenessColor.b),
    F(awarenessColor.a),
    U(killSoundEnabled),
    F(killSoundVolume),
    U(grenadePrediction),
    U(grenadeTrails),
    U(bulletTracers),
    U(tracerTeams),
    U(cameraVisuals.viewmodelEnabled),
    U(cameraVisuals.hideScoped),
    F(cameraVisuals.viewmodelFov),
    F(cameraVisuals.viewmodelOffset.x),
    F(cameraVisuals.viewmodelOffset.y),
    F(cameraVisuals.viewmodelOffset.z),
    U(cameraVisuals.thirdPerson),
    U(cameraVisuals.thirdPersonMode),
    U(cameraVisuals.thirdPersonKey),
    U(cameraVisuals.whileScoped),
    U(cameraVisuals.removeRecoil),
    U(cameraVisuals.scopedFovEnabled),
    F(cameraVisuals.distance),
    F(cameraVisuals.shoulder),
    F(cameraVisuals.height),
    F(cameraVisuals.scopedFov),
    U(scene.enabled),
    F(scene.brightness),
    F(scene.tint.r),
    F(scene.tint.g),
    F(scene.tint.b),
    F(scene.tint.a),
    U(lineups.enabled),
    U(lineups.heldOnly),
    F(lineups.range),
    F(lineups.standTolerance),
    F(lineups.aimTolerance),
    F(lineups.color.r),
    F(lineups.color.g),
    F(lineups.color.b),
    F(lineups.color.a),
    U(assists.strafeMode),
    U(assists.autoPistol),
    F(assists.pistolIntervalMs),
    U(worldVisuals.dropAmmo),
    U(cameraFovEnabled),
    F(cameraFov),
    U(softGlow),
    U(shadedFill),
    F(glowStrength),
    F(haloColor.r),
    F(haloColor.g),
    F(haloColor.b),
    F(haloColor.a),
    U(menuFont),
    U(hudFont),
    U(menuAnimations),
    U(menuPage),
    U(assists.shootMode),
    U(assists.preserveForward),
    F(assists.strafeStrength),
    U(assists.strafeWalkPause),
    F(assists.strafeRampMs),
    {"combat.recoilGroups.0",
     offsetof(VisualOptions, combat) + offsetof(combat::Options, recoilGroups) + 0 * sizeof(std::uint32_t), false},
    {"combat.recoilGroups.1",
     offsetof(VisualOptions, combat) + offsetof(combat::Options, recoilGroups) + 1 * sizeof(std::uint32_t), false},
    {"combat.recoilGroups.2",
     offsetof(VisualOptions, combat) + offsetof(combat::Options, recoilGroups) + 2 * sizeof(std::uint32_t), false},
    {"combat.recoilGroups.3",
     offsetof(VisualOptions, combat) + offsetof(combat::Options, recoilGroups) + 3 * sizeof(std::uint32_t), false},
    {"combat.recoilGroups.4",
     offsetof(VisualOptions, combat) + offsetof(combat::Options, recoilGroups) + 4 * sizeof(std::uint32_t), false},
    {"combat.recoilGroups.5",
     offsetof(VisualOptions, combat) + offsetof(combat::Options, recoilGroups) + 5 * sizeof(std::uint32_t), false},

    U(assists.shoot),
    U(assists.shootKey),
    U(assists.scopeOnly),
    U(assists.jumper),
    U(assists.strafer),
    U(assists.cappedAcceleration),
    F(assists.delayMs),
    F(assists.intervalMs),
    F(assists.pressMs),
    F(assists.turnRate),
    F(assists.minSpeed),
    F(assists.airAcceleration),
    F(assists.airSpeedCap),
    F(assists.tickRate),
    U(sessionBadge),
    U(badgeLight),
    F(badgeScale),
    F(badgeOpacity),
    U(spectators),
    U(keepAwake),
    U(autoAccept),
    F(spectatorX),
    F(spectatorY),
    F(spectatorScale),
    F(spectatorOpacity),
    U(paths.trailGlow),
    U(paths.shotGlow),
    U(paths.previewGlow),
    F(paths.trailStrength),
    F(paths.shotStrength),
    F(paths.previewStrength),
    F(paths.trailWidth),
    F(paths.shotWidth),
    F(paths.shotLifetime),
    F(paths.previewWidth),
    F(paths.trailHE.r),
    F(paths.trailHE.g),
    F(paths.trailHE.b),
    F(paths.trailHE.a),
    F(paths.trailSmoke.r),
    F(paths.trailSmoke.g),
    F(paths.trailSmoke.b),
    F(paths.trailSmoke.a),
    F(paths.trailFlash.r),
    F(paths.trailFlash.g),
    F(paths.trailFlash.b),
    F(paths.trailFlash.a),
    F(paths.trailFire.r),
    F(paths.trailFire.g),
    F(paths.trailFire.b),
    F(paths.trailFire.a),
    F(paths.trailDecoy.r),
    F(paths.trailDecoy.g),
    F(paths.trailDecoy.b),
    F(paths.trailDecoy.a),
    F(paths.previewHE.r),
    F(paths.previewHE.g),
    F(paths.previewHE.b),
    F(paths.previewHE.a),
    F(paths.previewSmoke.r),
    F(paths.previewSmoke.g),
    F(paths.previewSmoke.b),
    F(paths.previewSmoke.a),
    F(paths.previewFlash.r),
    F(paths.previewFlash.g),
    F(paths.previewFlash.b),
    F(paths.previewFlash.a),
    F(paths.previewFire.r),
    F(paths.previewFire.g),
    F(paths.previewFire.b),
    F(paths.previewFire.a),
    F(paths.previewDecoy.r),
    F(paths.previewDecoy.g),
    F(paths.previewDecoy.b),
    F(paths.previewDecoy.a),
    F(paths.shotStart.r),
    F(paths.shotStart.g),
    F(paths.shotStart.b),
    F(paths.shotStart.a),
    F(paths.shotEnd.r),
    F(paths.shotEnd.g),
    F(paths.shotEnd.b),
    F(paths.shotEnd.a),
    F(paths.shotCore.r),
    F(paths.shotCore.g),
    F(paths.shotCore.b),
    F(paths.shotCore.a)};
#undef U
#undef F
constexpr Field dropFields[]{{"enabled", offsetof(worldvisuals::DropGroup, enabled), false},
                             {"custom", offsetof(worldvisuals::DropGroup, custom), false},
                             {"display", offsetof(worldvisuals::DropGroup, display), false},
                             {"range", offsetof(worldvisuals::DropGroup, range), true},
                             {"color.r", offsetof(worldvisuals::DropGroup, color) + offsetof(Color, r), true},
                             {"color.g", offsetof(worldvisuals::DropGroup, color) + offsetof(Color, g), true},
                             {"color.b", offsetof(worldvisuals::DropGroup, color) + offsetof(Color, b), true},
                             {"color.a", offsetof(worldvisuals::DropGroup, color) + offsetof(Color, a), true}};
constexpr Field groupFields[]{{"custom", offsetof(tracking::Profile, custom), false},
                              {"enabled", offsetof(tracking::Profile, enabled), false},
                              {"fov", offsetof(tracking::Profile, fov), true},
                              {"speed", offsetof(tracking::Profile, speed), true}};
constexpr Field recoilFields[]{{"overrideDefault", offsetof(combat::RecoilProfile, overrideDefault), false},
                               {"activation", offsetof(combat::RecoilProfile, activation), false},
                               {"startShot", offsetof(combat::RecoilProfile, startShot), false},
                               {"curve", offsetof(combat::RecoilProfile, curve), false},
                               {"vertical", offsetof(combat::RecoilProfile, vertical), true},
                               {"horizontal", offsetof(combat::RecoilProfile, horizontal), true},
                               {"smoothing", offsetof(combat::RecoilProfile, smoothing), true}};
constexpr Field trackingFields[]{{"enabled", offsetof(TrackingConfiguration, enabled), false},
                                 {"hotkey", offsetof(TrackingConfiguration, hotkey), false},
                                 {"fovDegrees", offsetof(TrackingConfiguration, fovDegrees), true},
                                 {"interpolationSpeed", offsetof(TrackingConfiguration, interpolationSpeed), true}};
#define U(name)                                                                                                        \
    Field {                                                                                                            \
        #name, offsetof(EffectsConfiguration, name), false                                                             \
    }
#define F(name)                                                                                                        \
    Field {                                                                                                            \
        #name, offsetof(EffectsConfiguration, name), true                                                              \
    }
#define C(name) F(name.r), F(name.g), F(name.b), F(name.a)
constexpr Field effectFields[]{U(materialEnabled), U(glowEnabled), U(visibility), U(geometry),     F(glowWidth),
                               C(materialColor),   C(glowColor),   U(fovCircle),  F(fovThickness), C(fovColor)};
#undef U
#undef F
#undef C
template <class T> void AppendFields(std::string &text, const T &object, std::span<const Field> entries) {
    for (const auto &field : entries) {
        char value[64]{};
        std::to_chars_result converted;
        const auto *bytes = reinterpret_cast<const unsigned char *>(&object) + field.offset;
        if (field.floating) {
            float v;
            std::memcpy(&v, bytes, sizeof(v));
            converted = std::to_chars(value, value + sizeof(value), v);
        } else {
            std::uint32_t v;
            std::memcpy(&v, bytes, sizeof(v));
            converted = std::to_chars(value, value + sizeof(value), v);
        }
        if (converted.ec != std::errc{})
            throw std::runtime_error("Settings conversion");
        text += field.name;
        text += '=';
        text.append(value, converted.ptr);
        text += "\r\n";
    }
}
template <class T>
bool ReadFields(const std::wstring &path, const wchar_t *section, T &object, std::span<const Field> entries) {
    for (const auto &field : entries) {
        std::wstring key(field.name, field.name + std::strlen(field.name));
        wchar_t wide[96]{};
        const auto length = GetPrivateProfileStringW(section, key.c_str(), L"", wide, 96, path.c_str());
        if (!length)
            continue;
        if (length >= 95)
            return false;
        std::string value;
        for (unsigned i = 0; i < length; ++i) {
            if (wide[i] > 127)
                return false;
            value += static_cast<char>(wide[i]);
        }
        auto *bytes = reinterpret_cast<unsigned char *>(&object) + field.offset;
        std::from_chars_result parsed;
        if (field.floating) {
            float v{};
            parsed = std::from_chars(value.data(), value.data() + value.size(), v);
            std::memcpy(bytes, &v, sizeof(v));
        } else {
            std::uint32_t v{};
            parsed = std::from_chars(value.data(), value.data() + value.size(), v);
            std::memcpy(bytes, &v, sizeof(v));
        }
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
            return false;
    }
    return true;
}
} // namespace
std::wstring SettingsPath() {
    try {
        const auto root = vortex::DataDirectory();
        const auto destination = root / L"OverlaySettings.ini";
        if (std::filesystem::exists(destination))
            return destination.wstring();
        auto oldDirectory = vortex::ModuleDirectory(OverlayModule());
        if (!std::filesystem::exists(oldDirectory / L"OverlaySettings.ini") &&
            !GetEnvironmentVariableW(L"VORTEX_DATA_DIR", nullptr, 0))
            oldDirectory = vortex::KnownFolder(FOLDERID_Desktop) / L"CS2-Observer-Overlay";
        const auto oldProfile = oldDirectory / L"OverlaySettings.ini";
        if (std::filesystem::exists(oldProfile) && oldProfile != destination) {
            Configuration config;
            VisualOptions visual;
            TrackingConfiguration tracking;
            EffectsConfiguration effects;
            if (!LoadSettings(oldProfile.wstring(), config, visual, &tracking, &effects))
                return {};
            if (std::filesystem::exists(oldDirectory / L"profile"))
                std::filesystem::copy(oldDirectory / L"profile", root / L"profile",
                                      std::filesystem::copy_options::recursive |
                                          std::filesystem::copy_options::skip_existing);
            const auto relocate = [&](auto &text) {
                if (!text[0])
                    return;
                const auto relative = MediaPath(text).lexically_relative(oldDirectory / L"profile");
                if (!relative.empty() && *relative.begin() != L"..") {
                    const auto moved = PathUtf8(root / L"profile" / relative);
                    if (moved.size() >= sizeof(text))
                        throw std::runtime_error("Profile asset path is too long.");
                    std::memcpy(text, moved.c_str(), moved.size() + 1);
                }
            };
            relocate(config.fontPath);
            relocate(visual.backgroundPath);
            relocate(visual.killSoundPath);
            relocate(visual.combat.hitSoundPath);
            if (!SaveSettings(destination.wstring(), config, visual, tracking, effects))
                return {};
        }
        return destination.wstring();
    } catch (...) {
        return {};
    }
}

namespace {
constexpr Field cosmeticFields[]{
    {"enabled", offsetof(cosmetics::Options, enabled), false},
    {"knife.enabled", offsetof(cosmetics::Options, knife) + offsetof(cosmetics::Appearance, enabled), false},
    {"knife.definition", offsetof(cosmetics::Options, knife) + offsetof(cosmetics::Appearance, definition), false},
    {"glove.enabled", offsetof(cosmetics::Options, glove) + offsetof(cosmetics::Appearance, enabled), false},
    {"glove.definition", offsetof(cosmetics::Options, glove) + offsetof(cosmetics::Appearance, definition), false},
    {"agentT", offsetof(cosmetics::Options, agents), false},
    {"agentCT", offsetof(cosmetics::Options, agents) + sizeof(std::uint32_t), false}};
constexpr Field finishFields[]{{"enabled", offsetof(cosmetics::Finish, enabled), false},
                               {"paintKit", offsetof(cosmetics::Finish, paintKit), false},
                               {"seed", offsetof(cosmetics::Finish, seed), false},
                               {"wear", offsetof(cosmetics::Finish, wear), true},
                               {"statTrak", offsetof(cosmetics::Finish, statTrak), false},
                               {"kills", offsetof(cosmetics::Finish, kills), false}};
void AppendFinish(std::string &text, const cosmetics::Finish &finish, const std::string &section) {
    text += "\r\n[" + section + "]\r\n";
    AppendFields(text, finish, finishFields);
    text += "NameHex=";
    constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char c : std::string(finish.name.data())) {
        text += hex[c >> 4];
        text += hex[c & 15];
    }
    text += "\r\n";
}
bool ReadFinish(const std::wstring &path, cosmetics::Finish &finish, const std::wstring &section) {
    if (!ReadFields(path, section.c_str(), finish, finishFields))
        return false;
    wchar_t hex[129]{};
    const auto n = GetPrivateProfileStringW(section.c_str(), L"NameHex", L"", hex, 129, path.c_str());
    if (n % 2 || n >= 128)
        return false;
    auto digit = [](wchar_t c) {
        return c >= L'0' && c <= L'9' ? c - L'0' : c >= L'A' && c <= L'F' ? c - L'A' + 10 : -1;
    };
    for (unsigned i = 0; i < n; i += 2) {
        const int hi = digit(hex[i]), lo = digit(hex[i + 1]);
        if (hi < 0 || lo < 0 || !(hi * 16 + lo))
            return false;
        finish.name[i / 2] = static_cast<char>(hi * 16 + lo);
    }
    return cosmetics::Valid(finish);
}
} // namespace
std::wstring DefaultSettingsPath(const std::wstring &profilePath) {
    if (profilePath.empty())
        return {};
    if (std::filesystem::path(profilePath).parent_path() == vortex::DataDirectory())
        return (vortex::ModuleDirectory(OverlayModule()) / L"DefaultSettings.ini").wstring();
    return (std::filesystem::path(profilePath).parent_path() / L"DefaultSettings.ini").wstring();
}
bool LoadStartupSettings(const std::wstring &path, Configuration &config, VisualOptions &visual,
                         TrackingConfiguration *tracking, EffectsConfiguration *effects) noexcept {
    try {
        if (path.empty())
            return false;
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            return LoadSettings(path, config, visual, tracking, effects);
        const auto error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            return false;
        return LoadSettings(DefaultSettingsPath(path), config, visual, tracking, effects);
    } catch (...) {
        return false;
    }
}
bool SaveSettings(const std::wstring &path, const Configuration &config, const VisualOptions &visual,
                  const TrackingConfiguration &tracking, const EffectsConfiguration &effects,
                  bool replaceExisting) noexcept {
    try {
        if (path.empty() || !ValidConfiguration(config) || !ValidVisualOptions(visual) ||
            !ValidTrackingConfiguration(tracking) || !ValidEffectsConfiguration(effects))
            return false;
        std::string text = "[Overlay]\r\nVersion=1\r\n";
        AppendFields(text, config, fields);
        text += "FontHex=";
        constexpr char hex[] = "0123456789ABCDEF";
        for (const unsigned char c : PortableMediaPath(path, config.fontPath)) {
            text += hex[c >> 4];
            text += hex[c & 15];
        }
        text += "\r\n[Visual]\r\nmotionVersion=2\r\n";
        AppendFields(text, visual, visuals);
        text += "BackgroundHex=";
        for (const unsigned char c : PortableMediaPath(path, visual.backgroundPath)) {
            text += hex[c >> 4];
            text += hex[c & 15];
        }
        text += "\r\n";
        text += "KillSoundHex=";
        for (const unsigned char c : PortableMediaPath(path, visual.killSoundPath)) {
            text += hex[c >> 4];
            text += hex[c & 15];
        }
        text += "\r\n";
        text += "LineupMapHex=";
        for (const unsigned char c : std::string(visual.lineups.mapOverride)) {
            text += hex[c >> 4];
            text += hex[c & 15];
        }
        text += "\r\n";
        text += "HitSoundHex=";
        for (const unsigned char c : PortableMediaPath(path, visual.combat.hitSoundPath)) {
            text += hex[c >> 4];
            text += hex[c & 15];
        }
        text += "\r\n";
        for (std::size_t i = 0; i < worldvisuals::DropGroupCount; ++i) {
            text += "\r\n[DropGroup." + std::to_string(i) + "]\r\n";
            AppendFields(text, visual.worldVisuals.dropGroups[i], dropFields);
        }
        for (std::size_t i = 0; i < combat::RecoilProfiles; ++i) {
            text += "\r\n[Recoil." + std::to_string(i ? WeaponIcons[i - 1].id : 0) + "]\r\n";
            AppendFields(text, visual.combat.weapons[i], recoilFields);
        }
        for (std::size_t i = 1; i < visual.trackingProfiles.groups.size(); ++i) {
            text += "\r\n[TrackingGroup." + std::to_string(i) + "]\r\n";
            AppendFields(text, visual.trackingProfiles.groups[i], groupFields);
        }
        for (std::size_t i = 1; i < visual.trackingProfiles.weapons.size(); ++i) {
            text += "\r\n[TrackingWeapon." + std::to_string(WeaponIcons[i - 1].id) + "]\r\n";
            AppendFields(text, visual.trackingProfiles.weapons[i], groupFields);
        }
        text += "\r\n[Cosmetics]\r\n";
        AppendFields(text, visual.cosmetics, cosmeticFields);
        for (std::size_t i = 0; i < cosmetics::WeaponCount; ++i)
            AppendFinish(text, visual.cosmetics.weapons[i], "Finish." + std::to_string(WeaponIcons[i].id));
        AppendFinish(text, visual.cosmetics.knife.finish, "Finish.Knife");
        AppendFinish(text, visual.cosmetics.glove.finish, "Finish.Glove");
        text += "\r\n[CameraTracking]\r\n";
        AppendFields(text, tracking, trackingFields);
        text += "\r\n[Effects]\r\n";
        AppendFields(text, effects, effectFields);
        static std::atomic_uint64_t sequence{};
        const auto temporary =
            path + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++sequence);
        const auto file =
            CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        DWORD written{};
        const bool saved = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                           written == text.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (!saved || !MoveFileExW(temporary.c_str(), path.c_str(),
                                   MOVEFILE_WRITE_THROUGH | (replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0))) {
            DeleteFileW(temporary.c_str());
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}
bool LoadSettings(const std::wstring &path, Configuration &config, VisualOptions &visual,
                  TrackingConfiguration *tracking, EffectsConfiguration *effects) noexcept {
    try {
        if (path.empty())
            return false;
        // Keep a stable read snapshot while the INI API reads individual fields. Writers
        // atomically replace profiles, so they cannot race the same load across versions.
        vortex::Handle profileFile(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                               FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        LARGE_INTEGER fileSize{};
        if (!profileFile || !GetFileSizeEx(profileFile.value, &fileSize) || fileSize.QuadPart <= 0 ||
            fileSize.QuadPart > 512 * 1024 || GetPrivateProfileIntW(L"Overlay", L"Version", 0, path.c_str()) != 1)
            return false;
        Configuration next;
        next.worldUnitsPerMeter = config.worldUnitsPerMeter; // Host geometry scale is not a profile preference.
        VisualOptions nextVisual; // Missing fields get defaults, never a previously loaded profile's values.
        TrackingConfiguration nextTracking;
        EffectsConfiguration nextEffects;
        if (!ReadFields(path, L"Overlay", next, fields) || !ReadFields(path, L"Visual", nextVisual, visuals))
            return false;
        nextVisual.labelBackground = 0; // Retained INI field; labels now use only a small text shadow.
        if (!ReadFields(path, L"CameraTracking", nextTracking, trackingFields) ||
            !ValidTrackingConfiguration(nextTracking))
            return false;
        if (!ReadFields(path, L"Effects", nextEffects, effectFields) || !ValidEffectsConfiguration(nextEffects))
            return false;
        for (std::size_t i = 0; i < worldvisuals::DropGroupCount; ++i) {
            const auto section = L"DropGroup." + std::to_wstring(i);
            if (!ReadFields(path, section.c_str(), nextVisual.worldVisuals.dropGroups[i], dropFields))
                return false;
        }
        for (std::size_t i = 0; i < combat::RecoilProfiles; ++i) {
            const auto section = L"Recoil." + std::to_wstring(i ? WeaponIcons[i - 1].id : 0);
            if (!ReadFields(path, section.c_str(), nextVisual.combat.weapons[i], recoilFields))
                return false;
        }
        for (std::size_t i = 1; i < nextVisual.trackingProfiles.groups.size(); ++i) {
            const auto section = L"TrackingGroup." + std::to_wstring(i);
            if (!ReadFields(path, section.c_str(), nextVisual.trackingProfiles.groups[i], groupFields))
                return false;
        }
        for (std::size_t i = 1; i < nextVisual.trackingProfiles.weapons.size(); ++i) {
            const auto section = L"TrackingWeapon." + std::to_wstring(WeaponIcons[i - 1].id);
            if (!ReadFields(path, section.c_str(), nextVisual.trackingProfiles.weapons[i], groupFields))
                return false;
        }
        if (!ReadFields(path, L"Cosmetics", nextVisual.cosmetics, cosmeticFields))
            return false;
        for (std::size_t i = 0; i < cosmetics::WeaponCount; ++i)
            if (!ReadFinish(path, nextVisual.cosmetics.weapons[i], L"Finish." + std::to_wstring(WeaponIcons[i].id)))
                return false;
        if (!ReadFinish(path, nextVisual.cosmetics.knife.finish, L"Finish.Knife") ||
            !ReadFinish(path, nextVisual.cosmetics.glove.finish, L"Finish.Glove"))
            return false;
        wchar_t hex[521]{};
        const auto length = GetPrivateProfileStringW(L"Overlay", L"FontHex", L"", hex, 521, path.c_str());
        if (length % 2 || length >= 520)
            return false;
        std::memset(next.fontPath, 0, sizeof(next.fontPath));
        const auto digit = [](wchar_t c) {
            return c >= L'0' && c <= L'9' ? c - L'0' : c >= L'A' && c <= L'F' ? c - L'A' + 10 : -1;
        };
        for (unsigned i = 0; i < length; i += 2) {
            const int high = digit(hex[i]), low = digit(hex[i + 1]);
            if (high < 0 || low < 0 || !(high * 16 + low))
                return false;
            next.fontPath[i / 2] = static_cast<char>(high * 16 + low);
        }
        wchar_t mapHex[193]{};
        const auto mapLength = GetPrivateProfileStringW(L"Visual", L"LineupMapHex", L"", mapHex, 193, path.c_str());
        if (mapLength % 2 || mapLength >= 192)
            return false;
        for (unsigned i = 0; i < mapLength; i += 2) {
            const auto high = digit(mapHex[i]), low = digit(mapHex[i + 1]);
            if (high < 0 || low < 0 || !(high * 16 + low))
                return false;
            nextVisual.lineups.mapOverride[i / 2] = static_cast<char>(high * 16 + low);
        }
        wchar_t imageHex[4097]{};
        const auto imageLength =
            GetPrivateProfileStringW(L"Visual", L"BackgroundHex", L"", imageHex, 4097, path.c_str());
        if (imageLength % 2 || imageLength >= 4096)
            return false;
        for (unsigned i = 0; i < imageLength; i += 2) {
            const int high = digit(imageHex[i]), low = digit(imageHex[i + 1]);
            if (high < 0 || low < 0 || !(high * 16 + low))
                return false;
            nextVisual.backgroundPath[i / 2] = static_cast<char>(high * 16 + low);
        }
        if (nextVisual.backgroundPath[0] &&
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, nextVisual.backgroundPath, -1, nullptr, 0))
            return false;
        wchar_t soundHex[4097]{};
        const auto soundLength =
            GetPrivateProfileStringW(L"Visual", L"KillSoundHex", L"", soundHex, 4097, path.c_str());
        if (soundLength % 2 || soundLength >= 4096)
            return false;
        for (unsigned i = 0; i < soundLength; i += 2) {
            const int high = digit(soundHex[i]), low = digit(soundHex[i + 1]);
            if (high < 0 || low < 0 || !(high * 16 + low))
                return false;
            nextVisual.killSoundPath[i / 2] = static_cast<char>(high * 16 + low);
        }
        if (nextVisual.killSoundPath[0] &&
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, nextVisual.killSoundPath, -1, nullptr, 0))
            return false;
        const auto hitLength = GetPrivateProfileStringW(L"Visual", L"HitSoundHex", L"", soundHex, 4097, path.c_str());
        if (hitLength % 2 || hitLength >= 4096)
            return false;
        for (unsigned i = 0; i < hitLength; i += 2) {
            const int hi = digit(soundHex[i]), lo = digit(soundHex[i + 1]);
            if (hi < 0 || lo < 0 || !(hi * 16 + lo))
                return false;
            nextVisual.combat.hitSoundPath[i / 2] = static_cast<char>(hi * 16 + lo);
        }
        if (nextVisual.combat.hitSoundPath[0] &&
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, nextVisual.combat.hitSoundPath, -1, nullptr, 0))
            return false;
        if (GetPrivateProfileIntW(L"Visual", L"motionVersion", 0, path.c_str()) < 2) {
            auto &motion = nextVisual.combat;
            if (std::isfinite(motion.ghostDuration) && motion.ghostDuration >= .06f && motion.ghostDuration <= 2.f)
                motion.ghostDuration = 1.f;
            if (std::isfinite(motion.ghostOpacity) && motion.ghostOpacity >= 0 && motion.ghostOpacity <= 2.f)
                motion.ghostOpacity = std::min(motion.ghostOpacity, 1.f);
        }
        if (!ResolveMediaPath(path, nextVisual.combat.hitSoundPath) || !ResolveMediaPath(path, next.fontPath) ||
            !ResolveMediaPath(path, nextVisual.backgroundPath) || !ResolveMediaPath(path, nextVisual.killSoundPath) ||
            !ValidConfiguration(next) || !ValidVisualOptions(nextVisual))
            return false;
        config = next;
        visual = nextVisual;
        if (tracking)
            *tracking = nextTracking;
        if (effects)
            *effects = UnifiedHighlight(nextEffects);
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace awareness
