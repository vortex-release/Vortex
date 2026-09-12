#pragma once
#include "combat_features.hpp"
#include <awareness/EffectsApi.hpp>
#include <d3d11.h>
#include <memory>
namespace awareness {
struct GhostSurface {
    Matrix4x4 matrix;
    Viewport viewport;
    ID3D11RenderTargetView *target{};
    ID3D11DepthStencilView *depth{};
    bool mask{}, reversed{};
    ID3D11ShaderResourceView *exclusions{};
};
class ModelPreview {
    struct Data;
    std::unique_ptr<Data> data_;

  public:
    ModelPreview();
    ~ModelPreview();
    HRESULT Initialize(ID3D11Device *);
    HRESULT Render(ID3D11DeviceContext *, const PreviewPose *, float seconds, bool rotate, const EffectsConfiguration &,
                   const GhostSurface *ghost = nullptr);
    HRESULT RenderGhosts(ID3D11DeviceContext *, ID3D11RenderTargetView *, const FrameSnapshot &, Viewport,
                         const Configuration &, const combat::Options &, const combat::ReplayFrame &, double now,
                         ID3D11DepthStencilView *sceneDepth = nullptr, bool reversed = false);
    void Drag(float amount) noexcept;
    ID3D11ShaderResourceView *View() const noexcept;
};
} // namespace awareness
