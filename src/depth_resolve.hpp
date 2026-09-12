#pragma once
#include <d3d11.h>
#include <wrl/client.h>
namespace awareness::scene_depth {
// Run within the overlay's isolated context state. Hardware MSAA remains enabled in the game.
class Resolver {
    template <class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11Texture2D> input_, output_;
    Ptr<ID3D11ShaderResourceView> inputView_;
    Ptr<ID3D11DepthStencilView> outputView_;
    Ptr<ID3D11VertexShader> vs_;
    Ptr<ID3D11PixelShader> ps_;
    Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11DepthStencilState> depth_;
    Ptr<ID3D11RasterizerState> raster_;
    UINT width_{}, height_{}, samples_{}, quality_{};
    DXGI_FORMAT format_{DXGI_FORMAT_UNKNOWN};
    HRESULT Initialize(ID3D11Device *);

  public:
    HRESULT Resolve(ID3D11Device *, ID3D11DeviceContext *, const D3D11_TEXTURE2D_DESC &, ID3D11DepthStencilView *, bool,
                    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> &);
};
} // namespace awareness::scene_depth
