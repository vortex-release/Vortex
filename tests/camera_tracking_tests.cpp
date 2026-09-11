#include <awareness/CameraTracking.hpp>
#include <array>
#include <cstdio>
#include <limits>
using awareness::Vector3;
using namespace awareness::camera;
namespace {
int checks{}, failures{};
void Check(bool value, const char *message) {
    ++checks;
    if (!value) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}
bool Near(float a, float b, float epsilon = .001f) {
    return std::abs(a - b) < epsilon;
}
Vector3 Point(Angles angles) {
    const auto v = Forward(angles);
    return {v.x * 100.f, v.y * 100.f, v.z * 100.f};
}
} // namespace
int main() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    auto look = LookAt({}, {0, 0, 10});
    Check(look && Near(look->pitch, 0) && Near(look->yaw, 0), "+Z forward");
    look = LookAt({}, {10, 0, 0});
    Check(look && Near(look->yaw, 90), "+X right");
    look = LookAt({}, {-10, 0, 0});
    Check(look && Near(look->yaw, -90), "-X left");
    look = LookAt({}, {0, 0, -10});
    Check(look && Near(look->yaw, -180), "rear direction wraps to -180");
    look = LookAt({2, 3, 4}, {12, 3, 14});
    Check(look && Near(look->yaw, 45) && Near(look->pitch, 0), "origin-relative atan2");
    look = LookAt({}, {0, 10, 10});
    Check(look && Near(look->pitch, 45), "positive pitch is upward");
    look = LookAt({}, {0, 10, 0}, 72.f);
    Check(look && Near(look->pitch, 89) && Near(look->yaw, 72), "vertical pole preserves yaw and clamps pitch");
    look = LookAt({}, {0, -10, 0});
    Check(look && Near(look->pitch, -89), "lower pitch bound");
    Check(!LookAt({}, {}), "coincident points rejected");
    Check(!LookAt({nan, 0, 0}, {1, 2, 3}) && !LookAt({}, {inf, 0, 0}), "nonfinite positions rejected");
    const float largest = std::numeric_limits<float>::max();
    look = LookAt({-largest, 0, 0}, {largest, 0, 0});
    Check(look && Near(look->yaw, 90), "finite extreme position subtraction does not overflow");
    Check(Near(WrapYaw(721), 1) && Near(WrapYaw(-541), 179), "multi-turn yaw normalization");
    const auto midpoint = LerpAngles({0, 179}, {20, -179}, .5f);
    Check(Near(midpoint.pitch, 10) && Near(midpoint.yaw, -180), "shortest yaw path crosses wrap boundary");
    Check(Near(LerpAngles({}, {40, 80}, -1).yaw, 0) && Near(LerpAngles({}, {40, 80}, 2).yaw, 80),
          "lerp factor clamped");
    Check(Near(LerpAngles({3, 7}, {nan, 0}, .5f).yaw, 7), "invalid destination cannot poison camera");
    Check(Near(AngularDistance({}, {0, 0, 10}, {}), 0) && Near(AngularDistance({}, {10, 0, 0}, {}), 90),
          "geometric FOV angle");
    std::array<Target, 7> entities{{{1, 1, 100, {0, 0, 10}},             // teammate
                                    {2, 2, 0, {0, 0, 10}},               // dead
                                    {3, 2, 100, {0, 0, 10}},             // self
                                    {4, 2, 100, {0, 0, 10}, true, true}, // dormant
                                    {5, 2, 100, {0, 0, 10}, false},      // invalid
                                    {6, 2, 100, Point({0, 20})},
                                    {7, 2, 100, Point({0, 10})}}};
    auto selected = SelectTarget({}, {}, entities, 3, 1, 30);
    Check(selected && selected->id == 7,
          "filters dead, teammate, self, dormant and invalid targets; nearest angle wins");
    Check(!SelectTarget({}, {}, entities, 3, 1, 5), "targets beyond FOV ignored");
    Check(!SelectTarget({}, {}, {}, 3, 1, 30), "empty array");
    auto teamTargets = entities;
    selected = SelectTarget({}, {}, teamTargets, 3, 1, 30, TargetTeams::AllTeams);
    Check(selected && selected->id == 1, "all teams includes a closer teammate");
    teamTargets[0].health = 0;
    selected = SelectTarget({}, {}, teamTargets, 3, 1, 30, TargetTeams::AllTeams);
    Check(selected && selected->id == 7, "all teams still excludes dead, self, dormant and invalid targets");
    Check(!SelectTarget({}, {}, teamTargets, 3, 1, 5, TargetTeams::AllTeams), "all teams still respects FOV");
    Check(!SelectTarget({}, {}, teamTargets, 3, 1, 30, static_cast<TargetTeams>(2)), "invalid team mode rejected");
    teamTargets[0].health = 100;
    Settings teamSettings{true, 30, InstantFollowSpeed, TargetTeams::AllTeams};
    Angles teamAngles{};
    selected = Update({}, teamAngles, teamTargets, 3, 1, teamSettings, .01f, true);
    Check(selected && selected->id == 1, "update forwards all-team selection");
    teamSettings.teams = TargetTeams::Opponents;
    selected = Update({}, teamAngles, teamTargets, 3, 1, teamSettings, .01f, true);
    Check(selected && selected->id == 7, "changing to opponents takes effect on the next update");
    entities[6].health = nan;
    selected = SelectTarget({}, {}, entities, 3, 1, 30);
    Check(selected && selected->id == 6, "nonfinite health ignored");
    entities[6].health = 100;
    entities[6].position = {nan, 0, 10};
    selected = SelectTarget({}, {}, entities, 3, 1, 30);
    Check(selected && selected->id == 6, "nonfinite target ignored");
    entities[6].position = entities[5].position;
    selected = SelectTarget({}, {}, entities, 3, 1, 30);
    Check(selected && selected->id == 6, "equal angular distance uses stable ID");
    Check(!SelectTarget({}, {}, entities, 3, 1, nan), "invalid FOV rejected");
    std::array<Target, 1> target{{{10, 2, 100, Point({20, 40})}}};
    Settings settings{true, 90, 8};
    Angles angles{};
    Check(!Update({}, angles, target, 99, 1, settings, 1.f / 60, false) && angles.yaw == 0,
          "hotkey release freezes camera");
    settings.enabled = false;
    Check(!Update({}, angles, target, 99, 1, settings, 1.f / 60, true) && angles.yaw == 0,
          "disabled tracking freezes camera");
    settings.enabled = true;
    Check(!Update({}, angles, target, 99, 1, settings, 0, true) &&
              !Update({}, angles, target, 99, 1, settings, nan, true),
          "invalid frame delta rejected");
    settings.interpolationSpeed = 0;
    Check(!Update({}, angles, target, 99, 1, settings, 1, true) && angles.yaw == 0, "zero speed pauses");
    settings.interpolationSpeed = -1;
    Check(!Update({}, angles, target, 99, 1, settings, 1, true), "negative speed rejected");
    settings.interpolationSpeed = inf;
    Check(!Update({}, angles, target, 99, 1, settings, 1, true), "nonfinite speed rejected");
    settings.interpolationSpeed = 8;
    selected = Update({}, angles, target, 99, 1, settings, 1.f / 60, true);
    Check(selected && angles.yaw > 0 && angles.yaw < 40 && angles.pitch > 0 && angles.pitch < 20,
          "held input glides without overshooting");
    const auto atRelease = angles;
    Update({}, angles, target, 99, 1, settings, .5f, false);
    Check(angles.yaw == atRelease.yaw && angles.pitch == atRelease.pitch,
          "releasing after tracking freezes immediately");
    const auto advance = [&](int fps) {
        Angles result{};
        for (int i = 0; i < fps; ++i)
            Update({}, result, target, 99, 1, settings, 1.f / fps, true);
        return result;
    };
    const auto at30 = advance(30), at60 = advance(60), at144 = advance(144);
    Check(Near(at30.yaw, at60.yaw) && Near(at30.pitch, at144.pitch) && Near(at30.yaw, at144.yaw),
          "same elapsed time at 30/60/144 FPS has the same response");
    target[0].position = Point({0, -179});
    angles = {0, 179};
    settings.fovDegrees = 3;
    selected = Update({}, angles, target, 99, 1, settings, .1f, true);
    Check(selected && std::abs(angles.yaw) > 179, "FOV and tracking work across yaw seam");
    target[0].position = Point({0, 45});
    settings.fovDegrees = 90;
    angles = {};
    Update({}, angles, target, 99, 1, settings, .1f, true);
    const float first = angles.yaw;
    target[0].position = Point({0, -45});
    Update({}, angles, target, 99, 1, settings, .1f, true);
    Check(angles.yaw < first, "moving target direction updates on the next frame");
    target[0].health = 0;
    const auto beforeDeath = angles;
    Check(!Update({}, angles, target, 99, 1, settings, .1f, true) && angles.yaw == beforeDeath.yaw,
          "target death immediately stops tracking");
    target[0].health = 100;
    target[0].position = Point({20, 40});
    settings.interpolationSpeed = InstantFollowSpeed;
    for (float delta : {1.f / 30, 1.f / 144, 1.f / 1000}) {
        angles = {};
        Check(Update({}, angles, target, 99, 1, settings, delta, true) && Near(angles.pitch, 20) &&
                  Near(angles.yaw, 40),
              "instant endpoint reaches the target in one frame at any frame rate");
    }
    angles = {};
    Check(!Update({}, angles, target, 99, 1, settings, .01f, false) && angles.yaw == 0,
          "instant mode still requires its hotkey");
    settings.fovDegrees = 5;
    Check(!Update({}, angles, target, 99, 1, settings, .01f, true) && angles.yaw == 0,
          "instant mode still honors the FOV filter");
    std::printf("%d camera tracking checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
