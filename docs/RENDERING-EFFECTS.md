For the current 3.8 native two-color implementation and live verification, see [Awareness and fill](AWARENESS-AND-FILL.md). The older sections below describe the single-color and host rendering paths.

# Character rendering and dashboard — 3.5

The FOV circle, combined character highlight, native CS2 model adapter, host mesh postprocess and dashboard are compiled into EntityAwarenessOverlay.dll. This update retains the public structure layouts and existing four effects exports. See [CHARACTER-HIGHLIGHT.md](CHARACTER-HIGHLIGHT.md) for the native ownership/restoration contract and live verification limits.

## Controls

- **Tracking > FOV circle:** Show FOV circle, line thickness and RGBA color. The ring appears while tracking is enabled and a fresh perspective frame is available. Tracking FOV is a half-angle about the view axis. Radius uses the projection matrix and `tan(halfAngle)`, including viewport offsets and asymmetric frusta. Large boundaries may be outside the screen. Orthographic cameras have no angular ring.
- **Players > General > Glow / chams:** one Glow / chams toggle, color and opacity. In CS2 the native engine controls the silhouette edge/depth behavior. Other hosts also offer Always visible or Occluded only and soft-edge width (1–12 back-buffer pixels).
- **Players > Colors:** ordinary HUD palettes. Highlight color is next to its toggle; the circle color is on Tracking. Theme controls menu colors and background images.
- **Footer:** Auto-save waits for a 500 ms editing pause and writes atomically to OverlaySettings.ini. Normal DLL startup loads it automatically. Explicit host configurations start with Auto-save disabled. An enabled, dirty profile also flushes on coordinated shutdown. Save and Load work manually; failures are available under Settings > Diagnostics. An abrupt process termination cannot guarantee a final save.

The black/red sidebar menu uses shared theme tokens, translucent panels, Segoe UI, compact switches and slim sliders. A separate offscreen renderer shows the textured SAS preview with live pose retargeting and an animated fallback. Theme, image loading and preview details are in [MENU.md](MENU.md). HUD drawing retains anti-aliased corner brackets and shadows.

## Geometry and depth contract

CS2 uses the game's animated-model highlight component. It does not draw a collision box or substitute the demo model. For other hosts, only submitted world-space model triangles are accepted. Missing models are skipped, including when an old client sends the legacy BoundsFallback value. Collision bounds remain available for regular HUD boxes. The following mesh/depth rules apply to the host GPU path; native CS2 compositing uses the engine's own rendering.

`AwarenessSubmitEffectsInput` copies world-space triangle vertices and mesh ranges. Submit it on the bound render thread after AwarenessSubmitFrame and before the next Present. Each batch is consumed once. A new frame invalidates a pending batch. CPU arrays can be released after submission; the input DSV is AddRef'd until the next non-test Present and then released, including skipped frames. Do not resize the host's depth resource between submission and consumption. Stop submitting/join callers before coordinated shutdown.

Vertex counts are bounded to 262,144; there are at most 64 unique entity IDs; ranges must be nonempty, in bounds, finite and divisible by three. Positions use the same coordinate system and row-major, column-vector projection as FrameSnapshot. Exact mesh input must contain the desired visible material triangles; alpha-cutout textures are not sampled by the flat mask shader. Match the mesh pose to the submitted frame.

Scene depth must represent the same camera/viewport and frame, belong to the same D3D11 device, match the back buffer's dimensions, and use a single-sampled Texture2D DSV at mip zero. Submit it before the host clears or reuses it. Reversed depth is an explicit per-input flag. Merely finding a bound DSV does not establish that it contains the camera's scene depth, so the DLL never guesses one from arbitrary pipeline state.

Always visible disables depth testing. Occluded only uses GREATER for standard depth or LESS for reversed depth; depth writes are ZERO and stencil is disabled. Missing or incompatible depth skips the effect and reports DepthUnavailable. The overlay's private cleared depth buffer is never used as a substitute. Multisampled effect targets are explicitly unsupported; ordinary HUD rendering retains its existing support.

