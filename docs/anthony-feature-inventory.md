# Anthony / Jeremy integration inventory

This is a source-level inventory, not a claim that every source feature was ported or live-validated. Anthony's source was read in place and not modified. Features are classified by their actual implementation and native dependencies, rather than menu labels. Vortex retains its renderer, UI, profiles and branding.

## Anthony: Lefrizzel Ai

Source root: `C:/Users/Joshu/OneDrive/Desktop/friend 3 source/Lefrizzel AI/source/lefrizzel Ai`.
Reviewed the headers, entry points, configuration declarations, and implementation branches in every feature row below. Detailed lineup capture, normalization, JSON serialization/import and drawing branches were reviewed for the new implementation.

| Source group | Actual implementation / dependencies | Vortex integration in this pass |
|---|---|---|
| `widgets/widgets.cpp` | Draggable, clamped keybind, planted-bomb, spectator/avatar and radar panels; ImGui, Steam avatar cache, entity services, engine bomb state. | Existing spectator/avatar, watermark and bomb timer retained. No claim that all widget layouts or keybind/radar panels were copied. |
| `enemy_spec/enemy_spec.cpp` | Actual observer-target/mode writes while dead, input cycling, controller and observer pawn resolution. | Distinct native feature; not silently enabled or replaced by a cosmetic toggle. Requires validated observer write lifecycle. |
| `grenade_helper/grenade_helper.cpp` | Capture and armed throw capture, map cache, stand/aim guidance, throw recipe detection, JSON personal library/community packs, rename/delete/type/filter controls. | New Vortex manual capture, edit/remove, map and held-grenade filtering, nearest stand/aim guidance, throw instructions, import/export, atomic storage and asynchronous controller. Imports the native Anthony array format, including crouched eye height stored in `ang.z`. Automated recipe capture and unrelated DMA/Sapphyrus pack formats are not claimed as implemented. |
| `gamemode/gamemode.cpp` | Game type interface and convar reads, map detection and heuristic FFA/team inference. | No unvalidated vtable calls or single-team lobby inference introduced. Runtime map feed can supersede an explicit manual lineup map. |
| `bomb/bomb.cpp` | Planted-C4 pointer discovery, bombsite center cache and optional native `StartDefuse` call. | Existing Vortex planted-bomb/defuse state and timer retained. Auto-defuse and approximate damage calculation not copied. |
| `vote/vote.cpp` | Vote-cast/change event display and delayed engine `vote option1/2` command. | Distinct event/command integration remains; not represented by a nonfunctional control. |
| `custom_paint/custom_paint.cpp` | SafetyHook material-build midhook, loose-variable insertion, per-definition/paint weapon colors and glove tint. | Native material/economy dependency remains. No hard-coded foreign loose-variable layout copied. |
| `panorama/panorama.cpp` | RunScript and panel discovery, auto-accept retries and match-found hook. RunFrame hooks are deliberately gated off (`kInstallPanoramaHooks=false`); MatchFound installation is enabled. | Existing Vortex matchmaking flow retained. No unsafe imported UI vtable assumptions. |
| `hitsound/hitsound.cpp` | Local WAV folder scanning, normal/head/kill overrides, preview via WinMM, guarded filenames. | Existing Vortex hit sound retained; a user WAV library is a distinct additional feature, not claimed here. |
| `hitmarker/hitmarker.cpp` | Hurt/death event handling, pending world hit markers, impact correlation, screen markers and damage text. `ReportHitAddr()` is an actual stub returning zero. | Existing world hit markers / damage feedback retained. No reliance on the stubbed report-hit resolver. |
| `hitlog/hitlog.cpp` | Bounded floating damage log, optional remaining HP and session statistics, dragging and appearance controls. | Existing Vortex hit log retained. Remaining-HP/session-stat details require event fields not supplied by the present snapshot. |
| `notify/notify.cpp` | Thread-safe bounded animated toast stack, optional game-chat and tier0 developer-console output. | Existing Vortex operation feedback retained. Chat/console printing not copied. |
| `nadepred/nadepred.cpp` | Trace-based grenade simulation, bounce/detonation rules, cached held preview, in-air arcs and utility labels. | Existing Vortex prediction and depth-aware trajectory renderer retained; source differences are available to the rendering integration owner. |
| `scoreboard_weapons/scoreboard_weapons.cpp` | Real Panorama HUD JavaScript with equipment collection/cache while Tab is held; map-leave clearing. | Native game-HUD equipment integration completed in 3.27 through the verified shared frame dispatcher and current Panorama API. Bot row mapping, bounded equipment reads, owned widget reuse and cleanup are documented in `native-frame-scoreboard-3.27.md`. |
| `skinchanger/skinchanger.cpp` and `skin_menu.*` | Active weapon/knife/glove/agent changes, item-schema and model dependencies, per-weapon signatures, HUD refresh and knife kill-feed adjustment. | Native weapon/knife/glove/agent appearance adapter ported separately; see `cosmetics-native.md` for ABI and validation limits. Inventory mutation and custom tint midhook are not part of that port. |
| `sound_esp/sound_esp.cpp` | Velocity/ground-state sampled rings. `OnGameEvent` is empty; this is not a working footstep-event hook. | Vortex retains actual step-phase and native-event histories instead of regressing to distance/velocity-only synthetic footsteps. |
| `menu/menu.*`, `menu/menu_ui.*`, `config/config.*`, `config/configmanager.h` | Multiple feature pages, resizable themed UI, font/icon systems; JSON/LZ4 compressed profiles and separate size persistence. | Existing compact Vortex/Lucide UI and validated INI profiles retained. New lineup panel follows the same visual system. No foreign brand/logo or compressed config replacement. |

