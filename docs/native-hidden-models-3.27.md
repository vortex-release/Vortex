# Native hidden model layer — 3.27 offline evidence

This adds a native hidden-only mesh layer to the visible GeneratePrimitives integration described in `render-integration-3.26.md`. That earlier document's hidden-layer limitation describes 3.26; it is superseded by the implementation below. All investigation and validation here used supplied source, matching installed image files, a preexisting crash dump, and standalone offline fixtures. CS2 was not launched, attached to, injected into, or visually inspected.

## Rendering contract

Visible and TwoColor modes retain the cached, depth-tested flat/lit material at GeneratePrimitives. OccludedOnly leaves the game's original visible material unchanged. Selected `CsgoForward` player packets are identified again at the verified DrawArray callback, and its original executes exactly once for each original packet in the original order. Contiguous unselected packets remain batched. Full current handle/updater ownership, model classification, alpha and a 250 ms fresh-selection lease are required. Glow-property primitives remain independent.

A selected actual D3D draw is reproduced into private RGBA8 and D32 textures using its current vertex, bone, instance, index, geometry and tessellation state. A nearest-surface depth test retains the frontmost selected model surface, so the player's own farther limbs cannot bleed over its visible torso. The capture uses a solid-color pixel shader. It temporarily disables GPU occlusion predication and restores the original predicate afterward. Unexpected stream-output targets are rejected to prevent duplicate buffer/counter side effects. Bounded Begin/End tracking also suppresses duplicate capture during active host occlusion or pipeline/stream-output statistics queries; otherwise the private draw would inflate the host query result. Native render/depth targets, UAVs, depth/stencil state, blend state, pixel shader/class instances and its constant buffer are restored before the original GPU draw runs. Production performs no CPU depth readback.

At Present, a fullscreen compositor samples that private nearest-surface mask and compares its depth strictly behind the current resolved scene depth. Conventional depth uses GREATER and reversed depth LESS, with an exclusion bias and no scene-depth writes. A visible surface and an equal-depth native surface do not receive hidden tint. Opacity is applied once during this composite. The pass runs before trajectory/HUD/menu rendering. Missing or incompatible scene depth produces no hidden layer and does not arm capture.

The private mask contains opaque geometry coverage. Texture alpha-test cutouts are not reproduced by the solid-color capture shader. CPU-culled geometry for which the game emits no native draw cannot be recovered by this draw-stage implementation. These are explicit limits; no skeletal replacement, guessed native allocation, or unrestricted depth-disabled material is substituted.

## Deferred software-command bridge

Thread-local native draw selection alone is insufficient: Source queues software rendering commands and later replays them on another thread. The matching installed `rendersystemdx11.dll` has PE timestamp `0x6AA1AE0A`, SizeOfImage `0x4AD000`, SHA-256 `299CE092D9E74CCD69A10B92FD76E1CFA065969F72CB4747559840725F726863`.

CDB read the already-existing `Steam/dumps/crash_cs2.exe_20260911195839_1.dmp` and matching installed PE, without a symbol-server download. The new runtime checks validate PE identity, exact recorder/replay instruction bytes and the software-context vtable entry before enabling the bridge.

| Verified operation | RVA / layout |
| --- | --- |
| Skinned DrawArray | `scenesystem+0x545D0`, existing descriptor and entry-byte guard |
| Native indexed draw dispatch | `scenesystem+0x5458B`, context vtable byte offset `0x2F0` |
| Software rendering-context table | `rendersystemdx11+0x3E7D90`; slot `0x2F0` points to `0x26BE0` |
| Software indexed recorder | `rendersystemdx11+0x26BE0`; 8 x64 arguments preserved |
| Current software command cursor / block end | context `+0x420` / `+0x428`; bounded `0x8000` byte block |
| Indexed-instance command | 24 bytes; header `0x0018801F`, then index count, instance count, first index, signed base vertex, first instance |
| Replay indexed draw call | `rendersystemdx11+0x5F06C`; return site `0x5F073` |
| Replay command payload register | caller R15 equals command address +4 |

The recorder hook forwards the engine call exactly once. It verifies the changed cursor, block range, complete header and all five written draw arguments. Every recorded indexed command invalidates an old tag at the same address, including a nonselected replacement with identical arguments. Selected records then publish only their numeric command identity, arguments, color and frame generation into a fixed table. Engine command memory, primitive counts and native allocation structures are not modified. Per-thread page metadata is reused within a frame; guarded memory copies still contain invalid reads.

The 64-bit MASM shim preserves all six D3D DrawIndexedInstanced arguments, captures the original return address and caller R15 before a C++ prologue can change it, and supplies these to the existing D3D observer. Only the exact verified replay return address, command pointer, current generation and all draw arguments consume a tag. Tags are single use; mismatched arguments invalidate them. Unmatched calls always forward once. No command, entity, geometry or buffer resource is retained by a tag.

## Limits, teardown and cost

The table has 16,384 fixed slots with at most eight probes. Captured draws are bounded at 4,096 per frame; lock contention and excess captures are dropped rather than blocking the host rendering thread. Resources are cached per device and size. A resize/device change invalidates old geometry and tags. The mask retains no swap-chain image or view. Frame discard, failed Present, pause and hook rebind all disarm capture. Stale leases expire after 250 ms.

Shutdown disarms capture, disables depth and native callbacks, drains in-flight calls, then removes trampolines and releases private resources. Per-hook removal flags keep cleanup retryable after a failure. Original native materials remain owned by Source's resource cache.

The hidden layer requires an additional draw of selected geometry and one fullscreen composite. This is bounded work, not zero performance overhead. No live FPS improvement is claimed. Diagnostics distinguish bridge readiness, recorded tags, matched replay commands, actual GPU captures and dropped captures; GPU depth availability alone is insufficient to report native hidden rendering ready.

## Offline fixtures

`awareness_native_model_mask_tests` validates the real MASM bridge in a WARP D3D11 process. It preserves negative base vertex, nonzero first index/first instance, instance count and the caller's replay payload; confirms normal original drawing still occurs; excludes front-surface/self overlap; checks conventional and reversed depth, half-opacity hidden blending, untouched scene depth, restored host state, 4x MSAA source attachments, stale/reused commands, disabled capture, resize, target reference lifetime and hook teardown. `awareness_cs2_model_draw_tests` additionally covers bounded command tags, consumed tags, changed arguments, pointer replacement, generation advance and invalid input.

The final focused DLL build and three suites passed: `awareness_native_model_mask` (39 checks), `awareness_cs2_model_draw` (974 checks), and `awareness_trajectory_gpu`. Coverage includes frame-discard propagation, both predication polarities, stream-output rejection, active-query integrity, diagnostics and recorder page-cache reuse. The suppressing-predicate case exposed a color-upload ordering bug: predication must be disabled before `UpdateSubresource`, not only before the private draw. The corrected ordering and host-state restoration pass the fixture. Coordinated full-build and package results are recorded separately in the release verification record.

The private mask, guarded replay bridge and fixture code are original Vortex implementation. The supplied Anthony/Jeremy material and primitive-stage design comparison and retained MIT attribution remain documented in `render-integration-3.26.md` and `licenses/Lefrizzel-Ai-LICENSE.txt`.
