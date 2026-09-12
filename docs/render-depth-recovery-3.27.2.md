# Depth capture recovery (3.27.2)

The capture changes are based on offline GPU reproductions. No game process was opened, attached, or inspected, and no game DLL was loaded during this work. Both available persisted overlay logs contained fixture loads, so they cannot establish which failure occurred during the reported game session.

## Reproduced failures

1. `CopySelected` executes on the host D3D11 context before the overlay swaps to its isolated state. A host occlusion predicate can suppress `CopyResource`. The old code nevertheless marked the cached copy fresh and exposed stale depth to trajectory, area, replay, and hidden-model rendering. The capture now saves the predicate, disables it around the single GPU copy, and restores the same predicate and polarity.
2. A second depth clear before any qualified geometry froze its candidate for the entire frame. Redundant clears now restart candidate evidence. A candidate is frozen globally only after real geometry has been preserved successfully. Failed allocation cannot leave the cleared source advertised as an earlier valid scene.
3. A depth-only scene prepass was rejected because there was no color RTV in slot zero. Such passes now qualify through the same full-size depth attachment, full viewport, known clear direction, compatible depth comparison, and submitted geometry checks. Color attachments remain temporary references and are not retained across resize.

`native_model_mask_tests.cpp` gained exact depth-byte comparisons for both predicate polarities, both conventional and reversed depth, a redundant empty clear before scene geometry, and preservation of a depth-only prepass before a destructive clear. The new fixture failed **5 of 50 checks before the fixes** and passed after the changes. The existing native mask fixture still checks visible/hidden overlap, reverse depth, 4x MSAA attachments, host state restoration, query exclusion, command reuse, resize, and teardown. The trajectory/area GPU suite also passed with the new capture implementation.

Targeted result: `awareness_native_model_mask` passed in 0.16 seconds; `awareness_trajectory_gpu` passed in 0.17 seconds. These are fixture runtimes, not game performance measurements.

## Native ABI review

The matching installed `rendersystemdx11.dll`, examined through an existing saved dump, uses `ID3D11DeviceContext::ClearDepthStencilView` in its software replay block at RVA `0x5F70E`. This release therefore does not add speculative `ClearView` or state-swap detours. The validated primitive/material and indexed-command bridge remains unchanged. The visible material pass and hidden model compositor continue to use actual model geometry; no always-visible line or skeletal fallback was introduced.

The depth copy remains GPU-only and bounded to one copy per frame. The new code adds no GPU readback, CPU wait for a query result, persistent backbuffer reference, additional global hook, or per-frame material recreation. Pixel readbacks appear only in the standalone fixtures.

## Remaining limits

Offline evidence proves the captured-depth failures and their corrections; it does not prove which path a later game session takes. Hidden model capture still cannot invent geometry that the engine never submits, and it deliberately skips draws inside host occlusion/statistics queries or stream-output passes to avoid changing those results. Full-size matching depth is still required. If a later renderer build or render-resolution mode changes these conditions, diagnostics must identify the unavailable source instead of silently drawing through walls.