## Jeremy: velocity-cs2

Source root: `C:/Users/Joshu/OneDrive/Desktop/cool stuff/cs2/velocity-cs2/project/core/features`.
The detailed dropped-item review covered `esp/item/item.overlay.cpp`, `core/settings.hpp`, and the weapon definitions in `utilities/cstypes.hpp`. Other rows are a source-entry-point inventory for the assigned rendering/input/world owners, not a live compatibility assertion.

| Source group | Implemented capability observed | Vortex status / owner |
|---|---|---|
| `esp/item/item.overlay.cpp` | Weapon/utility category filtering, per-category display and range, name/icon color. | Integrated earlier as six Vortex drop groups with optional overrides and safe name fallback. This pass adds optional verified magazine count using Vortex's existing `Clip1` offset. |
| `esp/player/player.overlay.cpp`, `player.glow.cpp`, `player.chams.cpp`; item glow/chams | Boxes/skeleton/health/ammo/flags/offscreen labels, engine glow/material primitives and history objects. | Existing player overlay preserved. Visible materials and the native hidden-only scene-depth mask are integrated; see `native-hidden-models-3.27.md` for geometry and cost limits. Ammo/player flags remain distinct from dropped-weapon magazine labels. |
| `esp/projectile/*`, `misc/impl/projectile.trajectory.cpp` | Utility overlays, fire coverage/timers, projectile/tracer lines, simulated trajectories, throw-angle correction and damage prediction. | Existing area coverage/timers/depth paths retained; renderer owner handles applicable trajectory changes. No claim that angle correction or damage estimates are imported. |
| `esp/other/other.overlay.cpp` | Spectator avatars and bomb/defuse HUD. | Existing spectator/avatar/bomb features retained. |
| `misc/impl/hud.cpp` | Scope/crosshair replacement, hat and velocity display. | Root HUD/camera integration; not an automatic replacement of the existing crosshair. |
| `misc/impl/impacts.cpp` | Report-hit/hurt/impact processing, shot correlation, hit logs and tracers. | Existing Vortex event feedback pipeline retained. |
| `misc/impl/other.cpp`, `scoreboard_weapons.cpp`, `removals.cpp`, `dlight.cpp`, `camera.cpp` | Radar reveal, autobuy, names/alpha, kill-feed preservation, viewmodel/camera changes, scoreboard equipment, scene removals and dynamic lighting. | Native interface-dependent groups; root owns camera/world subset. Not blanket-marked integrated. |
| `world/impl/*` | Weather, smoke, sky/material/light/fog changes. | Native map tint and stock rain/snow/ash are integrated. Weather uses asynchronous resource requests and game-thread lifecycle control; see `weather-native.md`. Other foreign light/fog/smoke features are not blanket-marked integrated. |
| `movement/impl/*`, `combat/impl/*` | Movement and aiming implementations, prediction/extrapolation. | Input agent's source audit and integration. This HUD task makes no capability claim for them. |
| `changer/impl/*` | Economy item, guns, knives, gloves and agent changes. | Reviewed and adapted into the separate local cosmetics adapter; see `cosmetics-native.md`. Catalog parsing, exact restoration, and current-build ABI guards replace the foreign framework. |

