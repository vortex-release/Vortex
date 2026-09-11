# DLL camera tracking — version 3.9

Load the updated DLL through the existing host. Open **Insert -> Tracking**, enable tracking, choose a target node and FOV/speed, close the menu with Insert, then hold the selected key (right mouse by default). The DLL's Present path filters targets and computes smoothed angles. Release the key to stop. Tracking pauses with the menu open, when minimized/unfocused in CS2, or without valid current data. Settings persist through the existing Save/Load/Reset controls; older profiles default tracking to disabled.

The **CS2 integration applies native view angles**. The authoritative build-14181 snapshot supplies `dwViewAngles`, one of the 39 consumed global/schema values including the skeletal model-state field. It is imported and checked against the matching HPP export. The reader's verified build, ready frame, matching module bounds, readable finite angles and writable aligned storage are required. Pitch/yaw are committed as one compare-and-exchange; roll is preserved, and an intervening mouse/host angle update causes a retry on the next Present. No page protections are changed. This path is compiled into EntityAwarenessOverlay.dll. Native camera writes are covered by fixtures. The 3.6 live check was read-only and verified bone sampling, not camera writes.

The **normal ObserverDemo.exe uses the DLL camera loop**, retrieving output after Present and applying it to the next view matrix. Four optional C-linkage exports are added in `TrackingApi.hpp`: `AwarenessSetTrackingConfiguration`, `AwarenessGetTrackingConfiguration`, `AwarenessSubmitCameraInput`, and `AwarenessGetTrackingState`. The non-frame public structs retain their layouts. FrameSnapshot version 3 appends skeletal node data; version-1/version-2 frames remain accepted for HUD rendering with absent nodes cleared. These calls follow the same serialized lifetime/unload contract as the original exports.

Hosts submit a version-3 `FrameSnapshot` with valid world-space skeletal nodes, then a `CameraInput` containing their current portable angles and hold-key state before that frame's Present. The DLL pairs the input with the frame revision. New frames without matching camera input cannot reuse a previously held key. After Present, an active `TrackingState` supplies the angles for the host's next view. The host owns its focus/key policy. Host-supplied camera input is rejected in automatic CS2 mode; that mode always reads its own camera and actual selected key.

**Camera Tracking.cmd / ObserverDemo.exe --camera** retains the earlier portable example with its own ImGui tab and simulated camera. Its settings remain per session. The reusable module and snippet below remain available for other applications.

## Bind selection

Tracking > Bind > Set bind captures the next keyboard key, mouse button or wheel direction. Already-held input and the initiating click are ignored. Bindings save to the existing hotkey field. Insert/Home remain assignable through Ctrl-modified menu/drawing shortcuts. See [sound and binding details](SOUND-AND-BINDS.md).

## Target node selection

The DLL resolves the selected skeletal position instead of the collision midpoint. The `g_ActiveTargetBone` integer is the ImGui dropdown ordinal (0..3); `TargetBones` maps it to the engine ID in `TargetBone`. The profile stores `[Visual] activeTargetBone=0..3` in OverlaySettings.ini. Existing profiles without the key default to Head. Missing, invalid or stale-layout nodes are skipped without hiding the entity HUD. See [TARGET-NODES.md](TARGET-NODES.md) for verified IDs, reader layout and host examples.

## Team selection

**Tracking > Teams** offers **Opponents** (default) and **All teams**. This controls camera selection independently of the Players/HUD team filter. All teams includes teammates while still excluding self, dead, dormant, invalid, out-of-FOV and missing-bone targets; CS2 spectator teams remain excluded.

The portable module exposes `camera::Settings::teams` (`camera::TargetTeams`). The DLL stores its menu choice as `[Visual] trackingTeams=0` for Opponents or `1` for All teams, alongside the target-node choice. Save/Load/Auto save include this value; missing keys default to Opponents and invalid values reject the profile atomically. The existing exported `TrackingConfiguration` binary layout is unchanged.

The DLL smoke test changes the actual Teams dropdown, verifies both opponent and teammate camera output, then checks automatic saving and GUI Load. Unit tests also cover self/dead/invalid/dormant exclusions, FOV, invalid modes and CS2 spectator rejection.

## Modules

- `include/awareness/Math.hpp`: the existing `Vector3` structure, with x/y/z, addition and subtraction.
- `include/awareness/CameraTracking.hpp`: portable C++20 angles, atan2 look-at, angular target selection, shortest-arc lerp and frame-time-adjusted update. No Windows, ImGui or engine dependencies.
- `include/awareness/CameraTrackingImGui.hpp`: the ImGui tab and optional Win32 hold-key adapter.
- `demo/camera_tracking.hpp`: a complete integration with the demo's D3D11 host, simulated snapshots, projected entities and camera matrix.
- `tests/camera_tracking_tests.cpp`: math, team filtering and update checks.
- `include/awareness/TrackingApi.hpp`: independent versioned camera configuration, input, output and exports.
- `include/awareness/TargetBone.hpp`: verified named bone IDs, snapshot node storage and resolution.
- `src/targeting.hpp`: DLL-owned dropdown selection and enum mapping.
- `src/camera_control.hpp`: DLL snapshot adapter, selected-node resolution and CS2 coordinate conversion.
- `src/cs2_camera.hpp`: validated native angle reads and guarded pitch/yaw commits.
- `tests/cs2_camera_tests.cpp`: native storage, coordinate, selected-node and unavailable-source checks.

