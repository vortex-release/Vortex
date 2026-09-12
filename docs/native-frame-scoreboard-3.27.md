# Native frame dispatch and scoreboard equipment — 3.27

This note records the offline evidence used for the shared frame dispatcher and native Panorama scoreboard integration. Addresses are RVAs relative to the named module, specific to the installed CS2 build **14181** inspected on September 11, 2026. They are not signatures for arbitrary future game builds.

Implementation: `src/game_frame.hpp/.cpp`, `src/scoreboard_native.hpp/.cpp`, `src/scoreboard_reader.hpp`, `src/scoreboard.hpp/.cpp`, and `src/scoreboard_script.hpp`. The donor scoreboard implementation was inspected as a feature reference; its API names and stage labels were not accepted as current ABI evidence.

## Verified module identity

| Module | PE timestamp | Image size | SHA-256 of inspected file |
|---|---:|---:|---|
| `client.dll` | `0x6AA1AE5E` | `0x27DE000` | `a0c195f0b6ec00915ef08c548200a010ebbe7982d3a4bc468cad939b67c8c4e3` |
| `panorama.dll` | `0x6AA1AE49` | `0x5B0000` | `a607e0e4a7fdd1cf3c7f828c7a0f56da6d84eee93010972a56a94ade92461c41` |

The inspected files are under the installed game's `game/csgo/bin/win64` and `game/bin/win64` directories. Hashes above identify the offline evidence. Runtime gates check the expected engine build, PE architecture/magic, timestamp and mapped image size, exact relevant entry bytes, and interface/vtable identities. Runtime does not hash the entire modules on each frame. An unsupported build must not silently use these addresses.

## Frame stages, established from current code

`FrameStageNotify` is at client RVA `0xB29E60`; the verified interface table entry at `0x1B20E18` points to it. Its checked first 24 bytes are:

```text
48 89 5C 24 18 48 89 6C 24 20 57 48 83 EC 40 48 8B F9 33 ED 48 8B 89 40
```

The current switch indexes the incoming stage minus three. Its branches and paired profiler scope lifetimes establish these meanings:

| Stage | Vortex name | Branch RVA | Evidence |
|---:|---|---:|---|
| 5 | `NetworkStart` | `0xB2A156` | Opens `FrameNetUpdate`, using the scope object at `this + 0x430`. |
| 6 | `PostDataStart` | `0xB2A2F1` | Opens `FramePostDataUpdate`, using `this + 0x438`. |
| 7 | `PostDataEnd` | `0xB2A336` | Closes the `this + 0x438` scope through the call to `0x50FB50` at `0xB2A351`. |
| 8 | `NetworkEnd` | `0xB2A292` | Closes the `this + 0x430` scope through the call to `0x50FB50` at `0xB2A2E7`. |

The format strings referenced by the opening branches are at `0x1B221C0` and `0x1B221D8`. These relationships matter: stage 5 is not network-update completion, despite older donor enum labels.

One MinHook hook owns this entry. It calls the original function first and then dispatches up to eight fixed subscribers. Scoreboard equipment normally samples at stage 7; weather normally creates and moves effects at stage 8. Subscriber recursion/concurrent dispatch is suppressed without suppressing the original game call. A structured exception from a subscriber disables that subscriber and sets its diagnostic failure bit; the original game function is outside that exception boundary.

Dispatcher teardown disables callback availability, disables the hook, waits for its in-flight calls, and only then removes the hook and clears the trampoline/callback storage. A timeout or hook removal failure retains state for retry. Feature-specific cleanup must finish before the shared dispatcher is stopped.

## Panorama entry and current HUD context

The integration obtains `PanoramaUIEngine001` through the current `CreateInterface` export, checked at panorama RVA `0x37E760`. The expected interface vtable is `0x451E18`, with slot 13 pointing to `0x6DA30`. The current engine pointer is read from the interface at `+0x28`; its slot 77 must equal the script entry at `0xB6B50`.

The checked script-entry prefix is:

```text
4C 89 4C 24 20 4C 89 44 24 18 48 89 54 24 10 55 53 56 57 41 54 41 56 41
```

The invocation passes the current engine, current HUD panel, generated script text, the diagnostic source name `panorama/vortex_equipment.js`, and the final argument `1`. This source name is not a game-file installation. A successful native invocation means no caught native exception; it does not acknowledge that every JavaScript operation found its target panel.

Client RVA `0x243D0B8` supplies the current HUD object; the Panorama panel is at its `+8` field. Both are reacquired, with repeated reads checking coherence. Cached panel values are only compared to detect a changed context; they are never dereferenced to perform later cleanup. A context read distinguishes **Present**, **Gone**, and **Unreadable**. Only successfully repeated null HUD/panel reads prove Gone; an unreadable address does not prove that an existing UI tree was destroyed.

## Player row and bot mapping evidence

The current installed archive contains:

- `panorama/scripts/scoreboard.vts_c`: `pak01_373.vpk`, offset `896480`, length `224846`.
- `panorama/layout/scoreboard.vxml_c`: `pak01_160.vpk`, offset `87441184`, length `11873`.

