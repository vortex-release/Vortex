#pragma once
#include "effect_geometry.hpp"
#include <wrl/client.h>
#include <array>
namespace awareness {
// Called inside the overlay's isolated device-context state. No host state is retained.
class EntityEffects {
    template <class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    struct Surface {
        Ptr<ID3D11Texture2D> texture;
        Ptr<ID3D11RenderTargetView> target;
        Ptr<ID3D11ShaderResourceView> resource;
    };
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11VertexShader> geometryShader_, screenShader_;
    Ptr<ID3D11PixelShader> maskShader_, expandShader_, compositeShader_, fillShader_;
    Ptr<ID3D11InputLayout> layout_;
    Ptr<ID3D11Buffer> vertices_, constants_;
    Ptr<ID3D11RasterizerState> raster_;
    Ptr<ID3D11BlendState> coverageBlend_, alphaBlend_;
    Ptr<ID3D11DepthStencilState> always_, behind_, behindReversed_, visible_, visibleReversed_;
    Ptr<ID3D11SamplerState> sampler_;
    std::array<Surface, 3> surfaces_;
    Ptr<ID3D11DepthStencilState> stencilOnce_;
    Ptr<ID3D11DepthStencilView> fillStencil_;
    UINT fillWidth_{}, fillHeight_{}, stencilAllocations_{};
    UINT width_{}, height_{}, capacity_{};
    HRESULT Initialize(ID3D11Device *);
    HRESULT Resize(ID3D11Device *, UINT, UINT);

  public:
    UINT StencilAllocations() const noexcept { return stencilAllocations_; }
    HRESULT Render(ID3D11Device *, ID3D11DeviceContext *, ID3D11RenderTargetView *, const D3D11_TEXTURE2D_DESC &,
                   ID3D11DepthStencilView *, bool reversed, const Matrix4x4 &, Viewport, std::span<const EffectVertex>,
                   const EffectsConfiguration &, EffectsState &, bool visibleOnly = false);
};
} // namespace awareness
