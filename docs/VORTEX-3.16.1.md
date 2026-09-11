# Vortex 3.16.1

This hotfix repairs two confirmed code defects and strengthens the fire and tracer readers. CS2 was closed during verification, as requested.

| Report | Change and evidence | Remaining limit |
| --- | --- | --- |
| Recoil does not compensate | Removed an implicit false activation argument from the second configuration call each frame. Both calls now honor focus and menu state. The API requires the argument explicitly. | Actual mouse compensation in CS2 was not exercised. |
| Ghost covers the player's overlay | Cached silhouette mask excludes current player pixels from historical poses. GPU readback verifies that stationary trails add no pixels and moving trails preserve every current-player pixel. | The mask uses the packaged skeletal mesh. Other native models still need live confirmation. |
| No fire area | Accepts nonzero burning bytes, reads the active cell count, and records discovered infernos, burning cells and failed reads. Fixtures include burning bytes 2 and 255. | No live molotov was observed. |
| No bullet tracers | An unset owner may be corroborated by a validated active-weapon handle. A contradictory owner or stale handle is rejected. Both cases are tested. | Actual callback delivery and live projected lines remain unverified. |
| Red Xs | Reviewed application and ImGui drawing paths and checked the referenced white texture in the mounted core archive. | No confirmed cause; this issue remains unresolved. |

Ghost rendering copies compatible supplied scene depth into a private resource and supports normal and reversed depth. It never clears the host depth buffer. Current-player masking also works without supplied scene depth; this does not establish world or weapon occlusion in CS2. Private targets and depth states are reused and retried after incomplete resource creation.

Settings > Diagnostics shows the running version, profile path, utility entity count, fire entity count, burning cells, failed reads, and resulting areas. Existing shot counters distinguish callback capture, accepted segments, and projection. Periodic logs now include recoil activation and world-reader state. The bootstrap initializes COM on its worker and logs the loaded module path and process ID.

Verification: all 35 CTest checks passed, including D3D11 readback, repeated DLL loading/unloading, menu interaction, recoil input calculations, reader fixtures, launcher, loader, and local update server. No live match or game FPS benchmark was run.

The installed app and desktop copies are updated together. Saved settings are preserved. Restart CS2 before loading the new DLL in a later session; an already loaded copy keeps its old code.

Downloads remain a local preview. There is no public update address configured. See [hosting instructions](VORTEX-RELEASES.md) to connect the computer's release server to a public HTTPS address.
