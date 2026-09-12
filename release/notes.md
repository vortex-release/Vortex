# Vortex 3.27.1

- Moved the five-second depth/world diagnostics off the rendering thread. File and debugger output now use the existing bounded background queue.
- Stopped repeated Steam lookups once the persona/avatar is cached. Reopening the menu refreshes it; unresolved requests have bounded retries. First reads and explicit refreshes still use the existing Steam API path.
- Batched depth-hook activation and removal, reducing fourteen process-wide thread suspensions to one per transition. Disabled and hidden-only material selections no longer poll unused visible shader variants.
- Fixed gun/knife finishes by applying the economy item attributes consumed by the skin renderer, preserving originals for restoration. Unchanged requested finishes no longer repeatedly rebuild composite materials because of engine-maintained flag changes.
- Kept pending model changes tracked through delayed application and restoration, including disabling while a resource loads.
- Prevented transient overlay snapshot delays from restoring and reapplying the entire loadout; native entity and ownership validation remains active.
- Added queued slow-frame timing and a material-refresh counter to help distinguish overlay work from pauses elsewhere in the game.
- Installer packaging now reads the canonical DLL/demo/defaults directly, avoiding stale copies after incremental builds.

Existing settings are preserved. Validation uses offline builds and fixtures; no live CS2 testing was performed. These changes remove identified blocking paths and missing finish inputs, without claiming a measured live frame-time result.
