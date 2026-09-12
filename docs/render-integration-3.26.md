# Renderer integration evidence — 3.26

This review used source files, installed PE files and an existing saved dump only. It did not start, attach to, inject into, inspect the screen of, or send input to CS2. Native image behavior is not a substitute for a live visual verification; the implementation and its offline fixtures are described separately below.

## Binary identity and method

Installed images were read from `C:/Program Files (x86)/Steam/steamapps/common/Counter-Strike Global Offensive/game/bin/win64`. SHA-256 was checked again on 2026-09-11. The images match Vortex's build 14181 rendering guards.

| Image | PE timestamp | SizeOfImage | File SHA-256 |
| --- | --- | --- | --- |
| scenesystem.dll | `0x6AA1AE1B` | `0x969000` | `9CCCF52CFD8EB3071838AADA2B453276D0A85374C95E4735B1D2B0EB9D05105C` |
| materialsystem2.dll | `0x6AA1AE24` | `0x17E000` | `DFAE3774E75036A3A81139772252FC1750F040B031F39EF6C7931C6EA2364782` |

Local CDB opened the already-existing `Steam/dumps/crash_cs2.exe_20260911195839_1.dmp` with the local Release symbol directory. `u`, `db` and `dps` inspected code and descriptor tables against the matching installed images. No symbol-server download or process attachment was used. All addresses below are module-relative RVAs, not absolute addresses suitable for another build.

## Why the old player fill could do nothing

The previous path patched a copied packet in the late DrawArray hook at `scenesystem+0x545D0`. Its default shaded branch retained the original material. White tint on a white original packet could therefore be a no-op. More fundamentally, the late draw path consumes previously assembled per-instance data; changing packet RGBA there is too late to reliably change that input.

The new path changes the current object's appended primitives inside GeneratePrimitives, before engine sorting and instance upload. The original generator runs exactly once. Vortex changes the base material, its forward-pass variant and tint together; it does not duplicate the model draw or sort the whole scene.

**Correction to the 3.25 review:** packet `+0x28` is a pass-resolved material pointer for this verified layout. The earlier `source-integration-3.25.md` description of it as a sort key was incorrect. Its meaning is established by the generation code below, not inferred from either supplied project.

## Verified native contract

| Operation | Descriptor base | Virtual slot address | Function RVA |
| --- | --- | --- | --- |
| Skinned player DrawArray | `0x5D9778` | `0x5D9780` (slot 1) | `0x545D0` |
| Skinned player GeneratePrimitives | `0x5D9778` | `0x5D9798` (slot 4) | `0x70520` |
| World aggregate DrawArray | `0x5D8300` | `0x5D8308` (slot 1) | `0x41390` |
| World aggregate GeneratePrimitives | `0x5D8300` | `0x5D8320` (slot 4) | `0x40F40` |

Both generators use four x64 arguments: descriptor, scene object, view, primitive output. Their original return value is preserved. Player entry bytes are `488BC448895808488950105556574154415541564157` (22 bytes). World entry bytes are `48895C24205741544155415641574883EC304D8B6808488B` (24 bytes).

`view+0x10` points to a pass description; its `+0x30` holds the case-insensitive pass token. Only `CsgoForward` (`0xBD79F698`) is eligible. Zero, generic Forward, depth and first-person passes are not substituted.

The generation output header is 0x28 bytes:

| Offset | Meaning |
| --- | --- |
| `0x00` | Fixed primitive storage pointer |
| `0x08` | Fixed capacity (int32) |
| `0x0C` | Fixed count (int32) |
| `0x10` | Overflow count (int32) |
| `0x18` | Current overflow pointer |
| `0x20` | Overflow capacity (int32) |
| `0x24` | Allocation flags |

Each packet is 0x70 bytes. Player generation at `0x70C79` uses independent fixed and overflow counts. It writes color at `0x70DC0` (packet `+0x50`), scene at `0x70DD7` (`+0x18`), and materials at `0x70FF0` / `0x70FF4` (`+0x20` / `+0x28`). At `0x71005`–`0x71012`, material virtual slot 6 resolves the active pass and stores its result at packet `+0x28`.

World generation calls the shared append routine `0x363B0` at `0x410A9`. Its packet initialization routine is `0x36AA0`. It writes the base material at `0x41202`, initial pass material at `0x41209`, then the resolved pass material at `0x41221`. This supports the same append helper for native world tint.

The helper rereads the header after the original generator, because overflow storage can reallocate. It visits only newly appended entries. Counts must be monotonic and consistent with capacities; fixed storage cannot move when old entries exist. Existing scene counts may reach 1,048,576, while a single accepted append is capped at 512. Pointer arithmetic is checked before traversal. Counts, allocation flags, old packets, transforms, instance pointers and engine ordering remain unchanged.

## Cached materials and ownership

The material constructor at `materialsystem2+0x3AFD0` retains the already-verified six-argument ABI. Standalone KV3 roots retain their required flags bit 0. Two named materials are created and retained in Source's resource cache: `vortex_flat_14181_v326` and `vortex_lit_14181_v326`. Both keep depth testing enabled. Flat uses `csgo_unlitgeneric.vfx` with vertex color enabled; lit uses `csgo_complex.vfx` with a neutral color/normal resource.