The compiled script contains readable stock JavaScript. `_NewPlayerPanel` creates rows as `'player-' + oPlayer.m_xuid` (near byte `57991` in the resource). `AllPlayers_t.GetPlayerIndexByPlayerSlot` calls `GameStateAPI.GetPlayerXuidStringFromPlayerSlot`. The native equipment script locates the existing `Scoreboard` and row name-icons host `id-sb-name__nameicons`; it does not create a detached replacement scoreboard.

The index conversion was independently established from current client disassembly:

1. At `0xF5A56C`, the registration prepares callback `0xF3AA30`; `0xF5A586` supplies string `0x1BA9850`, `GetPlayerXuidStringFromPlayerSlot`.
2. The callback receives the player-slot argument in `EDX` and passes it as `ECX` to `0xA8BDC0`.
3. At `0xA8BDD9`, that resolver computes `EDX = ECX + 1` before entity lookup `0x9C3630`.
4. For fake-player flag `controller + 0x3F4`, bit `0x100`, the callback formats the player slot as the row XUID string. Otherwise it obtains the real XUID through `0x919BA0`.

Therefore, for the reader's one-based controller entity index, the correct fallback is:

```javascript
GameStateAPI.GetPlayerXuidStringFromPlayerSlot(controllerIndex - 1)
```

`GetPlayerXuidStringFromEntIndex` is absent from both inspected client and Panorama modules. Using that name behind a JavaScript existence check silently skips the fallback, which leaves Steam-ID-zero bots without equipment rows. The corrected path uses the registered API, not an invented entity layout or a new hook. Real Steam IDs remain decimal strings end-to-end, preserving values larger than JavaScript's exact integer range.

## Bounded reads and UI updates

A snapshot contains at most 64 players and 16 supported equipment entries per player. The reader validates current controller and pawn full handles, alive/team state, and each weapon's full owner handle. It checks weapon identity again after reading. The weapon-service handle vector has a strict 0–64 count limit; invalid vectors or reused handles do not become arbitrary scans.

The small schema additions are weapon-services inventory `+0x48`, pawn armor `+0x1CA4`, and item-services helmet `+0x49`, checked against the bundled build-14181 schema. Remaining fields use the shared offsets. The script uses a fixed equipment-name whitelist; account names and arbitrary resource paths are not interpolated into executable JavaScript.

Normal native work is limited to one update per 100 ms while enabled, the configuration lease is fresh (250 ms), the game is foreground, and the standard Tab scoreboard key is held. Current activation is tied to that key, not an inferred custom scoreboard binding. Unchanged snapshots/options/context skip script submission until the one-second refresh deadline. Options acquisition uses a try-lock on the game thread.

Generated scripts update existing image panels. Per-box signatures skip unchanged contents, and cached image identifiers avoid reloading unchanged SVG resources. Long-term panel lifetime belongs to the game's HUD; the active snapshot limits do not claim a global allocator or zero renderer cost. Styling uses native Panorama layout and the game's equipment SVGs rather than adding another full-screen render pass.

## Ownership and shutdown contract

Vortex boxes have an owned class (`VortexEquipmentV1`) and an identity containing controller index, full controller handle, and Steam ID. Reusing a controller slot cannot keep the previous player's box in the current set. A clear hides only Vortex-owned boxes, leaving native rank/name widgets untouched. Missing rows during HUD construction do not create floating widgets.

Disable, lost freshness/focus, and closed scoreboard input request clearing. Unreadable HUD/interface state retains the cleanup obligation; a verified null HUD/panel releases it without touching an old pointer. Shutdown allows clearing on any shared game-frame callback, because leaving a map can stop stage-7 network callbacks while other frame callbacks continue. A caller outside that game thread performs only read-only null-HUD proof, never a Panorama call.

The stop protocol must account for both active owned UI and in-flight subscriber work. It must not report completion while an earlier first-render callback can still create widgets. An update that began before shutdown must converge to a clear, and the shared frame dispatcher remains enabled until that work drains. If cleanup cannot finish within the bounded stop interval, state is retained and the caller receives `ERROR_BUSY`; this is not evidence of successful cleanup.

## Offline verification and limits

`tests/scoreboard_tests.cpp` covers bounded inventory reads, full-handle reuse, configuration validation, icon serialization, Steam-ID string precision, and Gone versus Unreadable HUD state. `tests/scoreboard_ui_tests.cjs` exercises the generated script against a deterministic panel model, including image reuse, clear/re-enable, controller reuse, missing/rebuilt trees, and the bot-slot API fixture. Consult the coordinated build/test result for pass status; this evidence note does not assert that an unrun fixture passed.

No game launch, attachment, input, native call, or live UI validation was performed for this audit. Offline ABI and archive evidence establish the current addresses, stage interpretation, and row/API mapping. They do not prove live native-thread behavior, visual fit in every game mode, translated/localized layouts, or a measured frame-time cost. A future game update requires fresh module and asset evidence before enabling the native paths again.
