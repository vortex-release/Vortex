# Menu and model preview (3.6)

The sidebar contains Players, Tracking, Theme and Settings. Ordinary pages omit build status, timing captions and units. Settings > Diagnostics retains the information needed to troubleshoot. Theme presets change active controls only; the window and panels retain their black base. Custom accents, window/panel/image opacity, background Fit/Fill and preview rotation persist in the existing INI. The default is red.

`src/ui_theme.hpp` defines the palette and control drawing. Toggles and slim sliders still use ImGui items for focus, keyboard navigation and pointer input. Applying a theme does not recreate the ImGui context, fonts or selected page. The footer stays visible while the page scrolls. Narrow windows collapse the preview into an expandable section.

`src/menu_background.hpp` owns one asynchronous decode/picker job and a cached GPU texture. Path edits are debounced for 250 ms, and image decoding never runs on Present. Native file selection runs on its worker. Coordinated destruction closes an open picker and joins its worker before DLL unload. GPU texture creation runs on the render thread. Removing/replacing a path clears the old texture; late decode results cannot replace a more recently selected path. The INI stores the UTF-8 path as hex, and invalid/truncated data is rejected atomically. Large pictures are resized to at most 2048 on their longest edge. Windows codecs provide supported formats; animated pictures use frame zero.

`src/model_preview.cpp` renders a textured SAS character to a private 384x576 RGBA target with its own depth buffer. The mesh has 21,129 vertices, 16,470 triangles, 94 skin joints and five base-color textures. The model, pose, inverse bind matrices and textures are embedded resources, so the DLL needs no adjacent model files. The isolated D3D11.1 context state restores the game's resources and pipeline after rendering.

When the menu is visible, `Source::ReadPreview` prefers the local alive player, then another alive player. It reads the first 25 shared world-skeleton transforms from the independently verified bone array. Pointer/count, finiteness, quaternion norms, scales, distances and limb lengths are checked. Optional jiggle/weapon nodes may use the exported local pose. Invalid data selects the animated fallback rather than retaining an old pose. The preview retargets onto the SAS mesh; it does not reproduce every agent skin or facial/finger animation. Native coordinates convert to preview space as `(y, z, x) * 0.0254` and combine with each joint's inverse bind transform. The GPU test verifies this conversion against a reference pose and checks changed bone transforms change pixels.

Without a live pose, the exported `tools_preview` pose receives subtle spine animation and optional turntable rotation. Dragging the preview changes its view angle. It is separate from the game highlight renderer: the native CS2 highlight continues to use the actual game model.

`scripts/pack-preview.py` prepares these resources from a Source 2 Viewer GLB export. Export `agents/models/ctm_sas/ctm_sas.vmdl_c` from the installed VPK with `--gltf_export_format glb --gltf_export_materials --gltf_export_animations --gltf_animation_list tools_preview --gltf_mesh_list thirdperson_body,thirdperson_default_gloves`, then run `python scripts/pack-preview.py <export.glb> assets`. This optional preparation step uses Python and Pillow; normal offline CMake builds use the included packed assets. Source provenance and joint names are recorded in assets/preview-model.json.

`camera::InstantFollowSpeed` is 200. Speeds below it retain the original exponential response; at the endpoint alpha is one. This preserves the public configuration layout and the existing menu, hotkey, FOV, target, focus and data-validity gates. It is not an unconditional angle write.

In 3.6.1, Tracking > Teams selects Opponents or All teams independently of the Players filter. It saves with the target-node preference in OverlaySettings.ini; existing profiles default to Opponents.

Version 3.8 connects the Glow / chams Fill selector to native CS2 model draws. Players > Awareness remains available. See [Awareness and fill](AWARENESS-AND-FILL.md) for controls, verified behavior and rendering limits.

Version 3.12 adds reversible menu fades, a Steam profile card, eight installed font presets and a compact path appearance editor. See [the styling implementation](STYLING-3.12.md).
