# Version 3.12 verification

All 28 Release CTest suites passed with MSVC 19.44. New coverage verifies independent glow toggles/colors/width/strength, profile round trips and invalid values, legacy defaults, reversible menu fades, the Steam interface fixture and cached 64x64 avatar GPU upload. The actual DLL smoke operates all three glow controls and the menu font dropdown, loads all eight installed font presets, and verifies graphics state preservation. Production stroke GPU readbacks check bright cores, bounded soft halos and plain held previews; connected-edge geometry prevents seams on curved paths. Generated Paths, fonts, profile-card and glow comparison images were visually reviewed. The published profile and its background are validated separately.

This was an offline-only update. The Steam API exports were checked in the installed file on disk; the account card was exercised with a synthetic local fixture, not the user's live account. No CS2 process reads, hooks, injection, input or in-game tests were run for 3.12. The original Desktop DLL and profile remain unchanged. Actual in-game appearance/performance and live Steam account display remain unverified for this version.

# Version 3.11 verification

All 26 Release CTest suites passed. Model-batch coverage checks layer ordering, one original batch, original packet preservation, sorted target lookup, transparent layers and a callback exceeding stack capacity. A 300-piece synthetic batch uses seven callbacks instead of the old 900. This is a structural regression check, not an in-game FPS measurement. Projectile tests verify 50 ms discovery, per-frame position refresh, immediate detonation/reuse rejection and reset behavior. Settings tests cover halo parameters, invalid strength and legacy label-background removal. GPU readbacks verify soft outline width/alpha and preservation of scene depth/stencil. The DLL and camera smoke suites were rerun after the final model-buffer change and passed. Demo images were reviewed for label clarity and outline appearance.

No live CS2 reads, hooks, injection, input or testing were performed for 3.11. The prior Desktop DLL and active profile were not modified. Native halo integration, actual frame-time gains and the remaining distance/LOD behavior require a later live check.

# Version 3.10.1 verification

All 26 Release CTest suites passed. Default-profile tests cover first-run fallback, existing-profile precedence, host distance scale, unchanged defaults after saving, folder relocation, relative image/font/sound paths, and invalid profile rejection. Both DLL smoke suites pass. The shared profile and its referenced background are validated again after extracting the ZIP to a different folder. This update changes profile loading and packaging; it adds no native game hooks.

# Version 3.10 verification

All 26 Release CTest suites pass. New coverage exercises the three-second ring, five-second trail expiry, twenty-shot cap and half-second fade, all team filters, collision floor/wall bounces, throw strength, timed detonation, query failure, schema identity reads, viewport clipping, and smooth FOV restoration. Settings tests cover all new fields, invalid values and defaults for older profiles. The actual DLL GUI smoke uses Players > Paths to enable the features and save/reload its profile. Both existing DLL load/unload smoke cycles remain passing. No Debug build was run.

Read-only disassembly verified the native callback and collision layouts against the locally installed build 14181; provenance stays in Updated Offsets/trajectory-layout.json. A read-only sample of the running game on September 11 found eleven controllers, ten pawn records, zero failed player reads, successful projectile enumeration, camera FOV 90 and a physics-world pointer. There was no held or flying utility in that sample. No new view hook, collision call or shot-event hook was executed in CS2 during this validation, so live throw/impact accuracy is unconfirmed. The background check sent no mouse/keyboard input and changed no game memory or settings. See [implementation and limits](TRAJECTORIES.md).

# Version 3.9 verification

All 25 Release suites passed, including new kill-counter and audio tests. The actual DLL menu smoke captures F7 and Mouse 4 through Set bind, saves/reloads the binding, and operates the kill-sound toggle and volume controls. Audio tests decode a Unicode WAV to exact PCM, reject malformed/oversized files, cancel decoding, and verify every sample through a muted XAudio2 output graph. Source/offset checks consume the two kill-counter fields from the existing matching dump. No live CS2 kill-trigger test or Debug rebuild was performed. See [sound and binding details](SOUND-AND-BINDS.md).

# Version 3.8 verification

All 23 Release CTest suites passed with MSVC 19.44. Native model-packet tests cover the named color pass, full-handle ownership, render-component mapping, shared entity filters, layer order, RGBA and preservation of original packets. Offset-generation tests detect changed rendering provenance. Existing GPU, settings, reader, tracking and DLL smoke suites remain passing. Debug was not rerun for 3.8.

On September 11, 2026 the new native module was tested in the running build-14181 CS2 session through a temporary companion. It created both materials, intercepted 35,904 eligible player-model packets across 800 samples, and the user confirmed white exposed fill and purple fill through walls. Shutdown removed the hook successfully. The companion was unloaded and the settings file retained SHA-256 `485D737E0ADD6864E87CEF7AD4D7A034312CDE47B2A09DD1BF82030107BC3428`. No mouse or keyboard input was used. The complete replacement DLL still needs loading in a fresh session; no broad map/performance claim follows from this bounded check. See [Awareness and fill](AWARENESS-AND-FILL.md).

