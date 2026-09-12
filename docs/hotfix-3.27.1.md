# Vortex 3.27.1: periodic stalls and finishes

The user reported roughly one-second freezes every five seconds and gun/knife finishes failing while knife/agent model changes worked. Investigation remained offline, using the checked-in code, existing logs, supplied source and current installed image files. No game launch, attachment, screen inspection or live input was performed.

## Rendering thread work

The previous background diagnostic queue was only used by trajectory telemetry. Depth diagnostics in `Renderer::Render` and world diagnostics in `Runtime::RenderFrame` still called `OverlayLog` directly on a five-second schedule. That sink performs debugger output, known-folder/directory lookup, file open/write/close. Those calls can block the rendering thread. Both periodic producers and other render-thread status messages now use the existing bounded try-lock queue. Its blocked-sink fixture verifies producers continue without waiting and the queue remains bounded.

Steam profile and spectator-avatar refresh previously called the Steam client synchronously from the renderer on two- and five-second schedules, even after an image was cached. Successful identity/avatar samples now settle until the user reopens the menu. Known missing avatars also settle; unresolved requests use a finite retry budget. Refresh resets polling eligibility while retaining current textures. Tests count actual provider queries rather than only RGBA decodes, because the old code already cached decoded images while continuing provider calls. First reads and explicit refresh can still block inside Steam; this fix removes steady-state polling without changing native module/thread ownership.

A broader background Steam worker was not applied after automatic approval review flagged concurrency and module-unloading regression risk. The narrower cache change preserves the existing lifetime model.

Depth hook setup/removal used fourteen separate MinHook enable/disable operations, each freezing process threads. Queued transitions apply them together. Existing external hook ownership remains intact on rollback. The focused mask fixture now has 42 passing checks. Its measured offline runtime fell from about two seconds to about 0.16 seconds; this is setup/teardown fixture evidence, not a live FPS result. Visible shader polling now requires both an eligible player and an active visible material.

Slow-frame diagnostics are queued and rate-limited. They report the interval between frames, overlay work, read/render durations and the number of native material refreshes. A large frame gap alone is not reported as proof that the overlay caused it.

## Cosmetic finishes

Fallback paint/seed/wear fields alone do not populate the item-view attributes consumed by the current skin getter. Weapons and knives now receive the verified paint attributes with original-state restoration, as gloves already did. Stable requested appearance changes and pending work control expensive material rebuilds; engine-maintained scalar/flag drift is repaired separately. Native material refreshes are counted in Diagnostics.

A brief delay in the independent 2D snapshot now has a two-second cosmetic grace period, preventing repeated restoration/reapplication during a transient stall. Explicit disable, minimize, rescan and failed rendering still suspend immediately, and the native game-thread adapter independently validates the local player, full entity handles and ownership on every callback.

Pending model changes retain the source and requested target separately from the last confirmed resident model. Disabling during a delayed load waits for confirmation, then restores the original without losing ownership or repeatedly issuing the same request.

Exact implementation evidence and limitations remain in `cosmetics-native.md`. Offline fixtures cover attribute mutation/restoration and rebuild cadence. Runtime visual correctness remains for the user's next ordinary launch to observe.

## Build delivery

Incremental DLL builds do not necessarily rerun the launcher's POST_BUILD copy. Packaging now stages the DLL and demo from their canonical Release target outputs and the default profile directly from source, rather than relying on that copied launcher folder. Public releases still use the versioned GitHub Actions build and checksum-verified assets. Active user settings are not reset.

## Integrated offline validation

The final Release x64 build passed all 54 CTest suites. The optional installed-catalog run passed 73 cosmetic checks with 207 definitions, 1,481 finishes and 2,121 valid pairs. All nine pinned cosmetic entry signatures, including the paint getter, match the installed client image without loading it. Settings retain their original checksum. `release/verification-3.27.1.json` records this local preflight; the release workflow performs its own build and packaging checks.
