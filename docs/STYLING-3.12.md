# Menu and path styling, 3.12

The separate release is `Desktop/CS2-Observer-Overlay-3.12`. The original release and its active profile remain unchanged. Source stays directly in `Desktop/Project`.

## Paths

Players > Paths has Preview, Trails, Bullets and Camera tabs. Preview and Trails each provide five utility colors with opacity, a Glow toggle, Width and Glow strength. Bullets has independent Start/End gradient colors, team filter, Width, Glow and Glow strength. Camera retains the world FOV control. Default flight and bullet glow strengths are 2 and 2.4; held preview glow is off, including its bounce/landing markers. All settings use the existing Save / Load / Auto save system and `OverlaySettings.ini`.

`src/trajectory_style.hpp` owns validated style defaults. `src/trajectory_draw.hpp` emits one gradient strip per projected segment: 16 vertices/42 indices with glow, or 8 vertices/18 indices without it. The core is crisp, with a 12-pixel fading halo beyond its half-width. Curved paths share mitered edges across adjacent strips, with bounded joins at sharp bends; a single pending segment avoids extra path buffers. Bullet endpoint colors interpolate within the same strip; the previous sixteen overlapping gradient segments are gone. The primitive reserves before reading the current vertex index to handle ImGui index rollover. Near-plane and viewport clipping remain in place. Trail/shot history limits and lifetimes are unchanged. This is a bounded screen-space glow, without a full-screen bloom pass. It does not change game materials or grenade prediction physics.

`paths.trailGlow`, `paths.shotGlow` and `paths.previewGlow` are independent. Matching `Strength` and `Width` keys tune the stroke. `paths.trailHE.r` through `.a`, for example, control flight HE color; `paths.previewHE.*` controls its held preview. Other utility names are Smoke, Flash, Fire and Decoy. `paths.shotStart.*` and `paths.shotEnd.*` define bullet colors. Invalid values reject the profile atomically. Older profiles receive the new appearance defaults while retaining existing feature enable switches.

## Menu and fonts

The dashboard uses subtle accent rules, translucent rounded cards and a two-column utility palette. A reversible 180 ms smoothstep fade animates opening and closing. Closing releases input immediately; the fading window cannot accept input. The HUD's draw list keeps its own opacity. Settings > Menu & text can disable animation, and offers independent menu/HUD choices: Segoe UI, Bahnschrift, Verdana, Tahoma, Trebuchet MS, Consolas, Arial and Segoe UI Variable. Missing fonts are disabled; saved unavailable choices fall back to Segoe UI. Font files are read from Windows and cached on first use, rather than bundled into the release. Existing host-provided custom HUD font paths still work. Choosing a HUD preset clears that custom path. The first load of an uncached font performs file/atlas work; subsequent frames reuse it.

The corresponding INI keys are `menuFont`, `hudFont` (ordinals 0–7) and `menuAnimations`. All are internal options; public configuration struct layouts remain unchanged.

## Steam profile card

`src/steam_profile.cpp` reads the current user's display name and medium avatar through the already-loaded Steamworks API. It does not initialize/shut down Steam, run the game's callbacks, require an API key, or read account credentials. The renderer polls every two seconds while the menu is open and uploads a new image only when its handle/account changes. Image dimensions are bounded before allocating; API read failures produce a placeholder. A tooltip exposes the full name if it is clipped by the sidebar.

The normal standalone demo has no Steam connection and shows a placeholder. The smoke test loads `tests/steam_api_fixture.cpp`, a local test DLL with a synthetic name/avatar. That fixture is excluded from the published release. It is not the user's Steam identity. Current installed interface exports were checked on disk; the actual account integration has not been tested inside CS2 for this version.

Steam API references: [display name](https://partner.steamgames.com/doc/api/ISteamFriends#GetPersonaName), [medium avatar](https://partner.steamgames.com/doc/api/ISteamFriends#GetMediumFriendAvatar), and [image decoding/cache guidance](https://partner.steamgames.com/doc/api/ISteamUtils#GetImageRGBA).

## Offline verification

Coverage includes separate glow controls and profile persistence, invalid settings, legacy defaults, reversible fade timing, exact stroke colors/vertex budgets, real WARP pixel readback of glow versus plain previews, the font selector and all eight fonts in the DLL atlas, mock Steam export resolution, bounded image decoding and avatar GPU caching. Generated demo images are retained in the build cache. No live CS2 rendering, account, performance or input test was performed, and the running DLL was not replaced.
