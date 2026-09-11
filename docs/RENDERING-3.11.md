# Rendering update 3.11

The separate build is in `Desktop/CS2-Observer-Overlay-3.11`. Building this source also deploys there by default. The original release is left intact.

## Model layers and soft glow

The native callback previously locked and searched the entire target list for each mesh packet. It also submitted hidden, original and visible geometry separately for each piece. Changing selection or ordering between body parts could produce inconsistent layers, while the repeated calls added CPU work.

The callback now retains one immutable, sorted target selection. Consecutive packets reuse ownership/model checks within that callback; entity generation, life state and scene ownership remain validated. Hidden packets are submitted in groups of up to 128, followed by the original batch exactly once, then the visible packets. Original packets, animation pointers and sort keys are not modified. Small callbacks use stack storage; larger ones spill into bounded temporary storage. No persistent per-thread heap buffer or destructor is retained across DLL unload.

In Two colors mode, **Soft glow** adds the existing engine silhouette effect around the model fill. **Glow color** and **Strength** are independent of Visible and Hidden colors. The new profile uses white visible fill and cyan hidden fill/halo. The engine determines the native halo's width. Behind walls mode does not add an always-visible halo. Turning off the feature restores owned glow values; a halo failure no longer pauses the separate fill pass.

Native geometry still depends on the engine supplying an eligible animated player draw. This change cannot guarantee a fill for a model culled or absent from that pass. It does not guess new offsets, replay stale animation pointers, or substitute a box for a missing silhouette. No live test was performed for this version, so remaining distance/LOD flicker and the combined native appearance are unconfirmed.

## Frame costs and readability

- Full projectile discovery now runs every 50 ms; already tracked projectiles update their positions each frame. Full handles are rechecked, so recycled entities and detonated utility drop immediately.
- Local memory validation checks its most recent region first. Permission checks and SEH remain in place.
- The overlay no longer allocates or clears an unused full-screen depth texture. The isolated D3D11 context still restores host state; actual supplied scene depth is preserved.
- Background loading runs while the menu is visible. Model preview rendering and pose reads pause when its panel is hidden.
- Host/demo outlines use a normalized separable Gaussian blur. Only the soft-edge surfaces are half resolution; geometry/depth coverage remains full resolution. Blur samples scale with the selected width.
- Names, distances, health numbers and weapon icons no longer draw background plates. Text uses pixel-aligned placement and a one-pixel shadow. Old profiles cannot turn the plates back on.

The offline 300-piece draw test now needs seven callbacks rather than 900. Actual game batches vary, and no FPS improvement is claimed from that fixture. All 26 Release suites passed, including GPU readbacks, settings and DLL load/unload tests. The final buffer adjustment was followed by passing model and DLL smoke checks.