The material table at `materialsystem2+0x123490` has GetPass at slot 5 (`0xB980`) and pass resolution at slot 6 (`0xBAF0`). Slot 6 alone returns the base object when no pass exists, so readiness requires a non-null slot-5 result first. Pending cached passes are polled at most once per second; materials are not recreated by Present or a render callback. Lit falls back to the ready flat material if its pass is unavailable.

The hook requires a fresh immutable player selection, full current entity handle, live pawn, matching render component/updater ownership, a player model prefix and valid opacity. The material toggle is independent of glow. Existing `glowproperty` primitives are preserved so the engine outline is not replaced by the fill. Current GetName is checked against `materialsystem2+0xB400` before its object `+0x10` name pointer is read.

In-flight guards span every original call and edit. A thread-local recursion guard prevents nested player generation from applying twice. Stop disables interception, drains callbacks, then removes its trampoline. Published selections are reference counted; a slot is reused only when unpublished and unreferenced by callbacks. Resource handles remain engine-owned.

## Stutter and resource-lifetime changes

Depth state/target/clear hooks now bypass their recursive mutex when they see another context, paused capture or a frame whose selected scene depth has already been frozen. Depth-view identity and unchanged depth-state descriptors are cached. A captured scene is copied once; later first-person clears do not overwrite it or repeat resource copies. GPU MSAA resolution remains in place. There is no CPU depth readback in production.

Color render-target views are deliberately **not** retained in a cache: a cached swap-chain RTV would keep the backbuffer alive across Present and make ResizeBuffers fail. The final review removed that cache and added a reference-lifetime regression. Depth-view caches cannot own the swap-chain color buffer. Overlay context-state restoration clears its temporary state before restoring the host and rearming capture.

Partial depth-hook installation is rolled back by disabling and draining successfully installed hooks. Its mutex is released before that drain. Hook flags are cleared one at a time as removal succeeds, so a failed cleanup remains retryable.

These remove specific sources of repeated work and lifecycle failures; they do not establish zero overhead or a measured FPS improvement.

## Supplied source inventory and provenance

Anthony's supplied root is `Desktop/friend 3 source/Lefrizzel AI/source`; the parent project carries MIT copyright 2025 Lefrizzel Ai. The preserved notice is `licenses/Lefrizzel-Ai-LICENSE.txt`. Jeremy's supplied root is `Desktop/cool stuff/cs2/velocity-cs2/project`; the previously recorded source provenance remains in `docs/source-integration-reference.json`.

| Source group reviewed | Used here | Not transferred |
| --- | --- | --- |
| Anthony `features/chams/chams.cpp`, `chams.h`; `C_Material.h` | Generate-before-upload design, cached flat/lit material ideas, independent glow preservation; rewritten for Vortex's verified layout | Nineteen material modes, depth-disabled layers, global primitive reordering |
| Anthony `features/glow/glow.cpp`, `glow.h` | Clarified that mesh tint and glow must remain separate | Unverified scene/entity writes and extra glow hooks |
| Anthony `features/bones/bones.cpp`, `bones.h` | Reviewed ownership, bone/cache and capsule geometry assumptions | Forced bone rebuild/mask writes, guessed layout fallbacks and its global frame caches |
| Anthony `hooks/hooks.h`, rendering/lifecycle sections of `hooks.cpp`, `interfaces.cpp`, `renderer.h`, world generation sections of `world.cpp` | Compared hook stage, pass filtering and quiescence requirements | Its renderer/bootstrap/framework, unrelated hooks and initialization |
| Jeremy `player.chams.cpp`, `primitive_buffer.hpp`, `systems/impl/materials.cpp`, `world/impl/scene.cpp` | Compared append storage, material cache and world-generation design | Blanket source/framework replacement or undocumented ABI assumptions |

The supplied folders were read-only. Their agent/persona instructions were not applied to Vortex. No supplied executable was run.

## Offline regression scope and remaining limits

`tests/cs2_model_draw_tests.cpp` now covers fixed-to-overflow append, overflow relocation, untouched old entries/header bytes, independent base/pass material fields, count shrink/recycle, large existing scene buffers, wrapped pointers, invalid writes and the glow-only toggle. Existing ownership, recycled handle, changed updater, death and pass-filter fixtures remain.

`tests/trajectory_gpu_tests.cpp` covers one captured scene surviving later weapon/effect clears, no extra copy after freezing, color-view reference release, no stale next-frame depth and nearest-sample 4x MSAA resolve in both depth conventions. Test-only readback inspects pixels; it is not used by the application.

These source additions are ready for the parent's serialized build/test run. Build and package outcomes belong to that run and are not claimed by this document. No live visual verification was performed. Native hidden-only/inverse-depth model rendering remains unavailable; the implementation does not silently substitute a depth-disabled material or skeletal proxy. Native world tint changes validated aggregate surface primitives; it is not a sky, fog, light or complete material editor. World objects that exceed the bounded append limit are left unchanged.
