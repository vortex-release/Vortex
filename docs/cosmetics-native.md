# Local cosmetic appearance adapter

Vortex owns a native, client-side cosmetic adapter and an asynchronous catalog reader. It changes the rendered appearance of weapons, knives, gloves, and the local T/CT player model. It does not add inventory items, contact Steam, change account inventory, send economy messages, or persist changes into game archives.

## Sources and attribution

The lifecycle/material sequence was reviewed against Anthony's `features/skinchanger/{skinchanger,skin_sdk,skin_items}.cpp` and Jeremy's `core/features/changer/impl/{guns,knives,gloves,agents,econ_item_system}.cpp` and `changer.hpp`. The original Anthony MIT notice remains in `licenses/Lefrizzel-Ai-LICENSE.txt`. Jeremy's source was supplied by the user; this implementation does not transplant its protection/address framework. Game catalog text is read from the user's installed game, not redistributed.

The port adapts source concepts but replaces permissive pointer/index resolution, guessed fallback offsets, arbitrary HUD erase calls, and stock-value resets with full serial handles, pinned ABI checks, bounded transactions, exact snapshots, and conditional restoration.

## Implemented contract

- `cosmetics_options.hpp`: header-only validation and defaults; one finish per `WeaponIcons` entry, independent knife/glove selection, and T/CT agent definitions. All controls default off. Wear is 0..1, seed 0..1000, paint ID 0..100000, count 0..999999, names are terminated valid UTF-8 without controls.
- `economy_catalog.cpp`: local `pak01_dir.vpk` v1/v2 directory parsing, bounded archive reads, Valve KeyValues and UTF-16 localization, recursive prefab inheritance with cycle/depth limits, item/finish compatibility from item sets and generated asset names. Catalog publication is immutable and asynchronous. It runs no game functions from its worker.
- `cosmetics_native.cpp`: only verified post-original `PostDataEnd = 7` dispatch. Exact weapon owner/serial checks; current held-weapon child ownership for HUD models; snapshot/restore item identity, names, finish values, model paths, mesh masks and subclass tokens. Glove attribute definitions 6/7/8 retain both original presence and value; absent attributes are removed through the verified engine function during restore.
- Weapon finish material rebuilds happen only on state changes, with at most two pending cosmetic changes per normal stage. No render-frame material construction loop and no per-frame disk work.
- Changes are reverted on feature disable, stale/pause configuration, death, weapon ownership loss, or shutdown request. Destroyed/recycled handles are never dereferenced for restoration. Each restored scalar must still match the adapter's last applied value; a newer server value wins.
- `StopNative()` requests game-thread restoration, returns `ERROR_BUSY` while callbacks or tracked state remain, and disables future ticks once drained. The owner then stops its shared dispatcher and calls `Shutdown()` to join catalog IO. No game calls occur on the shutdown thread.

## Current build evidence

The adapter is pinned to build 14181, client PE timestamp `0x6AA1AE5E`, image size `0x27DE000`, the checked-in authoritative schema snapshot, and exact entry bytes for every used function. `scripts/inspect-cosmetics.py` reads the on-disk PE without loading it. A build mismatch fails closed.

| Function | RVA | Current machine-code evidence |
|---|---:|---|
| Set item attribute | `0x1123210` | Adds `0x208` to item view, forwards name in RDX and float in XMM2 to `0x1122E40`, then invalidates description. |
| Remove item attribute | `0x11214A0` | Reads count `+0x210`, pointer `+0x218`; compares 16-bit ID `+0x30` with stride `0x48`. |
| Invalidate item description | `0x111ED20` | Item-view description owner at `+0x200`; invoked through its engine cleanup path. |
| Set model | `0x939940` | Takes entity RCX and path RDX; resolves resource and calls entity model setter. |
| Set mesh mask | `0xA85840` | Scene node RCX, 64-bit mask RDX; model state `+0x140`, mask `+0x208`. |
| Update subclass/view model | `0xAC4E20` | Calls subclass resolver `0x2075A0`, then valid subclass model refresh. |
| Clear/rebuild composite owner | `0x143F020` | Current callsites `0x7DFF87`, `0x803DA2` pass weapon `+0x608` and boolean true. |
| Update weapon skin | `0x7DE490` | Weapon RCX, boolean DL; current engine caller follows composite refresh with this function. |

The original source calls a stage named `FRAME_RENDER_START = 6`; current-client dispatch inspection shows stage6 opens the post-data block and stage7 closes it. Vortex uses the verified post-data end instead of importing the stale semantic name.

## Deliberately separate source features

Anthony's `RegenerateWeaponSkin` wrapper is an explicit no-op with a warning about a prior unsafe argument layout. It is not presented as an extra refresh capability. Its `custom_paint` midhook tries several register candidates for a material vector; those guesses are not imported. Four-channel custom finish tinting requires a separately validated material-build hook and is not implemented by this adapter. Stock finish kits, wear, seed, names, stat counters, knife/glove/agent selections are distinct and supported by the adapter.

No game launch, injection, attachment, input simulation, UI automation, or live playtest is part of this work. Offline fixture compilation and local archive parsing verify configuration, transaction and catalog behavior; they cannot claim the in-game appearance has been visually validated.

## Final offline validation

The Release `awareness_cosmetics_tests` target compiles without warnings and passes its CTest registration. The optional installed-archive/async retry run passed **47 checks, zero failures**, reading **207 definitions, 1,481 paint kits, and 2,121 supported item/finish pairs**. This found and fixed repeated top-level Valve sections, quoted `[0]`/`[*]` keys, clothing-hands classification, and stale cache retention on catalog reload.

Weapon refresh requires a constructed item-view vtable, initialized attributes, a readable subclass, and bounded composite-owner arrays. Default glove views are allowed to begin with `m_bInitialized = 0` only when their constructed view and bounded attribute vector are valid. Current machine code at `client + 0xC1349B` consumes the schema glove-reapply flag; `0xC1356C` clears it, so the port uses that path rather than a guessed body-group vtable.

A model path in the disk catalog is not a residency claim. The adapter checks the scene's actual model binding/resource, model name, and mesh-mask readback. Requests still pending use a 250 ms retry delay and a loading status. Initial resource loading can cost game-thread time; the adapter does not promise zero performance impact. Empty name-tag input preserves the exact original name. The stop path remains retryable until pending restoration is complete or its full entity handle is gone.
