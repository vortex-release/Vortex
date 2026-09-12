# Vortex 3.25 source integration

The user supplied the project in `Desktop/cool stuff` and authorized reuse. Its C++ source was inspected directly; its executables and initialization code were not run. The reference remains unchanged. Vortex keeps its own architecture, branding, launcher, ImGui UI, Lucide icon set, configuration system and updater.

## Integrated capabilities

| Reference capability | Vortex implementation | Controls |
| --- | --- | --- |
| Per-category item visuals | Six category filters, independent display/range/color, utility discovery with name fallback, serial/owner checks | World > Dropped weapons > Categories |
| Utility countdown indicators | Fire countdown from the engine start tick and lifetime; estimated standard smoke countdown; active footprints remain independent of timer expiry | World > Utility areas > Utility timers |
| Hit logs | Bounded event history, captured player names, damage/headshot labels, configurable rows/position/scale/duration, frame-time-based fades | Feedback > Hit feed |
| Movement intent | Optional pause while walking, smooth steering activation/direction changes | Assists > Strafer |
| Input lifecycle | Repeat interval survives transient loss of a crosshair target | Assists > Assisted Shoot |
| Primitive management | Bounded reusable scratch pool, batched reads, preserved draw count/order/return values and transparent-pass ordering | Automatic |

Old profiles retain existing global item settings until a category is customized. Utility items, the hit feed and utility countdowns are opt-in. New options participate in save/load/import/export through the existing validated configuration pipeline. No subscriptions or market UI were introduced.

The new smoke countdown is labelled `~`: it estimates the standard 18-second lifetime. It does not hide the real area when that estimate expires. Fire reads `C_Inferno::m_nFireLifetime` from the project's current checked schema snapshot; no arbitrary offset was added. Missing or invalid clock data suppresses the countdown instead of starting a fresh timer on discovery.

## Reviewed but not transferred

The reference's native material generation, weather, camera/aspect/third-person, model history, inventory replacement and command/subtick features depend on its own interfaces, schema cache and hooks. Their presence in that source does not establish compatibility with Vortex's current hook stage. In particular, its primitive field at `0x28` conflicts with the verified sort-key field in Vortex, and its depth-disabled hidden materials would reproduce the reported clipping problem. Those implementations were not merged. Vortex still does not claim a working hidden-model material or generic world-surface material editor.

Its renderer, thread pool, memory/security wrappers, embedded fonts and initialization framework were not substituted for Vortex's. Existing dependencies and their notices are retained.

## Validation

Checks use synthetic memory fixtures, fake input, software/offscreen rendering and existing automated build tests. No CS2 launch, attachment, menu inspection or gameplay input is part of this update. Coverage includes timer expiry and bad clocks, UTF-8 names, small viewports and feed placement, profile round trips, ownership validation, render scratch reuse and the prior small-stack regression. Package and installed-copy validation is recorded separately in `release/verification-3.25.0.json` when deployment completes.
