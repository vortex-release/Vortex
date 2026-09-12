# Vortex 3.26 integration

The source remains Vortex. The supplied Jeremy and Anthony projects were reviewed in place. A hash/line-count inventory of all 431 supplied files is in `triple-source-inventory.json`; actual feature coverage and remaining native dependencies are classified in `anthony-feature-inventory.md`. Original supplier directories were not modified. The pre-integration source backup is `AppData/Local/Vortex/backups/before-triple-integration-20260911-210109.zip`.

## Corrections to the reported problems

- Player material/tint edits now happen before native instance upload. The old late DrawArray hook could leave colors unchanged. Both base and pass-resolved materials are set together; cached flat/lit variants retain one native draw. `render-integration-3.26.md` records the matching binary layouts and corrects the earlier mistaken description of packet+0x28.
- Depth capture avoids work for other/paused contexts, bounds copies, and retains no color-view reference across Present that could block ResizeBuffers. Entity snapshots are exchanged under one try-lock and keep their freshness deadline when an exchange is skipped. Grenade collision prediction updates at most 30 Hz while moving / 10 Hz stationary, with immediate refresh on movement or weapon/strength changes. Periodic diagnostics are queued to a bounded Runtime-owned background writer.
- Jumper separates synthetic release and press across input ticks. Strafer respects the chosen A/D turn branch; optional mouse mode observes raw relative mouse input, supplies A/D and leaves view angles untouched. Manual keys, focus, text input and landing release synthetic ownership. Auto-pistol supports normal semiautomatic IDs and excludes automatic CZ75/charged R8. Existing Assisted Shoot is preserved.

## Added features and integration

World materials change aggregate geometry tint before upload, independently of screen grading. Camera controls use the verified setup callback and collision trace; viewmodel controls use the independently verified current client function at RVA 0x8A66D0, with alive-local-pawn identity checks and frame-local output edits. New camera/world modes default off. Scope zoom remains native unless its own override is enabled.

The lineup library is under the Vortex data directory, `lineups/lineups.json`. Capture reads local feet/eye/raw aim/weapon from the verified view callback. Enter the current map explicitly in Trajectories > Lineups; a saved override must be changed when changing maps. The library accepts its own versioned JSON and Anthony's supported legacy arrays. Saves/imports are atomic, asynchronous and bounded; closing Vortex cancels a pending picker before joining the worker. Library content is independent of INI profiles; appearance and map override are saved in each profile.

New fields for scene/camera/viewmodel, lineup appearance, strafe mode, pistol repeat and dropped-ammo display all persist through normal save/load/import/export. Release presets include explicit new defaults. Existing active settings are preserved during deployment.

## Limits

Visible flat/shaded chams are integrated. Correct native hidden-only silhouettes need a verified inverse-depth or silhouette-composition pipeline; copying Z-disabled duplicate draws would reproduce overlapping layers and extra rendering. Those modes are unavailable in this build. Native economy/skins, weather assets, observer writes, automatic grenade recipes and game scoreboard/vote integrations are not represented as working features. See the inventory for exact coverage.

There is no zero-FPS-cost claim, benchmark claim or live gameplay verification. Offline ABI evidence, unit/concurrency fixtures, synthetic D3D11 checks, settings tests and installer checks are recorded separately in the release verification JSON. Anthony's MIT notice ships in `Lefrizzel-Ai-LICENSE.txt`.
