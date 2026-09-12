# Vortex 3.27.3

- Combined Jumper and Strafer in Assists > Movement, with separate switches and shared controls visible in both steering modes.
- Preserved the initial physical jump press instead of canceling it immediately. Re-jumps arm during flight, avoid repeated presses on a stalled known tick, and restore held Space when control is disabled.
- Fixed A/D steering stalling while forward input is held. Finite camera yaw remains usable after full rotations, and skipped/raced camera writes no longer count as successful turns.
- Fixed historical shot counts keeping Strafer paused after recoil activity ends. Camera and key handoffs retain physical input priority.
- Reduced the worker wait to 1 ms only while Jumper and physical Space are active, with movement-only memory reads when combat data is unnecessary.

This remains a Windows-input assist, not a tick/subtick-synchronized movement hook. Perfect jump timing or maximum velocity is not guaranteed. Settings are preserved; offline validation only, with no game launch or inspection.
