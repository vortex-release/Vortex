#pragma once
#include "OverlayApi.hpp"
#include <d3d11.h>
#include <cmath>

namespace awareness {
inline constexpr std::uint32_t EffectsApiVersion = 1;
inline constexpr std::uint32_t MaxEffectVertices = 262144;
enum class EffectVisibility : std::uint32_t { AlwaysVisible, OccludedOnly, TwoColor };
enum class EffectGeometry : std::uint32_t { BoundsFallback, MeshOnly }; // Value 0 is legacy; no bounds are drawn.
struct EffectsConfiguration {
    std::uint32_t size{sizeof(EffectsConfiguration)}, version{EffectsApiVersion};
    // Retained ABI fields; public configuration normalizes them to one highlight toggle.
    std::uint32_t materialEnabled{}, glowEnabled{};
    EffectVisibility visibility{EffectVisibility::AlwaysVisible};
    EffectGeometry geometry{EffectGeometry::MeshOnly};
    float glowWidth{2.f};
    Color materialColor{.72f, .79f, .83f, .70f}, glowColor{materialColor};
    // In TwoColor mode these are the visible and hidden colors, respectively (same ABI).
    std::uint32_t fovCircle{1};
    float fovThickness{1.25f};
    Color fovColor{.72f, .83f, .88f, .55f};
};
inline bool ValidEffectsConfiguration(const EffectsConfiguration &c) noexcept {
    const auto color = [](Color v) {
        return std::isfinite(v.r) && std::isfinite(v.g) && std::isfinite(v.b) && std::isfinite(v.a) && v.r >= 0 &&
               v.r <= 1 && v.g >= 0 && v.g <= 1 && v.b >= 0 && v.b <= 1 && v.a >= 0 && v.a <= 1;
    };
    return c.size == sizeof(c) && c.version == EffectsApiVersion && c.materialEnabled <= 1 && c.glowEnabled <= 1 &&
           c.fovCircle <= 1 && static_cast<unsigned>(c.visibility) <= 2 && static_cast<unsigned>(c.geometry) <= 1 &&
           std::isfinite(c.glowWidth) && c.glowWidth >= 1 && c.glowWidth <= 12 && std::isfinite(c.fovThickness) &&
           c.fovThickness >= .5f && c.fovThickness <= 4 && color(c.materialColor) && color(c.glowColor) &&
           color(c.fovColor);
}
// Call after validation. Legacy material/glow settings migrate to one model highlight.
inline EffectsConfiguration UnifiedHighlight(EffectsConfiguration c) noexcept {
    if (!c.materialEnabled && c.glowEnabled && c.visibility != EffectVisibility::TwoColor)
        c.materialColor = c.glowColor;
    c.materialEnabled = c.glowEnabled = (c.materialEnabled || c.glowEnabled) ? 1u : 0u;
    if (c.visibility != EffectVisibility::TwoColor)
        c.glowColor = c.materialColor;
    c.geometry = EffectGeometry::MeshOnly;
    return c;
}
struct EffectMesh {
    std::uint32_t entityId{}, firstVertex{}, vertexCount{};
};
// Submit AFTER AwarenessSubmitFrame and BEFORE Present, on the render thread.
// Vertices are world-space triangle lists in the submitted frame's coordinate system.
// CPU data is copied. Depth is AddRef'd until the next non-test Present, then released.
// Supply the scene depth for this camera/viewport BEFORE postprocessing clears it.
// Depth must belong to this D3D11 device, match the back buffer, and be single-sampled.
// No depth/stencil writes or clears are made. No depth supplied => occluded-only skips.
struct EffectsInput {
    std::uint32_t size{sizeof(EffectsInput)}, version{EffectsApiVersion};
    const Vector3 *vertices{};
    std::uint32_t vertexCount{}, meshCount{};
    const EffectMesh *meshes{};
    ID3D11DepthStencilView *sceneDepth{};
    std::uint32_t reversedDepth{};
};
enum class EffectsStatus : std::uint32_t {
    Disabled,
    Ready,
    NoGeometry,
    DepthUnavailable,
    UnsupportedTarget,
    Failed,
    NativeReady,
    NativeSingleColor,
    NativeMaterialPending,
    NativeMaterialReady
};
struct EffectsState {
    std::uint32_t size{sizeof(EffectsState)}, version{EffectsApiVersion};
    EffectsStatus status{EffectsStatus::Disabled};
    std::uint32_t boundsCount{}, meshCount{}, depthAvailable{};
    HRESULT result{S_FALSE};
};
inline const char *EffectsStatusText(EffectsStatus s) noexcept {
    switch (s) {
    case EffectsStatus::Disabled:
        return "Character highlight is off";
    case EffectsStatus::NativeReady:
        return "CS2 character highlight enabled";
    case EffectsStatus::NativeSingleColor:
        return "Single-color highlight: model/depth data unavailable";
    case EffectsStatus::NativeMaterialPending:
        return "Waiting for player model draws";
    case EffectsStatus::NativeMaterialReady:
        return "Player model fill active";
    case EffectsStatus::Ready:
        return "Effects rendering";
    case EffectsStatus::NoGeometry:
        return "Waiting for eligible model geometry";
    case EffectsStatus::DepthUnavailable:
        return "Fill paused: matching scene depth is unavailable";
    case EffectsStatus::UnsupportedTarget:
        return "Effects require a single-sampled render target";
    default:
        return "Effects renderer could not complete this frame";
    }
}
} // namespace awareness
extern "C" {
AWARENESS_API HRESULT __cdecl AwarenessSetEffectsConfiguration(const awareness::EffectsConfiguration *) noexcept;
AWARENESS_API HRESULT __cdecl AwarenessGetEffectsConfiguration(awareness::EffectsConfiguration *) noexcept;
AWARENESS_API HRESULT __cdecl AwarenessSubmitEffectsInput(const awareness::EffectsInput *) noexcept;
AWARENESS_API HRESULT __cdecl AwarenessGetEffectsState(awareness::EffectsState *) noexcept;
}
