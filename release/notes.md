# Vortex 3.27.2

- Fixed shared scene-depth capture when the game uses GPU predication, clears depth more than once before drawing, or renders a depth-only world pass. This restores the required depth path for occluded tracers, throwable paths, fire areas and hidden model shading under those conditions.
- Kept throwable trail history through brief missing samples; retries use full entity identities and never publish stale positions. Held-grenade validation and full-turn yaw handling were corrected.
- Added fire-event footprints while individual burning cells are unavailable. Exact cells replace the estimate once available; original fire particles are retained until their actual replacement has rendered.
- Added player skeletons under Players > General, with color, opacity, line width, outline and joint-dot options under Players > Style. Skeletal reads run in the background cache and honor existing player filters.
- Added preview failure-stage and fire-geometry diagnostics using the bounded background log queue.

Existing settings are preserved; skeleton drawing starts disabled. Offline regression validation only: no live CS2 launch or inspection was performed, and no unmeasured frame-rate or in-game visual result is claimed.