The host fill and soft edge share one selected visibility mask and one color. Eligible geometry obeys the HUD's self/dead/dormant/team/range filters and distance opacity. The master overlay toggle controls entity effects. The FOV ring remains independent of that toggle.

## Host example

Link the DLL import library or resolve the exports as ObserverDemo does. After initialization, retrieve and update the effects configuration:

```cpp
#include <awareness/EffectsApi.hpp>
#include <span>

HRESULT ConfigureEffects() {
    awareness::EffectsConfiguration c;
    HRESULT hr = AwarenessGetEffectsConfiguration(&c);
    if (FAILED(hr)) return hr;
    c.materialEnabled = 1;
    c.glowEnabled = 1;
    c.visibility = awareness::EffectVisibility::OccludedOnly;
    c.geometry = awareness::EffectGeometry::MeshOnly;
    c.glowWidth = 5.f;
    c.materialColor = {.35f, .65f, .72f, .22f};
    c.glowColor = c.materialColor;
    return AwarenessSetEffectsConfiguration(&c);
}

// Call on the render thread after drawing the scene and before clearing its depth.
HRESULT PresentWithEffects(IDXGISwapChain* chain,
    const awareness::FrameSnapshot& frame,
    std::span<const awareness::Vector3> triangles,
    std::span<const awareness::EffectMesh> meshes,
    ID3D11DepthStencilView* sceneDepth, bool reversedDepth) {
    if (!chain || triangles.size() > awareness::MaxEffectVertices ||
        meshes.size() > awareness::MaxEntities) return E_INVALIDARG;
    HRESULT hr = AwarenessSubmitFrame(&frame);
    if (FAILED(hr)) return hr;
    awareness::EffectsInput input;
    input.vertices = triangles.data();
    input.vertexCount = static_cast<std::uint32_t>(triangles.size());
    input.meshes = meshes.data();
    input.meshCount = static_cast<std::uint32_t>(meshes.size());
    input.sceneDepth = sceneDepth;
    input.reversedDepth = reversedDepth ? 1u : 0u;
    hr = AwarenessSubmitEffectsInput(&input);
    return FAILED(hr) ? hr : chain->Present(0, 0);
}
```

Use `AwarenessGetEffectsState` after Present for status, model count, matching-depth availability and HRESULT. `boundsCount` remains zero. In CS2, NativeReady and meshCount describe successfully enabled native model components; they are not GPU pixel measurements. The host interface still does not infer opaque model/depth pointers.

## GPU implementation

src/effect_geometry.hpp validates/copies model submissions and never expands bounds into triangles. src/entity_effects.cpp contains HLSL Shader Model 5 programs compiled once on first use with D3DCompile. A reused dynamic vertex buffer batches world-space geometry into a single R8 coverage mask. Max blending prevents overlapping triangles from increasing opacity.

A horizontal and vertical weighted dilation of that mask creates the soft boundary. The original coverage is subtracted so glow stays outside the filled geometry. The composite shader combines the filled mask and soft edge beneath the ImGui HUD. Public configuration normalizes the retained material/glow fields to the same RGBA and enabled state, providing one combined feature. Shader resources are unbound before becoming render targets. Three R8 surfaces are reused and rebuilt on resolution changes; no back-buffer views or submitted scene-depth references survive the frame. The existing D3D11.1 context-state scope restores host graphics bindings.

The mask and postprocess are skipped when the highlight is disabled or models are missing. The CS2 native path uses neither these mask surfaces nor the demo geometry. Width is bounded, buffers are reused and geometry is uploaded once per active frame. Effects add GPU work; no claim is made about measured CS2 frame time. HDR conversion, device-loss recovery, Vulkan and D3D12 remain outside this renderer's contract.
