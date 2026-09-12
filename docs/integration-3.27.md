# Vortex 3.27 integration

This release completes the four ports held out of the local 3.26 candidate: hidden player materials, local cosmetic appearances, native weather and scoreboard equipment. It also includes the previously documented 3.25/3.26 stability, input, world/camera, UI and lineup changes. The supplied projects remain unchanged. Their feature inventory and MIT attribution are retained; this is not a claim that every optional feature from either source was copied.

## Rendering and native ownership

`native-hidden-models-3.27.md` records the private nearest-depth model mask, verified deferred-command bridge, strict behind-scene composition, host-state preservation and bounded command storage. Visible materials keep the existing cached native path. The hidden layer composites before trajectories and ImGui. Scene depth alone does not advertise success: diagnostics also expose native bridge readiness, queued/matched commands, captures and rejected draws.

A shared, version-checked frame-stage dispatcher owns cosmetics, weather and scoreboard callbacks. Post-data update is stage 7; network completion is stage 8 in the inspected build. Foreign enum labels were not assumed correct. Per-callback fault isolation, leases, full entity handles and retryable shutdown prevent stale data from becoming retained native work. Extensions must restore or retire their owned state while callbacks remain available; the dispatcher is stopped afterward. `native-frame-scoreboard-3.27.md` documents stage and Panorama evidence.

## Features and profiles

- Loadout: per-weapon compatible finishes, wear, seed, names and StatTrak; knife/glove models and finishes; separate T/CT agents. The catalog is read asynchronously from the installed game. No account inventory or archive writes are involved. `cosmetics-native.md` documents exact rollback and resource limits.
- Weather: stock rain/snow/ash, with one, two or three emitters. Resource requests are asynchronous; native creation waits for verified residency. Cleanup remains on the game thread. See `weather-native.md`.
- Scoreboard: native equipment SVGs in existing player rows, including Steam-ID-zero bots, armor and objective equipment. Reads are bounded to 64 controllers and 64 inventory handles each; up to 16 display items per row. Unchanged widgets/assets are reused. Tab currently controls activation.
- Configuration: every new choice round-trips through the existing atomic INI profile system. Names use bounded UTF-8/hex encoding. Missing fields from older profiles get disabled defaults. Applying a visual preset preserves personal loadout choices. All seven bundled INI profiles receive the new defaults; the existing active user profile is preserved.

## Verification and limits

Only offline source/binary inspection, compilation, standalone D3D11 fixtures and configuration/native-state simulations were used. No CS2 launch, attachment, input simulation or live screen inspection was performed. The final build and test evidence is recorded in `release/verification-3.27.0.json`; public packaging and publication use `release.ps1` and the tagged GitHub Actions workflow.

Hidden rendering adds selected geometry draws and a fullscreen composite. It cannot reconstruct engine-culled geometry or reproduce texture alpha cutouts in its solid mask. Weather costs particle work, and first-use cosmetic models may require engine loading. These changes do not promise zero overhead or measured live FPS improvement. Native ABI guards disable mismatched game builds. The scoreboard DOM fixture proves script behavior against its modeled tree, not every live game layout. Lineup guides still use explicit map selection. Remaining optional source features are classified individually in `anthony-feature-inventory.md`.
