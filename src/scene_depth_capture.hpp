#pragma once
#include <d3d11.h>
#include <wrl/client.h>
namespace awareness::scene_depth {
struct Snapshot {
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> view;
    bool reversed{};
    unsigned copies{}, eligibleBindings{}, depthClears{}, qualifiedDraws{};
};
// Initialize after MinHook, once a real swap chain is known. Only the registered immediate context is observed.
HRESULT Start(ID3D11Device *, ID3D11DeviceContext *, UINT width, UINT height) noexcept;
// Before the overlay changes host context state. Returns only this frame's validated capture.
Snapshot BeginOverlay() noexcept;
// After restoring host context state. Arms the next frame; overlays never become depth candidates.
void EndOverlay() noexcept;
// Called when Present skips rendering (minimized, stale or disabled).
void DiscardFrame() noexcept;
HRESULT Stop() noexcept;
} // namespace awareness::scene_depth
