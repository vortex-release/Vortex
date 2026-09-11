# Offset sources and discovery

Updated Offsets is the only maintained input folder. The active snapshot is local-build-14181-b1430a455e7b, generated 2026-09-10T00:02:04.9771386+00:00. source.json records hashes for the three consumed JSON files and both matching HPP exports. Other module JSON/HPP files from the same dump remain available for future work.

| Global | Old RVA, build 14180 | Current RVA, build 14181 |
| --- | --- | --- |
| Entity list | 0x2571220 | 0x2577BE0 |
| View matrix | 0x23CB830 | 0x23D21F0 |
| Local controller | 0x23A0F30 | 0x23A78D0 |
| Local pawn | 0x23C6268 | 0x23CCC08 |
| Engine build | 0x60F594 | 0x6105A4 |

The fourteen existing controller/pawn/scene/collision schema fields did not change in this update. Version 3.1 added five weapon fields. Version 3.2 also imports `dwViewAngles` from the same snapshot (client.dll RVA 0x23E2C98). The importer validates all twenty-five consumed values together, including numeric or object-with-offset schema formats. Conflicting HPP/JSON exports are rejected before replacement. A JSON-only external import removes stale optional HPP files. Check mode verifies generated text and recorded hashes without modifying files.

When enabled and the hold key is down, the DLL's camera controller uses that angle slot only with a verified matching build and a ready frame. The address must fit client.dll. Reads reject nonfinite or implausible angles. An aligned writable pitch/yaw pair is updated atomically only if it still matches the sampled values, preserving intervening mouse changes and the separate roll field. Rescan clears the cached camera address. The external diagnostic probe and address scanner remain read-only.

## Build identity

Reading the old engine build RVA after the update returned 655. The reader now verifies the x64 PE image and executable sections, finds exactly one `89 05 ? ? ? ? 48 8D 0D ? ? ? ? FF 15 ? ? ? ? 48 8B 0D` instruction sequence, and resolves the first signed disp32 as `match + 6 + displacement`. The target must remain inside engine2.dll and contain a readable, nonzero build value. Missing, ambiguous, unreadable, invalid-image and out-of-module cases fail verification.

Only a verified build equal to the snapshot build permits the built-in entity reader to use its globals and schema fields. The old raw RVA value is retained as diagnostic evidence, never as the trusted game build. Updating signatures or constants still requires runtime validation against the target game version.

Camera, players and active weapons are read every selected Present; no frame snapshot is reused. Missing modules use a retry delay. Once modules and their build are inspected, invalid camera/session data does not force another engine code scan. Diagnostics can explicitly request a rescan. Module references are held until shutdown.

The in-process reader caches region permissions only for one sample, checks the full range for committed/readable memory, rejects guard/no-access regions and uses SEH around the final copy in case memory disappears. Typed reads publish data only after complete success. The external read-only probe still uses ReadProcessMemory. Weapon read failures clear the icon without discarding valid player data.

## Entity layout

Explicit assumptions: chunk table at +0x10, 512 entries/chunk, identity stride 0x70, index mask 0x7FFF. These are not generated schema fields. Entity/identity backlinks and stored indices are checked. The reader also validates finite camera/position values, plausible health and nondegenerate bounds. Unassigned handles count as inactive; duplicate pawn records are skipped; unreadable or malformed records count as failed reads. Spectator/unassigned teams do not become ordinary player teams.

## Standalone address API

AwarenessFindAddresses scans a selected loaded x64 module (null name defaults to client.dll). It remains independent of the built-in build-verified reader. Its broad signatures do not bypass the built-in build gate.

| Target | Pattern | Displacement / length |
| --- | --- | --- |
| Entity pointer slot | `48 8B 0D ? ? ? ? 48 8D 94 24` | +3 / 7 |
| Matrix address | `48 8D ? ? ? ? ? 48 8B ? 48 8D ? ? ? ? ? E8` | +3 / 7 |

Each resolves as `match + 7 + signed_disp32`. The entity MOV names a pointer slot that is dereferenced once; the LEA names the matrix directly. The wildcarded LEA must still use RIP-relative addressing. Executable-section scanning handles page/chunk overlap and reports incomplete reads. Patterns must be unique. Per-target statuses distinguish absence, ambiguity, bad instructions and unreadable targets; S_OK means both succeeded. A unique readable match alone does not prove game-specific meaning. No scanning writes are performed.

The generic matcher uses its first literal byte to skip non-candidates and still handles leading wildcards, overlapping matches and the final legal position. AwarenessScanFixture.dll supplies synthetic loaded-module tests and is never part of the Desktop runtime.
