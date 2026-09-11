# Projectile paths, tracers and camera FOV (3.10)

All controls are under **Players > Paths**. Feature enable switches default off in code; the bundled saved profile enables paths and tracers. All options use the existing Save / Load / Auto save controls. The original public API layouts are unchanged; these options are internal `VisualOptions` fields in the `[Visual]` section of `OverlaySettings.ini`.

Version 3.12 adds independent preview/flight colors and glow, plus bullet endpoint colors and glow controls. Flight and bullet glow start strong; preview glow starts off. Appearance settings live under `paths.*` in `[Visual]`. See [styling details](STYLING-3.12.md).

## Grenades

`grenadePrediction` draws a plain anti-aliased path for the local player's held grenade, with bounce circles and a larger endpoint marker. `grenadeTrails` draws glowing paths from actual projectile positions. HE is orange, smoke grey, flash white and fire red; decoys use blue.

The reader enumerates live entity identities, validates their back-pointers and full handles, checks exact projectile designer names and skips dormant/detonated projectiles. Each of at most 32 trails has a fixed 193-point ring sampled at up to 64 Hz. While flying, it retains the most recent three seconds. After the projectile disappears or detonates, the retained path fades linearly for five seconds. Serial-number changes keep recycled entities separate. Invalid frames, rescan and round/map events clear history.

Prediction runs from the native view-setup callback at up to 30 Hz. Each sweep calls the game's current physics world with a grenade-sized hull, the verified grenade collision mask and the local pawn excluded. Throw strength, player velocity and pitch affect launch. It uses fixed simulation steps, gravity, reflected velocity, collision elasticity, resting detection and utility detonation rules. Missing/invalid collision results suppress the path; a simulation that reaches its time bound without finishing has no endpoint circle.

This is a collision-aware estimate, not the game's full grenade simulation. The current model assumes standard gravity 800 and throw speed 750; custom server physics, moving obstacles, future player movement, breakable surfaces, sub-tick release timing and some incendiary behavior can change the eventual result. The endpoint can be an airborne detonation point for timed utility. No live throw-to-prediction accuracy claim has been established for this release.

## Bullet tracers

`bulletTracers` enables yellow-to-white lines. `tracerTeams` selects Local, All, Opponents, Teammates, T or CT (ordinals 0–5). This is independent of player HUD and follow-camera filters.

The client game-event callback records the firing player's eye position on `weapon_fire`, resolves the full pawn handle and team through the game's event/controller accessor, then pairs `bullet_impact` positions with that fire record. Multiple pellet or penetration impacts can share one origin. Unmatched impacts are skipped, not replaced with a forward ray. The event pairing window is 350 ms. Only events the client receives can be drawn; network delivery and visibility can limit other-player coverage. Eye positions are sampled at event receipt, so remote interpolation can slightly displace their starting point.

Storage is fixed at twenty lines; oldest entries are replaced. Each fades linearly over 0.5 seconds. Event capture and rendering exchange data under a short lock. Drawing never calls ImGui from the game-event callback.

## Camera FOV

`cameraFovEnabled` and `cameraFov` control the actual world camera (60–140). They do not change the follow-camera selection limit. The hook runs after the native view setup and before its caller creates the projection matrix. It interpolates from the current camera FOV to the selected value, including a smooth return to the game's current value when disabled. While enabled, it also overrides a scoped camera's world FOV. Viewmodel FOV is unchanged.

Minimizing, stale data or the master drawing toggle stops the override. Original view setup continues every frame; no permanent camera-service value is changed. Native hooks must be removed successfully before unloading, with the same quiesced-thread lifecycle contract as the other hooks. Include threads entering view setup and client game-event dispatch in that contract.

## Files and verification

- `include/awareness/Trajectories.hpp`: fixed histories, filtering, prediction and smoothing.
- `src/trajectory_reader.hpp`: schema-based grenade and held-utility reads.
- `src/trajectory_native.cpp`: verified view, collision and event adapters.
- `src/trajectory_draw.hpp`: clipped ImDrawList rendering.
- `Updated Offsets/trajectory-layout.json`: build-specific addresses, entry bytes and local binary provenance; generated into `src/cs2_offsets.hpp`.

The native layout is pinned to the locally inspected build 14181. A schema refresh alone cannot reverify the callbacks. The offline tests cover floors, walls, throw strength, detonation, failed collision queries, storage limits, expiry, filters, schema reads, clipping and FOV restoration. The real DLL's GUI is tested for toggles and profile save/load. The September 11 read-only CS2 sample verified an active physics-world pointer, FOV 90 and successful projectile enumeration with ten pawn records; no utility was held or in flight. It did not execute the new native hooks or verify live shot impacts. Standalone demo mode tests these controls and rendering helpers; it does not provide native CS2 events or map collision data.
