# Rendering and input changes in 3.14

## What changed

- **Bullet tracers:** captures the client's tracer-effect callback, including the game's muzzle attachment resolver. Endpoints come from `CEffectData`; pawn and weapon-owner handles resolve the team. Fire/impact events remain a secondary source, without requiring both events to arrive. Duplicate client/event lines are suppressed. The 20-line limit and half-second fade remain. A fine, separately configurable core sits inside the colored halo.
- **Throw preview:** updates when the throw origin, angles, velocity or strength changes, without the old 30 Hz throttle. Unchanged predictions are refreshed every 100 ms for changing collision geometry. Every prediction still uses native hull sweeps. Held previews retain their independent glow toggle, disabled by default.
- **Flight trails:** their live endpoint updates between history samples. Utility discovery visits at most 256 slots per reader per frame, starting at the high end of the table; cached projectile positions update each frame. Large tables therefore take several frames to discover completely instead of causing periodic full-table scans.
- **Timing:** trajectory samples, replay interpolation and fades share a high-resolution monotonic clock. Millisecond uptime is retained only for coarse watchdog deadlines. Duplicate zero-time recoil updates retain their state.
- **Recoil:** compensation now runs before camera setup, rather than after Present. Shot transitions gate the response without comparing the weapon's last-shot timestamp against another clock domain. Failed compare-and-swap writes retain the pending correction; there are at most two attempts per update. Per-weapon profiles and menu/focus/stale-data gates remain.
- **Ghost replay:** keeps ten skeletal samples but renders three interpolated echoes for the nearest four eligible histories. The maximum is 12 poses and 24 draw calls, compared with up to 80 poses and approximately 800 calls previously. The flat ghost pass combines the mesh parts and skips their textures. Old long trails migrate to 0.16 on first load; subsequent saved choices retain their value. The menu offers 0.06–0.5.
- **Model fill:** single-color mode now uses current engine model packets. Model shading reuses the original visible material with a tint. Shaded and opaque solid fills avoid the extra visible model layer; transparent solid fills keep the extra layer to preserve their blending. The original model's animation and unselected packets are preserved. The old native halo remains optional under Soft glow, with its engine-dependent temporal behavior.
- **Awareness:** has its own All, Opponents, Teammates, T and CT filter. It works independently of the Players filter, while retaining the common distance and opacity limits.
- **Molotov areas:** no longer depend on a successful view-hook clock sample. Their filled footprint follows a padded convex boundary of active fire cells, rather than one oversized circle. The former opacity multiplier was removed, and near-plane clipping keeps the fill visible while the camera approaches. The closed luminous edge shares joins at the closing seam.

## Controls

Players > General: Model shading, Fill, visible/hidden colors. Players > Awareness: Players selector. Players > Paths > Bullets: Core, Start, End, Glow and Glow strength. Motion: Trail length and opacity. World: Show areas, Fire and Fire color. Existing switches and profiles are retained; features that were disabled remain disabled.

Settings > Diagnostics reports received shot effects, accepted tracers, recoil sample availability and successful angle updates. These counters distinguish a connected hook from data actually reaching a feature.

## Verification and limits

The Release build is checked with the offline unit/integration suite, hidden DX11 demo, WARP pixel readbacks, actual menu serialization and two complete DLL load/unload cycles. New regression cases cover tracer ownership/serials and deduplication, differing recoil clocks and failed writes, interpolated skeletal poses, independent awareness teams, current trail heads, clipped fire fill and visible material preservation. Native callback entries and the muzzle resolver were verified from the installed build 14181 DLL files on disk.

No live game was controlled or tested for this release. Actual frame-rate gains, native callback coverage and recoil response remain unmeasured in CS2. A tracer can only be drawn when the client supplies its effect or an impact event. Ghosts still use the bundled SAS mesh, rather than copying individual agent skins. Fire boundaries are a convex approximation of active cells, and some volumetric smoke passes can remain. The screenshots are offline render checks, not evidence of live-game operation.
