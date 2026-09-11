# Vortex 3.16

The launcher and in-game menu share a charcoal, violet, and white design. The photo background is disabled and no longer decoded during rendering. The menu has seven sections: Overview, Players, Aim & recoil, Trajectories, World, Feedback, and Settings. Camera FOV belongs to Aim & recoil.

## Profiles

- **Signature:** based on the existing personal setup: names, health, weapon icons, white model effects, violet paths with white cores, tracking, recoil, objective and hit feedback.
- **Focus:** fewer labels and effects, opponents only, local bullet paths, and no model fill or glow.
- **Broadcast:** both teams, distance and damage information, visible/hidden model colors, tracking and recoil disabled.

All three use one accent plus white. Presets cover major modules and retain personal hotkeys, fonts, sounds, and coordinate units. Undo restores the preceding configuration. Palette-only changes keep feature selections. Save and Auto-save work across all pages.

The existing personal profile was backed up before palette cleanup. Recoil gains were reset to a calibrated 1:1 baseline with 25 ms smoothing for the new input backend; weapon selections and bindings remain available for adjustment. Public preset files contain no personal absolute font or sound paths.

## Tracers

The old hook intercepted only the legacy Tracer effect. Read-only inspection of the installed build found ParticleTracer plus a firearm path that directly creates particles without calling either legacy effect callback. The new hooks cover all three independently and preserve the original calls.

The direct path receives actual start/end vectors from queued shot records. It verifies the bullet-service parent pawn, service back-reference, weapon owner and full entity handles before accepting a segment. It does not assume that m_vecBulletStart or m_vecBulletEnd exist; those names are absent from the bundled schema. The m_iShotsFired counter and event fallback provide additional diagnostics.

The tracer ring now retains up to 128 segments. Near-plane clipping remains supported. A shot projected almost directly along the viewing direction draws a small impact marker instead of disappearing as a zero-length line. Diagnostics show hook availability, direct and particle calls, accepted/rejected segments, and projected/clipped counts.

## Recoil

Weapon identity follows the active weapon handle and item definition index. The reader verifies entity generations and obtains evaluated recoil punch from the supported engine accessor. View punch is not blindly added a second time.

Windows SendInput is the default backend. It converts angle correction using actual mouse sensitivity and configurable m_yaw/m_pitch values, retains fractional mouse counts, and resets residual input across weapon, owner, firing-sequence and backend changes. A nonzero pawn sensitivity override already includes zoom; it must not be multiplied by the FOV adjustment twice. The existing native-angle backend remains selectable.

Input is gated by game foreground focus and menu state. Diagnostics expose the weapon, punch angles, read failures and successful input updates. No driver is installed.

## Performance

A worker owns entity reading and publishes complete snapshots approximately every 15 ms. Present copies a published snapshot and refreshes the camera projection, keeping camera movement current without re-scanning the entity list. The worker pauses when rendering stops requesting updates, discards invalidated in-flight samples, and joins before source teardown.

Engine materials were already cached; that cache is retained. Model target selection moved to the worker and selection storage is reused through a small pool. ImGui owns and reuses its draw buffers, with capacity reserved for normal frames. Animation smoothing uses elapsed time.

The simple geometry-fill path uses its own cached stencil resource to prevent overlapping faces from accumulating alpha. It never clears or writes the host's depth/stencil resource. Visibility-dependent and glow rendering retain their required passes. This is a specific optimization, not a claim that every rendering mode can use one stencil pass.

## Verification and remaining limits

The release passes 35 CTest checks: blocked-worker publication/invalidation, recoil input accumulation and error handling, weapon/sensitivity/direct-shot memory fixtures, full presets, tracer clipping, stencil reuse and host depth preservation, real D3D11 readback, interactive menu save/load/presets, repeated DLL loading/unloading, launcher, loader, profile migration, offset generation, and local server checks.

The installed client.dll was inspected without modifying it. Its PE timestamp is 1788980830 and image size is 41803776, corresponding to the bundled build 14181. Hook entry bytes and layout provenance are recorded in Updated Offsets/trajectory-layout.json. These guards deliberately reject a mismatched build.

No live CS2 shooting session or before/after game FPS benchmark was run. Hook ABI inspection and controlled fixtures cannot establish that every weapon or server mode behaves identically. In Settings > Diagnostics, use the counters and %LOCALAPPDATA%\Vortex\logs after testing a short burst. Positive direct-shot/accepted counts with no projected lines isolates a projection/filter issue; zero hook calls isolates capture or build compatibility. Recoil diagnostics distinguish reading from input failure.

## Downloads and updates

The installer is in Desktop\Vortex. The local server serves the release from http://127.0.0.1:17843/. The app remains a local preview with an empty public update URL. Follow VORTEX-RELEASES.md to connect a stable HTTPS hostname, then package a higher version with that address before distributing it to other computers.

Implementation references: [Microsoft D3D11 depth/stencil documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-depth-stencil) and [Dear ImGui's D3D11 backend](https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_dx11.cpp).
