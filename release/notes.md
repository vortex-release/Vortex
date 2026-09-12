# Vortex 3.27.0

- Added hidden-only and two-color player materials using a private nearest-surface mask and scene-depth composition. Visible and hidden layers no longer stack over the same front-facing surface. Cached visible materials retain the normal native draw path.
- Added a Loadout tab with weapon finishes, wear, seed, StatTrak and names; knife and glove models; and separate T/CT agents. Searchable compatible finishes come from the installed game's catalog. Original item state is restored when overrides are disabled.
- Added native rain, snow and ash with three emitter budgets, asynchronous asset requests and cleanup across map transitions.
- Added equipment icons to the game's scoreboard, including bot rows, optional armor/objective equipment and icon sizing. Updates reuse owned widgets and leave native scoreboard elements intact.
- Reduced snapshot lock contention, depth-copy work, prediction frequency and synchronous diagnostic IO. Added rendering state, resource lifetime and callback shutdown guards.
- Corrected Jumper timing and Strafer direction, added mouse-directed strafing and supported semiautomatic-pistol repeat fire. Added native map tint, scoped FOV, viewmodel controls, visual recoil removal and collision-limited camera distance.
- Added grenade lineup capture/editing, stand/aim guides, JSON import/export and atomic background saves. Added dropped-firearm ammunition labels and complete persistence for the new settings.
- Kept the compact Vortex UI, matching Lucide icons, multiple configuration profiles, launcher and anonymous GitHub updater. Existing personal settings are preserved; new loadout, weather and scoreboard features start disabled.

Validated with Windows x64 Release builds and offline fixtures. No live CS2 testing was performed. Native integrations are guarded for the verified game build. Hidden rendering cannot recover geometry culled by the engine, and its solid mask does not reproduce texture alpha-test cutouts. Hidden layers, weather and first-use cosmetic resources have a rendering/loading cost; zero performance impact is not promised. Scoreboard activation currently uses Tab. Lineup guides require selecting the map. Additional source capabilities outside these ports are documented in the integration inventory.

Installer and update packages are unsigned.
