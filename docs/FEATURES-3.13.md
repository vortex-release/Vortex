# Features in 3.13

All new features start disabled. Existing settings remain intact. The internal options and per-weapon overrides save atomically in OverlaySettings.ini; public ABI structs have not changed.

## Recoil

The view-setup callback samples the local weapon handle, definition, shots fired, recoil index and last-shot time. It calls the pinned engine evaluated-punch getter with the same evaluation flag used by engine view consumers. Compensation follows changes in that evaluated vector, so no guessed weapon recoil table is required. Default and 34 per-firearm profiles configure vertical/horizontal strength, start shot and deterministic easing. Smoothness zero applies the current delta immediately. No random mouse movements are generated.

The render loop applies compensation after camera follow with the existing compare-and-swap angle writer. Enabling mid-burst, weapon changes, decreasing shot counts, invalid samples, loss of focus, open menus and stale frames rebase or stop compensation. A failed compare-and-swap never overwrites an intervening mouse update. Native calls require matching build, PE metadata and entry bytes. Recoil behavior has not been verified in a live session for this release.

## Objective and hit feedback

The world reader discovers planted C4, smoke and inferno entities at 5 Hz; cached records update each frame using full entity handles. The objective uses engine countdown fields and a sampled engine clock, draws at x=50/y=height/2, and changes white/yellow/red at 20/10 remaining. Kit status comes from item services. Active defuses use their own duration/countdown; completed bombs disappear.

Hit feedback consumes player_hurt only when its attacker resolves to the local pawn. Damage, victim position and hit group come from the event/entity data. A bounded 32-event history feeds the fading center X and rising damage text. A monotonic event serial prevents repeated sounds across Presents. WAV/MP3 decoding runs on a Media Foundation worker and playback uses Windows Multimedia waveOut, with up to eight independent voices and per-voice sample gain. It never changes system audio volume. Existing kill sounds retain their XAudio2 player. Both support separate file paths, volume, Browse and Test controls.

## Motion ghosts

Ten timestamped skeletal poses per entity are retained in a ring buffer. Movement is sampled at most 40 Hz and historical poses are spaced over the configured trail length. Missing entities, deaths, handle reuse, map resets and teleports discard old history. Ghosts use the packaged SAS mesh skinned to recorded live world joints; individual agent skins and equipment are not copied. The nearest eight eligible histories are rendered, at most 80 character poses, with range/team filtering and off-screen culling. Team colors fade from the selected opacity (maximum .3) to zero. Direction arrows grow with measured speed. Each pose has a depth prepass and a color pass to prevent overlapping mesh parts from accumulating opacity. One shared depth surface handles ghost self-occlusion; no per-ghost depth texture is created.

## World and utility effects

World contrast tints private copies of aggregate world draw packets in the color pass. It reduces world RGB without changing engine-owned packets, material resources or lighting convars. Native player fill colors receive configurable brightness/saturation; the existing model fill and soft silhouette glow controls supply the color override and bloom-like halo. Model brightness applies when model highlighting is enabled. Custom texture import is not included.

Fire ellipses follow active flame positions. Smoke uses a standard geometric cylinder proxy, not its dynamic voxel boundary. Explosion rings come from grenade detonation events and fade rapidly. Each type has a color/opacity and toggle. Projection is clipped to avoid unbounded vertices near the camera. These overlays remain visible through walls.

The particle callback recognizes utility resource paths through the verified collection/resource descriptor chain. It removes only matching packet collections near a current replacement area. Missing names, unknown effects and missing replacement data pass through. It does not disable simulation or blindly hide all particles. Volumetric smoke can also use other engine passes; complete removal of every smoke pass is not claimed. World tint and particle callbacks are independently pinned to local module metadata/entry bytes. All hooks stop accepting work on stale data and coordinate removal during unload.

## Verification

New offline checks cover recoil rebasing/convergence, event/history limits, bomb/kit/defuse reads, handle reuse, fire extents, inactive effects, particle-name indirection, world tint alpha preservation, finite clipped HUD geometry, expired markers and full skinned ghost pixels on WARP. Settings tests cover per-weapon overrides, Unicode sound paths and atomic rejection of invalid data. The hidden DX11 demo exercises the actual DLL controls and saves their state. Native offsets and entry bytes were inspected from the installed build 14181 files without opening, controlling or testing the running game.

The Release build passed all 29 offline tests. Live timing, smoke-pass coverage, recoil feel and frame-rate gains still need an in-game check in a later session. Offline tests establish code and rendering behavior, not live game compatibility or performance.
