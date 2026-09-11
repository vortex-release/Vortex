For the current 3.8 native two-color implementation and live verification, see [Awareness and fill](AWARENESS-AND-FILL.md). The older sections below describe the single-color and host rendering paths.

# Character highlight — version 3.5

**Players > General > Glow / chams** is now one feature with one color and opacity. In CS2 it drives the game's model highlight component, allowing the engine to render the character's actual animated silhouette. The overlay no longer expands collision bounds into glowing boxes. Regular HUD boxes, health bars, labels and firearm icons retain their separate controls.

## CS2 integration

`src/cs2_glow.hpp` owns the reversible native model-highlight update. `Source::UpdateHighlight` only enables it after the existing engine build verification succeeds and the current entity frame is ready. It runs on the bound Present thread. The native renderer uses the game's current model and animation; it does not use the demo mannequin or reconstruct a character from collision bounds or a few bones.

All consumed property offsets come from the user's matching JSON/HPP dump in **Updated Offsets**. The generator now imports 36 global/schema fields, including `C_BaseModelEntity::m_Glow` and these `CGlowProperty` fields:

- `m_fGlowColor`, `m_glowColorOverride`, `m_iGlowType`.
- `m_iGlowTeam`, `m_nGlowRange`, `m_nGlowRangeMin`.
- `m_bFlashing`, `m_bEligibleForScreenHighlight`, `m_bGlowing`.

The native update selects glow type 3, supplies the selected RGB/RGBA color, permits the highlight for all teams at the component level, applies the configured distance range, disables flashing, and enables the eligible/glowing flags. The overlay's own shared filters decide which individual players receive it: master toggle, self/dead/dormant checks, team filter, distance fade and opacity. Other component bytes, timing fields and the vtable are untouched.

The native engine controls silhouette compositing, depth behavior and edge width. Host-mesh-only visibility and pixel-width controls are therefore hidden in the CS2 UI. This is not a custom replacement of every Source 2 material shader, and the native result is not asserted to match the reference image pixel for pixel.

Changes made during Present are available to the engine's next scene render. Once active, the silhouette follows the engine model pose rather than a separately sampled box. No per-frame mask surfaces or CPU mesh reconstruction are used by this native path.

## Ownership and cleanup

Each tracked player is identified by the entity-list entry, pawn address, identity pointer and complete handle including its generation. The identity is revalidated before writes. Each 1-byte or aligned 4-byte field is read and updated using compare-and-exchange after checking writable memory. SEH catches invalidated memory; page protections are never changed.

The controller remembers original values only for fields it owns. Unchanged fields are not rewritten on each Present. If the engine supplies a new value during an active highlight, that value becomes the restoration baseline. A partial update failure rolls back owned changes.

Disabling the feature or master overlay, filtering a target out, losing a ready frame, minimizing, rescanning and coordinated shutdown restore owned values where the same entity still exists. A changed full handle is never treated as the old player. Restoration never overwrites a field that has since changed away from the value this module wrote. Temporary write failures can be retried; coordinated shutdown returns failure while owned fields remain unrestored. These guards do not claim to be an engine simulation lock or an atomic snapshot of all component fields.

## Profile and host compatibility

OverlaySettings.ini retains the old effects keys for compatibility. `UnifiedHighlight` normalizes both old enable flags to one flag, both colors to one color, and geometry to MeshOnly. A glow-only legacy profile takes its existing glow color; other profiles keep their material color. The legacy BoundsFallback numeric value is accepted but produces no geometry. Saving through the GUI writes the normalized values using the existing automatic/manual profile routines.

EffectsConfiguration, EffectsInput, EffectsState and FrameSnapshot retain their existing layouts. `EffectsStatus::NativeReady` is appended to the status enum. For that status, `meshCount` reports the number of native player components successfully enabled, not a GPU pixel measurement. The reserved legacy `boundsCount` is zero. A host supplies exact world-space model triangles through the existing effects API; missing models are skipped. The low-level GPU renderer still supports its fill and edge passes internally, while the public configuration and menu expose one combined feature.

## Demo and verification

The normal ObserverDemo now supplies its own posed humanoid triangle models every frame. These are explicitly simulated demo assets. They are never used as a substitute for a CS2 player model. The model mask contains the head, torso, arms and separated legs; the soft edge follows those triangles.

The Release and Debug suites exercise actual GPU model silhouettes, empty leg gaps/bounding-box corners, one-toggle GUI behavior, profile migration, missing-geometry suppression, model/depth lifetime, native-field writes, filters, conflict handling, rollback, reused handles, protected pages and restoration. Existing camera, target-node, FOV, HUD and graphics-state tests remain enabled.

The native component layout was checked against the installed build-14181 snapshot. The updated native effect was **not visually verified in a running CS2 session**. Passing the synthetic memory and DX11 demo tests establishes the integration mechanics, not the final game's exact compositing, visibility or hardware-GPU performance. Load the new DLL in a fresh session to evaluate the in-game result.
