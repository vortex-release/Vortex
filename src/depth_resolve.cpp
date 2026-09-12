#include "depth_resolve.hpp"
#include <d3dcompiler.h>
namespace awareness::scene_depth {
namespace {
constexpr char Shader[] = R"(
cbuffer Settings:register(b0) { uint samples; uint reversed; uint2 padding; };
Texture2DMS<float> source:register(t0);
float4 VS(uint id:SV_VertexID):SV_POSITION { float2 uv=float2((id<<1)&2,id&2);return float4(uv*float2(2,-2)+float2(-1,1),0,1); }
float PS(float4 p:SV_POSITION):SV_Depth {
    float depth=reversed?0:1;
    [loop] for(uint i=0;i<samples;++i) { float d=source.Load(int2(p.xy),i);depth=reversed?max(depth,d):min(depth,d); }
    return depth;
})";
bool Formats(DXGI_FORMAT dsv, DXGI_FORMAT &resource, DXGI_FORMAT &sample) {
    switch (dsv) {
    case DXGI_FORMAT_D16_UNORM:
        resource = DXGI_FORMAT_R16_TYPELESS;
        sample = DXGI_FORMAT_R16_UNORM;
        return true;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        resource = DXGI_FORMAT_R24G8_TYPELESS;
        sample = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        return true;
    case DXGI_FORMAT_D32_FLOAT:
        resource = DXGI_FORMAT_R32_TYPELESS;
        sample = DXGI_FORMAT_R32_FLOAT;
        return true;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        resource = DXGI_FORMAT_R32G8X24_TYPELESS;
        sample = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        return true;
    default:
        return false;
    }
}
} // namespace
HRESULT Resolver::Initialize(ID3D11Device *device) {
    if (device_.Get() != device) {
        *this = Resolver{};
        device_ = device;
    }
    if (ps_)
        return S_OK;
    HRESULT hr;
#define RESOLVE_CHECK(x)                                                                                               \
    if (FAILED(hr = (x)))                                                                                              \
    return hr
    Ptr<ID3DBlob> code, error;
    RESOLVE_CHECK(D3DCompile(Shader, sizeof(Shader) - 1, "MSAADepth", nullptr, nullptr, "VS", "vs_5_0",
                             D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error));
    RESOLVE_CHECK(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                             vs_.ReleaseAndGetAddressOf()));
    D3D11_BUFFER_DESC b{};
    b.ByteWidth = 16;
    b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    b.Usage = D3D11_USAGE_DEFAULT;
    RESOLVE_CHECK(device->CreateBuffer(&b, nullptr, constants_.ReleaseAndGetAddressOf()));
    D3D11_DEPTH_STENCIL_DESC d{};
    d.DepthEnable = TRUE;
    d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    d.DepthFunc = D3D11_COMPARISON_ALWAYS;
    RESOLVE_CHECK(device->CreateDepthStencilState(&d, depth_.ReleaseAndGetAddressOf()));
    D3D11_RASTERIZER_DESC r{};
    r.FillMode = D3D11_FILL_SOLID;
    r.CullMode = D3D11_CULL_NONE;
    r.DepthClipEnable = TRUE;
    RESOLVE_CHECK(device->CreateRasterizerState(&r, raster_.ReleaseAndGetAddressOf()));
    code.Reset();
    error.Reset();
    RESOLVE_CHECK(D3DCompile(Shader, sizeof(Shader) - 1, "MSAADepth", nullptr, nullptr, "PS", "ps_5_0",
                             D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error));
    RESOLVE_CHECK(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                            ps_.ReleaseAndGetAddressOf()));
    return S_OK;
}
HRESULT Resolver::Resolve(ID3D11Device *device, ID3D11DeviceContext *context, const D3D11_TEXTURE2D_DESC &target,
                          ID3D11DepthStencilView *depth, bool reversed, Ptr<ID3D11DepthStencilView> &result) {
    result.Reset();
    if (!device || !context || !depth || target.SampleDesc.Count != 1)
        return S_FALSE;
    Ptr<ID3D11Device> owner;
    depth->GetDevice(&owner);
    if (owner.Get() != device)
        return E_INVALIDARG;
    D3D11_DEPTH_STENCIL_VIEW_DESC vd{};
    depth->GetDesc(&vd);
    Ptr<ID3D11Resource> resource;
    Ptr<ID3D11Texture2D> texture;
    depth->GetResource(&resource);
    if (FAILED(resource.As(&texture)))
        return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (desc.Width != target.Width || desc.Height != target.Height || desc.ArraySize != 1 || desc.MipLevels != 1)
        return S_FALSE;
    if (desc.SampleDesc.Count == 1) {
        result = depth;
        return S_OK;
    }
    if (vd.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2DMS || desc.SampleDesc.Count > 16)
        return S_FALSE;
    DXGI_FORMAT typeless{}, sample{};
    if (!Formats(vd.Format, typeless, sample))
        return S_FALSE;
    HRESULT hr;
    RESOLVE_CHECK(Initialize(device));
    if (!input_ || width_ != desc.Width || height_ != desc.Height || samples_ != desc.SampleDesc.Count ||
        quality_ != desc.SampleDesc.Quality || format_ != vd.Format) {
        input_.Reset();
        inputView_.Reset();
        output_.Reset();
        outputView_.Reset();
        auto source = desc;
        source.Format = typeless;
        source.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        source.Usage = D3D11_USAGE_DEFAULT;
        source.CPUAccessFlags = source.MiscFlags = 0;
        RESOLVE_CHECK(device->CreateTexture2D(&source, nullptr, &input_));
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = sample;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        RESOLVE_CHECK(device->CreateShaderResourceView(input_.Get(), &srv, &inputView_));
        source.Format = DXGI_FORMAT_R32_TYPELESS;
        source.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        source.SampleDesc = {1, 0};
        RESOLVE_CHECK(device->CreateTexture2D(&source, nullptr, &output_));
        D3D11_DEPTH_STENCIL_VIEW_DESC output{};
        output.Format = DXGI_FORMAT_D32_FLOAT;
        output.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        RESOLVE_CHECK(device->CreateDepthStencilView(output_.Get(), &output, &outputView_));
        width_ = desc.Width;
        height_ = desc.Height;
        samples_ = desc.SampleDesc.Count;
        quality_ = desc.SampleDesc.Quality;
        format_ = vd.Format;
    }
    ID3D11ShaderResourceView *empty{};
    context->PSSetShaderResources(0, 1, &empty);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    context->CopyResource(input_.Get(), texture.Get());
    const UINT settings[]{samples_, reversed ? 1u : 0u, 0, 0};
    context->UpdateSubresource(constants_.Get(), 0, nullptr, settings, 0, 0);
    auto *cb = constants_.Get();
    context->PSSetConstantBuffers(0, 1, &cb);
    context->OMSetRenderTargets(0, nullptr, outputView_.Get());
    context->OMSetDepthStencilState(depth_.Get(), 0);
    context->OMSetBlendState(nullptr, nullptr, ~0u);
    context->RSSetState(raster_.Get());
    D3D11_VIEWPORT viewport{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
    context->RSSetViewports(1, &viewport);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs_.Get(), nullptr, 0);
    context->PSSetShader(ps_.Get(), nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    auto *srv = inputView_.Get();
    context->PSSetShaderResources(0, 1, &srv);
    context->Draw(3, 0);
    context->PSSetShaderResources(0, 1, &empty);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    result = outputView_;
    return S_OK;
#undef RESOLVE_CHECK
}
} // namespace awareness::scene_depth