## Coordinate and timing contract

The example uses the demo's **Y-up, +Z-forward, +X-right** coordinates. Angles are in degrees. Positive pitch looks up and positive yaw looks right. Pitch is clamped to [-89, 89] to avoid vertical look-at singularities; yaw wraps to [-180, 180). Exact vertical targets preserve the current yaw. Coincident and nonfinite points are rejected. Convert coordinates and angle conventions explicitly when adapting a different engine.

FOV is the maximum angular deviation from the current view axis: a **cone half-angle**, not the projection's camera FOV. The GUI offers 1–90 degrees; the reusable selector accepts and clamps finite values to 0–180 degrees. Targets are selected by their actual 3D angular separation. Dead, self, invalid and dormant entries are skipped in both modes. Opponents also excludes same-team entries; All teams includes teammates. Equal angular distances use a stable ID tie-breaker.

`LerpAngles` linearly interpolates pitch and the shortest yaw arc. `Update` uses `alpha = 1 - exp(-speed * deltaSeconds)` to get equivalent smoothing at different frame rates for a stationary target. Speed is an inverse-seconds response rate, not degrees per frame. The GUI/API range is 0�200. A speed of zero pauses; 200 (camera::InstantFollowSpeed) uses alpha=1 for an immediate eligible update. Speeds below 200 keep their existing response. Invalid time/speed or released input leaves the camera unchanged. The interactive demo caps a single frame delta to 0.1 seconds after long stalls; the portable core uses the caller's delta directly.

`ActivationHeld` checks the foreground window, ImGui input capture, the mouse's presence in the window client area, and the high bit of `GetAsyncKeyState(VK_RBUTTON)`. Pass a different virtual-key code to choose another hold key. Call after `ImGui::NewFrame()`. Keep UI and camera updates on their owning frame thread.

## Minimal host integration

Include paths point at this project's `include` directory and ImGui headers. The host owns persistent settings/angles and supplies world-space target points. The `Vector3` type already exists; do not redefine it in the same namespace.

```cpp
#include <awareness/CameraTrackingImGui.hpp>
#include <array>

namespace tracking = awareness::camera;

// Persistent host state, initialized once:
tracking::Settings settings{true, 45.f, 8.f};
tracking::Angles view{};
awareness::Vector3 cameraOrigin{0.f, 2.f, -8.f};
std::array<tracking::Target, 3> entities{{
    {1, 1, 100.f, {0.f, 1.f, 10.f}}, // Same team: ignored.
    {2, 2,   0.f, {2.f, 1.f, 12.f}}, // Dead: ignored.
    {3, 2, 100.f, {4.f, 1.f, 15.f}}  // Live opposing target.
}};

void DrawAndUpdate(HWND window, float deltaSeconds) {
    // Host already called ImGui::NewFrame().
    if (ImGui::Begin("Camera controls")) {
        if (ImGui::BeginTabBar("Camera tabs")) {
            tracking::DrawTrackingTab(settings);
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    tracking::Update(cameraOrigin, view, entities,
                     99u, 1, // Local entity ID and team.
                     settings, deltaSeconds,
                     tracking::ActivationHeld(window, VK_RBUTTON));

    const awareness::Vector3 forward = tracking::Forward(view);
    // Apply cameraOrigin and forward to YOUR camera/view matrix.
    // For DirectXMath: XMMatrixLookToLH(origin, forward, {0,1,0}).
    // Then draw the scene and finish the host's ImGui frame.
}
```

For another platform or input system, call the portable `Update` directly with your own `bool activationHeld`. A false activation flag prevents selection/interpolation; the menu can still render and accept settings changes.

## Verification

The normal build runs 21 CTest suites. The portable camera suite checks atan2 quadrants, vertical targets, float extremes, yaw seams, filtering, FOV, hold/release, moving/dead targets and matching elapsed-time response at 30/60/144 FPS. Native camera tests verify coordinate conversion, pitch/yaw writes, roll preservation, compare-and-exchange conflict rejection, protected/freed memory and unavailable-source gating. The DLL smoke tests the real target-node dropdown, Head/Pelvis camera output, automatic selection saving and GUI Load, plus the DLL-owned camera checkbox, exported configuration/input/output, tracking through Present, immediate release, menu pause and mismatched camera/frame input. It also retains graphics-state preservation and GPU readback. Smoke activation is explicitly supplied by the demo host; it does not synthesize physical mouse input or verify a live CS2 session.

`ObserverDemo.exe --camera-smoke` runs the portable example's WARP check. `ObserverDemo.exe --smoke` tests the actual DLL, including its camera integration, and saves `observer-camera.bmp` and `smoke-test.log` beside the executable. Normal builds keep these artifacts in the build cache.
