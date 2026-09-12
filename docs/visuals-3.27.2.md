# Vortex 3.27.2 visual correction

This change addresses missing bullet/utility paths, absent fire footprints, unstable chams and a missing player-skeleton option. Work is offline: no game launch, attachment, input or screen inspection is performed. The available saved game-data directories currently contain fixture logs, so they do not establish which live rendering path failed for the user.

## Player skeleton

Players > General includes a Skeleton toggle. Players > Style includes player/custom color, outline, joint dots, opacity and line width. Existing profiles default it off; saving, loading and importing/exporting profiles use the existing validated configuration path. Team and distance filters follow the player overlay.

The worker reads one bounded block of 23 rig joints for each eligible player when skeleton drawing is enabled. It validates the bone buffer/count before and after the read, checks finite transforms, and verifies full entity identity and scene ownership around the sample. Invalid joints omit affected links. Positions are published alongside the same player snapshot; drawing performs no bone memory reads and keeps the public FrameSnapshot ABI unchanged.

Eighteen rig links are clipped to the camera near plane and viewport. Dead/dormant/local/filtered players are excluded, and implausibly long or missing limb endpoints cannot produce screen-spanning X lines. The common rig uses the same verified SAS/Phoenix layout as the existing player-model preview; unrelated custom rigs are not inferred from arbitrary bone indices.

## Fire areas

Inferno start events may precede the available burning-cell array. Such event records are explicitly marked as estimated footprints and now generate depth-tested geometry instead of being discarded. Exact burning cells replace that estimate when available. Estimated geometry does not authorize hiding the original fire particles. The area renderer reports actual geometry/vertex/cell counts, rejected records, approximate footprints and its render result in diagnostics and queued logs.

## Shared depth and trajectories

Three independent capture failures were reproduced in the native GPU fixture: host predication suppressing the scene-depth copy, redundant initial depth clears freezing an otherwise eligible candidate, and depth-only world prepasses being ignored because no color target was bound. The fixture had five failing assertions before the fixes and passes afterward. See `render-depth-recovery-3.27.2.md` for the precise constraints and retained limitations.

Short gaps in asynchronous projectile samples retain trail history and briefly retry known full entity handles without publishing stale positions. A different handle/type or sustained absence starts a new history. Held-grenade reads validate weapon generation, finite throw state and active-weapon consistency. Finite yaw is normalized so rotating through a full turn cannot disable the preview. Prediction status and attempt/failure counters distinguish input or native tracing failures from rendering-depth failures, including when bullet tracers are disabled.

Scene-depth availability remains required for correct wall occlusion; hidden bullet tracers are not substituted with an always-visible 2D line. These fixes address reproducible shared-depth causes of missing/unstable chams. They do not recover player geometry the engine never submits or establish that every live artifact has been reproduced.

## Validation and delivery

Focused fixtures cover bone-array replacement, invalid/absent joints, viewport/near-plane clipping, skeleton configuration round trips, event-footprint depth/expiry/cache behavior, missing projectile samples and the three shared-depth failures. The final integrated build result is recorded in `release/verification-3.27.2.json` before packaging. Active settings are preserved; the skeleton toggle is off until selected.
