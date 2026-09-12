# Native weather integration

The weather controller adapts the native-particle approach in Anthony's MIT-licensed `features/world/weather.cpp` and Jeremy's `core/features/world/impl/weather.cpp`. Its lifecycle and asynchronous loading are implemented independently. It does not copy their embedded assets, install game files, or use their stale frame-stage enum.

## Runtime contract

- `StartWeather()` verifies the installed client and resource-system images before enabling native calls.
- `ConfigureWeather(options, fresh)` publishes a 500 ms lease; stale snapshots disable creation and request cleanup.
- `TickWeather(stage)` runs after the shared FrameStageNotify original. The verified current network-update end is stage **8**.
- `StopWeather()` requests cleanup on the game thread, waits up to 1500 ms, and returns `ERROR_BUSY` while work remains. The caller must retain the DLL and retry before stopping the shared frame dispatcher.
- `ReadWeatherDiagnostics()` exposes inactive/loading/active/retry/cleanup states and bounded counters.

Options are `enabled` (0/1), `kind` (0 rain, 1 snow, 2 ash), and `density` (0 light, 1 medium, 2 dense). Weather defaults off. Density selects one, two, or three native emitters; it is not a fake density control point. Existing assets do not share a verified density or color CP contract.

Only these existing game assets are requested:

- `particles/rain_fx/rain.vpcf`
- `particles/rain_fx/snow.vpcf`
- `particles/rain_fx/ash.vpcf`

All three were found in the installed `pak01_dir.vpk` and corresponding archives. Rain uses the stock transformed-sphere emitter and environment culling. Snow and ash use their stock transformed-sphere emitters. CP0 follows the verified current camera, including a spectator camera, without attaching to a cached entity pointer. No screen-space rain stack or recurring spawn grid is used.

## Offline verification, 2026-09-11

Client build 14181:

- SHA256 `a0c195f0b6ec00915ef08c548200a010ebbe7982d3a4bc468cad939b67c8c4e3`
- PE timestamp `0x6AA1AE5E`, image size `0x27DE000`
- Particle create wrapper `0x7A75E0`, destroy `0x9A2DA0`, float3 control point `0x9DE340`
- Manager global `0x20A9A78`; RTTI-verified CGameParticleManager vtable `0x1AEF5D8`
- Manager vector count `+0x98`, data `+0xA0`, monotonic next ID `+0xE0`; descriptor effect ID `+0x38`
- Resource-system interface global `0x25F11A0`
- Resource path builder `0x184EBE0`: `(ResourcePath*, const char*, uint64_t type)`, type `0x66637076` (`vpcf`)
- ResourcePath is 0xE0 bytes: CBufferString with 0xC8 inline capacity, resource ID at 0xD0 and hash at 0xD8. The current particle creator constructs this same layout.

ResourceSystem:

- SHA256 `9d0b36be5381f74b51472c2b55327c52a1da5adb1c8f5acaac53c970df491662`
- PE timestamp `0x6AA1ADB5`, image size `0x8E000`
- RTTI-verified CResourceSystem vtable `0x5FA88`
- Manifest request vtable slot 22 / RVA `0x1A790`; release slot 25 / `0x1A920`; path status slot 51 / `0x170B0`
- Manifest request layout: count at0, paths at8, synchronous flag at0x10, second lifetime flag at0x11, reason at0x18, priority at0x20, callback/context at0x28/0x30; size0x38.
- With synchronous=false and priority1, the implementation takes its queue branch at `0x1A8EB`, not the blocking branch at `0x1A89A`.
- Release queues the ticket for engine cleanup. No Vortex callbacks or stack storage survive the request; the engine copies the path list, as demonstrated by its own stack-based callers.
- Status3 is the native fully-loaded state. The original particle creator calls BlockingLoad when the status query returns0. Vortex therefore retains an asynchronous manifest and checks status3 immediately before calling create.

The module validates exact PE identity and 24-byte function prefixes, and checks live interface vtables before use. It uses the exported `CBufferString::Purge(int)` for its own path storage.

## Stability and cost

Normal particle creation and movement run only at network completion. Retirement and resource release can run on any shared game-frame callback after disable, lease expiry, or shutdown, so cleanup does not depend on another network update after leaving a map. Disabled, quiescent weather performs no native frame work.

Storage is fixed. Effect creation is capped at one per150ms, control-point updates at30Hz and only when the camera moved. Creation failures use a two-second backoff. Asynchronous resource requests have a15-second timeout and capped retry delay. At most three resource tickets and three effects are owned.

A particle is owned by its manager generation, its monotonic ID, and its descriptor pointer. Native vectors are bounded at4096entries and cached entry positions handle the common path. Manager replacement, next-ID rollback, global/world replacement, or game-clock rollback discards old tokens without calling into a replacement manager. Transient ownership-read or destroy failures retain pending cleanup, rather than blindly destroying an ID or allocating replacements.

## Verification limits

Deterministic tests cover budgets, source loss, invalid configuration/coordinates/timing, resource readiness, mode/density changes, ID/descriptor reuse, pending creation ownership, retry, and cleanup recovery. The current native function signatures and stock asset presence were checked offline. No game launch, attachment, input, or live visual validation was performed. Actual material appearance and asset behavior remain subject to the game's renderer; no zero-performance-impact claim is made.