The following sections retain historical 3.6 results; their old native-rendering limitations do not describe 3.8.

# Validation — version 3.6

## Verified result

All 21 CTest suites pass in Release and Debug with MSVC 19.44, Windows SDK 10.0.26100 and CMake 4.4.3. Project code compiles with /W4 and /permissive-. The existing upstream MinHook CMake deprecation notice remains.

The selected skeletal node, saved Tracking dropdown, FOV ring, material renderer, outline postprocess, effects API and dashboard are part of the DLL. Tests execute real D3D11/WARP shaders and read the resulting GPU pixels. The DLL smoke exercises the actual Present hook and real Win32/ImGui controls in two complete manual/automatic load/unload cycles.

| Suite | Coverage |
| --- | --- |
| awareness_math | Projection, clipping, distances, health math and HUD footer layout |
| awareness_patterns | Pattern boundaries, wildcards and relative addresses |
| awareness_cs2_reader | Entity validation, active firearm lookup, all four skeletal indices, missing/invalid bones and preserved scene origins |
| awareness_camera_tracking | atan2 angles, wrap, FOV, target filtering and timed interpolation |
| awareness_fov_ring | Angular projection, radius, viewport offsets, resolution, off-axis frusta and offscreen/invalid limits |
| awareness_menu_media | Textured/skinned GPU model, native coordinate conversion, live pose changes, fallback animation, highlight color, asynchronous Unicode image loading and cache removal |
| awareness_effects | GPU flat materials, standard/reversed depth, outer-only glow, width/opacity, disabled paths, exact character silhouettes, leg gaps and rejection of legacy bounds fallback |
| awareness_cs2_glow | Native model fields, color packing, shared filters, disable/rollback, concurrent values, full-handle reuse, protected memory and restoration |
| awareness_cs2_camera | Native angles, guarded writes, preserved roll, concurrent changes, selected-node camera output and no origin fallback |
| awareness_local_memory | Readable/guarded pages, overflow and invalidated regions |
| awareness_weapons | 34 firearm mappings, atlas UVs and unsupported-item fallback |
| awareness_build_verification | The original stale build-address mismatch and invalid/ambiguous signatures |
| awareness_settings | Atomic profile save/load, effects/tracking/RGBA/node persistence, invalid selections and old profile defaults |
| awareness_input | Latest coalesced cursor position, key/button ordering, native cursor and focus handling |
| awareness_api | Version/header checks, v1/v2-to-v3 frame conversion, protected-page ABI boundaries, lifecycle and invalid effects |
| awareness_scanner_integration | Loaded-module scanner behavior |
| awareness_offsets | All 36 consumed offsets, matching JSON/HPP sources, independent skeletal provenance and generated header |
| awareness_build_support | Cache relocation, quoting, child environments and failure propagation |
| awareness_offset_generation | No-op import, stale/generated files, schema updates, conflicting exports, invalid bone IDs and skeletal provenance changes |
| awareness_dx11_smoke | Actual DLL node dropdown and changed camera output, auto-save/load, material/FOV pixels, mesh/depth lifetime, resize and host-state restoration |
| awareness_camera_smoke | Portable camera demo hold/follow/release and GPU projection |

## Character highlight verification

The glowing AABB expansion was removed from effect_geometry.hpp. Both legacy geometry modes now require actual model triangles. The DLL smoke clicks the single Glow / chams checkbox and verifies both internal passes enable together, draws three filtered model meshes with zero bounds, then confirms a frame without models reports NoGeometry. GPU readback verifies a filled torso with empty space between legs and in unused bounding-box corners. Demo screenshots were inspected.

The native model-highlight test uses actual writable/protected process memory with the imported schema layout. It validates RGBA packing, enabled flags, live colors, unchanged-field write suppression, death/dormancy/self/team/distance filtering, restoration on disable and unavailable data, rollback after partial failure, preserved intervening engine changes, generation-handle reuse and guarded-page rejection. The uninitialized Source cannot enable native effects. No live game rendering is involved in this test.

## Skeletal target verification

The installed build-14181 SAS and Phoenix `.vmdl_c` skeletons were independently decoded using ValveResourceFormat CLI 20.0. Both identify Head=7, Neck=6, Upper Chest/spine_2=4 and Pelvis=1; index 0 is root_motion. The installed client.dll was inspected at RVA 0x91882F: it accesses the skeleton bone pointer at 0x1C0 and uint16 count at 0x1D4 and copies count multiplied by 32 bytes. With schema ModelState=0x140, the non-schema offsets are +0x80 and +0x94. Asset and binary hashes are retained in `Updated Offsets/bone-layout.json`. This is static evidence, not a live animated-pose test.

