#include "cs2_camera.hpp"
#include "camera_control.hpp"
#include "cs2_source.hpp"
#include <cstdio>
#include <limits>
using namespace awareness;
namespace {
int checks{}, failures{};
void Check(bool condition, const char *message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}
bool Near(float a, float b) {
    return std::abs(a - b) < .001f;
}
} // namespace
int main() {
    cs2::Source uninitializedSource;
    cs2::NativeViewAngles unverified;
    Check(!uninitializedSource.ReadCamera(unverified) &&
              uninitializedSource.ApplyCamera({}, {}) == HRESULT_FROM_WIN32(ERROR_NOT_READY),
          "unverified/uninitialized game source cannot read or write the camera");
    SYSTEM_INFO system;
    GetSystemInfo(&system);
    auto *bytes = static_cast<unsigned char *>(
        VirtualAlloc(nullptr, system.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!bytes)
        return 1;
    auto *angles = reinterpret_cast<cs2::NativeViewAngles *>(bytes);
    *angles = {10, 20, 3};
    const auto address = reinterpret_cast<std::uintptr_t>(angles);
    cs2::NativeViewAngles sample;
    Check(cs2::ReadViewAngles(address, sample) && sample.yaw == 20, "read native angle tuple");
    const auto converted = cs2::TrackingAngles(sample);
    Check(converted.pitch == -10 && converted.yaw == 20, "CS2 pitch sign conversion");
    Check(cs2::CommitViewAngles(address, sample, {15, 30}) == S_OK, "commit camera pitch/yaw");
    Check(angles->pitch == -15 && angles->yaw == 30 && angles->roll == 3, "native coordinate output preserves roll");
    Check(cs2::CommitViewAngles(address, sample, {5, 40}) == HRESULT_FROM_WIN32(ERROR_RETRY) && angles->yaw == 30,
          "intervening angle change is never overwritten");
    sample = *angles;
    Check(cs2::CommitViewAngles(address, sample, {120, 541}) == S_OK && angles->pitch == -89 && angles->yaw == -179,
          "native writes clamp pitch and normalize yaw");
    Check(cs2::CommitViewAngles(address + 4, *angles, {0, 0}) == E_INVALIDARG, "unaligned camera storage rejected");
    Check(cs2::CommitViewAngles(0, *angles, {0, 0}) == E_INVALIDARG, "null camera storage rejected");
    Check(cs2::CommitViewAngles(address, *angles, {std::numeric_limits<float>::quiet_NaN(), 0}) == E_INVALIDARG,
          "nonfinite output rejected");
    DWORD old{};
    VirtualProtect(bytes, system.dwPageSize, PAGE_READONLY, &old);
    Check(cs2::CommitViewAngles(address, *angles, {0, 0}) == E_ACCESSDENIED, "read-only memory stays unchanged");
    VirtualProtect(bytes, system.dwPageSize, PAGE_READWRITE | PAGE_GUARD, &old);
    Check(cs2::CommitViewAngles(address, sample, {0, 0}) == E_ACCESSDENIED, "guarded memory not touched");
    MEMORY_BASIC_INFORMATION info{};
    VirtualQuery(bytes, &info, sizeof(info));
    Check((info.Protect & PAGE_GUARD) != 0, "write rejection preserves guard protection");
    VirtualProtect(bytes, system.dwPageSize, PAGE_NOACCESS, &old);
    Check(!cs2::ReadViewAngles(address, sample) && cs2::CommitViewAngles(address, sample, {0, 0}) == E_ACCESSDENIED,
          "inaccessible angle memory rejected");
    VirtualProtect(bytes, system.dwPageSize, PAGE_READWRITE, &old);
    *angles = {200, 0, 0};
    Check(!cs2::ReadViewAngles(address, sample), "implausible pitch is not accepted as camera data");
    VirtualFree(bytes, 0, MEM_RELEASE);
    Check(cs2::CommitViewAngles(address, sample, {0, 0}) == E_ACCESSDENIED, "freed memory rejected");
    FrameSnapshot frame;
    frame.localTeam = 2;
    frame.localEntityId = 99;
    frame.entityCount = 3;
    frame.cameraOrigin = {0, 0, 5};
    frame.entities[0].id = 1;
    frame.entities[0].team = 2;
    frame.entities[0].health = 100;
    frame.entities[0].valid = 1;
    frame.entities[0].origin = {100, 0, 5};
    frame.entities[1] = frame.entities[0];
    frame.entities[1].id = 2;
    frame.entities[1].team = 3;
    frame.entities[1].origin = {100, 20, 15};
    frame.entities[2] = frame.entities[1];
    frame.entities[2].id = 3;
    frame.entities[2].health = 0;
    frame.entities[2].origin = {100, 0, 5};
    for (unsigned i = 0; i < frame.entityCount; ++i) {
        frame.bones[i].positions[static_cast<int>(TargetBone::Head)] = frame.entities[i].origin;
        frame.bones[i].validMask = 1u << static_cast<int>(TargetBone::Head);
    }
    TrackingConfiguration config;
    config.enabled = 1;
    config.fovDegrees = 45;
    camera::Angles view{};
    const auto result = TrackFrame(frame, view, config, .1f, true, true);
    Check(result && result->id == 2 && view.pitch > 0 && view.yaw > 0,
          "CS2 coordinate adapter follows live opponent above/right");
    Check(Near(result ? result->desired.yaw : 0, 11.309933f), "CS2 target yaw uses X/Y ground plane");
    const auto checkTeam = [&](std::uint32_t expected) {
        camera::Angles teamView{};
        const auto selected =
            TrackFrame(frame, teamView, config, .1f, true, true, TargetBone::Head, camera::TargetTeams::AllTeams);
        return selected && selected->id == expected;
    };
    Check(checkTeam(1), "CS2 adapter includes teammates in all-team mode");
    frame.entities[0].id = frame.localEntityId;
    Check(checkTeam(2), "CS2 all-team mode excludes the local player");
    frame.entities[0].id = 1;
    frame.entities[0].team = 1;
    Check(checkTeam(2), "CS2 all-team mode still excludes spectator teams");
    frame.entities[0].team = 2;
    frame.bones[1].positions[static_cast<int>(TargetBone::Pelvis)] = {100, 20, 2};
    frame.bones[1].validMask |= 1u << static_cast<int>(TargetBone::Pelvis);
    camera::Angles pelvisView{};
    const auto pelvis = TrackFrame(frame, pelvisView, config, .1f, true, true, TargetBone::Pelvis);
    Check(pelvis && pelvis->id == 2 && pelvisView.pitch < 0,
          "selecting pelvis changes aim to that bone instead of head or root");
    Check(!TrackFrame(frame, pelvisView, config, .1f, true, true, TargetBone::Neck),
          "missing selected node never silently falls back to root");
    const auto before = view;
    Check(!TrackFrame(frame, view, config, .1f, false, true) && view.yaw == before.yaw,
          "adapter respects activation release");
    frame.entityCount = MaxEntities + 1;
    Check(!TrackFrame(frame, view, config, .1f, true, true), "adapter rejects oversized frames");
    config.hotkey = VK_INSERT;
    Check(ValidTrackingConfiguration(config), "Insert is bindable with Ctrl+Insert preserving menu access");
    config.hotkey=binding::WheelDown;
    Check(ValidTrackingConfiguration(config), "wheel bindings are accepted");
    config.hotkey=260;
    Check(!ValidTrackingConfiguration(config), "unknown binding codes are rejected");
    std::printf("%d native camera/adapter checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
