# Movement correction, 3.27.3

## Behavior and controls

Assists > Movement contains Jumper and Strafer, with independent enable switches. Jumper preserves the user's initial Space press, releases during takeoff to arm the next press, and re-jumps at the first eligible grounded sample. A stalled known tick cannot accumulate repeated presses. Disabling the assist restores a still-held Space where the game controls are active; focus loss never restores a key into another application. Failed handoffs remain pending and cannot authorize input for a new pawn or simulation epoch.

Strafer helps align airborne movement to a useful acceleration angle; it does not increase the game's movement limits. A / D steering uses the held side and optional W/S input while adjusting yaw. Mouse direction follows real mouse motion while Space is held, supplies the corresponding side key, and yields to physical A/D. Both modes expose forward-input preservation, walking pause and minimum speed. Camera-specific strength, ease-in, turn limit and physics estimates remain in A/D mode.

## Reproduced defects

Before the fixes, five new assertions failed: W+A and W+D could never leave their equal-speed intermediate angles, and initial ground engagement immediately canceled physical Space. The strafe planner now accepts an equally fast intermediate step toward the selected optimum, while preserving the same-side and turn-rate constraints. Initial jump input is allowed to reach takeoff or a bounded timeout before it is released.

The reader no longer rejects finite native yaw merely because it crosses a full turn. Raw yaw is retained for compare/exchange, so normalization cannot corrupt the expected camera bits. Camera writes distinguish Applied, Unchanged, Yielded and Failed; user-input races restore suppressed forward input and do not inflate successful-turn counts.

The old recoil-priority test treated any nonzero shot counter as active recoil indefinitely. A new-shot observation now starts a bounded 300 ms priority window; old counters, resets, weapon/pawn switches and sampling gaps do not inherit it.

## Timing and limits

The implementation still uses scheduled Windows SendInput and compare/exchange camera updates. Holding Space with Jumper active requests a 1 ms worker wait; ordinary active work waits 4 ms and invalid/inactive work waits 20 ms. These are requested wait intervals, not guaranteed scheduling or game-consumption times. Movement-only reads omit shot, weapon and target work when it is unnecessary. No busy spin or new game callback is installed.

Vortex does not have a verified pre-simulation CUserCmd/subtick hook. Observed controller tick-base progress cannot prove that the game consumed a queued Windows event at a particular command boundary. This release therefore does not claim tick-perfect jumps, server-independent maximum velocity, or measured live performance. A true command-synchronized implementation remains separate work requiring a verified callback, command layout and prediction ordering.

## Validation

The focused synthetic-input fixture passes 623 checks, including the reproduced failures, stalled-tick retry bounds, physical-key handback, failed pawn-transition cleanup, yaw wrapping, movement-only reads, camera write outcomes, recoil-window expiry and poll scheduling. Existing assist and input functionality remains covered by the integrated suite. The final local Release x64 result is recorded in release/verification-3.27.3.json; the release helper repeats build and packaging checks before pushing a tag. No live game access or user settings changes are performed.