Reader fixtures verify all four array indices, exact positions, short/missing arrays, invalid positions and unsupported selections. Invalid nodes clear their validity masks while retaining entity bounds/HUD data. Tracking tests demonstrate different Head/Pelvis angles and reject absent nodes without a root/midpoint substitute. Settings tests round-trip the selection and reject invalid ordinals atomically. The DX11 smoke clicks the actual Combo, confirms Head/Pelvis produce opposite pitch signs for a controlled pose, waits for automatic saving, and uses GUI Load to restore the saved selection.

## Rendering assertions

The standalone effects test distinguishes geometry in front of and behind a depth plane. Always visible draws both; Occluded only draws the correct side under both standard and reversed depth. Missing scene depth produces no effect pixels and an explicit status. The original depth/stencil buffer matches byte-for-byte after every pass.

The low-level GPU tests also isolate the internal edge pass: it has no interior fill when the fill pass is disabled. The public controls combine both passes. A wider radius increases the outer boundary and lower opacity lowers its contribution. A submitted triangle rasterizes as a triangle and is copied independently of caller storage; missing triangles produce no fallback box. Invalid mesh ranges are rejected atomically. MeshOnly never fabricates a shape. Unsupported multisampled targets are reported explicitly.

The DLL smoke validates effect toggles through Players > General, resulting entity pixels, the filtered model count, and the supplied scene-depth path. It confirms that depth/mesh inputs expire after one Present and that ResizeBuffers works after effect surfaces are allocated. A separate ring comparison confirms that its toggle changes visible pixels even with the entity HUD disabled. Existing host graphics bindings, depth/stencil contents, WNDPROC restoration and shutdown behavior still pass.

Automatic saving is tested through the actual footer checkbox: an effect value reaches OverlaySettings.ini after the debounce without pressing Save, then the GUI Load action restores it after a later change. The smoke preserves/restores any pre-existing profile. Invalid/new/old profiles are also exercised by the settings suite.

## Visual review

Generated screenshots were inspected at 1000x640 and after expanding the dashboard in a 1280x900 back buffer. Segoe UI typography, black/red translucent surfaces, rounded panels, column alignment, footer persistence, scrolling, HUD shadows, model silhouette fill, glow and FOV rendering were reviewed. Appearance and camera settings intentionally scroll in smaller windows. The model preview renders a skinned textured character. The smoke also changes the actual theme Combo, loads a background image through the profile, verifies pixels across the menu and checks that graphics state is restored.

The existing firearm icons update on consecutive Presents. HUD font replacement leaves dashboard typography and the selected tab unchanged. No snapshot frame cap was reintroduced: the built-in reader still samples every selected Present.

## Limits and delivery

The user reported the preceding camera integration working. The bone selector and camera output were verified with the synthetic DX11/WARP host. A read-only 3.6 check read live bones successfully; native highlight rendering has not been visually verified in CS2. Bone mapping was checked on the installed SAS/Phoenix rigs; custom or future rigs require verification. CS2 now uses its own model-highlight rendering, whose exact appearance/depth behavior remains a live verification limit. Other hosts supply actual meshes and matching depth through EffectsApi.hpp; no arbitrary bound DSV or collision box substitutes for those inputs.

The D3D11 debug layer was unavailable; GPU readback and graphics-state assertions ran. No claim is made about hardware-GPU performance or end-to-end CS2 latency. Effects add GPU work. The new passes require a single-sampled target; HDR conversion and automatic device-loss recovery remain unsupported. Vulkan/D3D12 and Present1-only paths are outside the renderer contract.

Source remains directly in Desktop/Project; Updated Offsets remains separate. Raw dump files and bone-layout.json are unchanged in this update. The generated header now imports the native glow fields from that same matching schema snapshot. Runtime remains directly in Desktop/CS2-Observer-Overlay. Build caches stay outside Desktop in %LOCALAPPDATA%/EntityAwarenessOverlay/Build. The release hash is recorded in build-info.json. Existing user settings and logs are retained. Reload through the existing coordinated shutdown contract or use a fresh session.

## Menu and preview verification (3.6)

The new GPU test reconstructs a native world pose from the embedded model, converts it through the live renderer, and compares it with the reference render. It then changes bones and verifies changed pixels. The fallback animation/rotation and highlight color produce distinct GPU output. Image tests cover Unicode paths, invalid inputs, resource reuse and removal. INI tests cover theme, opacity, image paths, instant speed and rejection of invalid values. Instant tracking is tested at 30, 144 and 1000 FPS with hotkey/FOV guards. The preview currently retargets the common skeleton onto the SAS mesh.

### Live read-only check

On September 11, 2026, the running CS2 process matched build 14181. The probe read 18 controllers, 16 pawn records, 10 selected tracking nodes and 10 valid full preview poses; two entity reads were skipped. A validated live pose was saved locally and rendered through the same DX11 preview renderer, then its GPU image was inspected. The model had the correct textured body and articulated pose. The probe only requested process query/read rights: no DLL loading, hook, input or memory write was performed. This verifies live sampling and offline retargeting, not an in-game visual inspection of the newly built DLL or native glow.