The four release-held ports (hidden player materials, local cosmetics, weather, and scoreboard equipment) are implemented in 3.27. Their documents distinguish offline verification from live behavior and enumerate remaining limits. Other optional source features retain their individual status above.

No blanket source-stub conclusion was drawn from early returns: most are explicit guards around substantive code. Confirmed Anthony stubs/gates are listed in their own rows.

## New code and attribution

New `src/grenade_lineups.*`, `src/grenade_lineups_draw.hpp`, `src/grenade_lineups_panel.hpp` implement Vortex's bounded authoring/storage/rendering workflow. The user-facing concept, throw-kind numbering and legacy JSON interchange are adapted from Anthony's grenade helper. The implementation is rewritten around Vortex types and thread ownership, with no source DLL bytes, signatures, arbitrary script execution, or foreign artwork copied.

`licenses/Lefrizzel-Ai-LICENSE.txt` preserves the supplied MIT notice verbatim: Copyright (c) 2025 Lefrizzel Ai. Package this notice with Vortex. Jeremy's earlier adaptation notice remains separately applicable.

## Integration contract

- Persist `lineups.enabled`, `heldOnly`, `range`, `standTolerance`, `aimTolerance`, `color.r/g/b/a`, `mapOverride` and `worldVisuals.dropAmmo`.
- `lineups::Valid(Options)` is header-only so existing settings-only tests do not need to link the IO implementation.
- Add `src/grenade_lineups.cpp`; it uses the existing bundled nlohmann JSON, Windows `ole32`/`shell32`, and standard asynchronous tasks.
- Own one `lineups::Controller` and `lineups::PanelState` on the rendering/UI owner. Call `Tick()` to publish completed work. `DrawPanel` queues actions without running file IO on the draw thread.
- `Capture` contains local feet, actual eye position, raw Source pitch/yaw (positive pitch downward), held definition, monotonic sample time, optional normalized map name and a validity bit. A map supplied by the validated runtime always supersedes manual fallback. Unknown/invalid map or captures older than 250 ms draw nothing.
- Controller storage defaults to the Vortex data directory's `lineups/lineups.json`. Import merges exact duplicates idempotently; malformed files leave the old library intact. Mutations publish only after an atomic save succeeds.
- Compile `tests/lineup_tests.cpp` plus the module against `awareness_math`, `awareness_imgui`, `ole32` and `shell32`. Existing world-feature fixtures cover the added magazine reader/display.

## Focused validation

The canonical Release targets `awareness_lineup_tests` and `awareness_world_features_tests` compiled and passed 60 and 1,219 checks respectively. After adding cancellable controller shutdown and wrapped-yaw regression coverage, the final lineup sources were compiled independently and passed 62 checks, with zero failures. No game launch, process loading, live input or UI automation was performed by this task. The standalone build used the existing compiled ImGui library; the integration owner performs the final canonical build.

`Controller::Stop()` cancels a pending native picker from its own worker apartment and waits for file work to finish. Runtime shutdown calls it explicitly; the destructor also does so. Runtime background-IO status includes lineup operations. The native capture feed and menu use the shared monotonic `FrameSeconds()` timebase. Map override remains an explicit user-selected fallback until a validated map name is supplied.
