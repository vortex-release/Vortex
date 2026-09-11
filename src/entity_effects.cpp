#include "entity_effects.hpp"
#include <d3dcompiler.h>
#include <cstring>
namespace awareness {
namespace {
constexpr char shaders[] = R"(
cbuffer Parameters : register(b0) {
    row_major float4x4 projection;
    float4 material;
    float4 glow;
    float4 kernel; // texel x,y; radius; horizontal
    float4 mode; // two colors; visible-mask pass; fill enabled; edge enabled
};
struct Vertex { float4 position:SV_POSITION; float alpha:TEXCOORD0; };
Vertex Geometry(float3 position:POSITION,float alpha:TEXCOORD0) {
    Vertex o; o.position=mul(projection,float4(position,1));o.alpha=alpha;return o;
}
float4 Fill(Vertex i):SV_TARGET { return float4(material.rgb, material.a*i.alpha); }
float2 Mask(Vertex i):SV_TARGET { return mode.y>0?float2(0,i.alpha):float2(i.alpha,0); }
struct Pixel { float4 position:SV_POSITION; float2 uv:TEXCOORD0; };
Pixel Screen(uint id:SV_VertexID) {
    Pixel o;o.uv=float2((id<<1)&2,id&2);o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;
}
Texture2D<float2> coverage:register(t0);
Texture2D<float2> expanded:register(t1);
SamplerState borderSampler:register(s0);
float2 Expand(Pixel i):SV_TARGET {
    float2 value=0;float total=0;
    float2 step=kernel.w>0?float2(kernel.x,0):float2(0,kernel.y);
    float sigma=max(.6,kernel.z*.45);
    int radius=(int)ceil(kernel.z);
    [loop] for(int j=-radius;j<=radius;++j) {
        float weight=exp(-.5*j*j/(sigma*sigma));
        value+=coverage.SampleLevel(borderSampler,i.uv+step*j,0)*weight;
        total+=weight;
    }
    return value/max(total,.00001);
}
float4 Composite(Pixel i):SV_TARGET {
    float2 mask=coverage.SampleLevel(borderSampler,i.uv,0);
    float2 edge=expanded.SampleLevel(borderSampler,i.uv,0);
    float4 fillColor=material,edgeColor=glow;
    if(mode.x>0) {
        // Visible coverage takes precedence over hidden backfaces/overlapping geometry.
        fillColor=lerp(glow,material,saturate(mask.y/max(mask.x,.00001)));
        edgeColor=lerp(glow,material,saturate(edge.y/max(edge.x,.00001)));
        fillColor.a*=mode.z;
        edgeColor.a*=mode.w;
    }
    float fill=mask.x;
    float outer=saturate(edge.x-fill);
    float a=fill*fillColor.a,b=outer*edgeColor.a;
    float alpha=a+b*(1-a);
    return float4((fillColor.rgb*a+edgeColor.rgb*b*(1-a))/max(alpha,.00001),alpha);
}
)";
struct Parameters {
    Matrix4x4 matrix;
    Color material, glow;
    float kernel[4];
    float mode[4];
};
bool MatchingDepth(ID3D11Device *device, ID3D11DepthStencilView *view, const D3D11_TEXTURE2D_DESC &target) {
    if (!view)
        return false;
    Microsoft::WRL::ComPtr<ID3D11Device> owner;
    view->GetDevice(&owner);
    if (owner.Get() != device)
        return false;
    D3D11_DEPTH_STENCIL_VIEW_DESC vd{};
    view->GetDesc(&vd);
    if (vd.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D || vd.Texture2D.MipSlice != 0)
        return false;
    Microsoft::WRL::ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resource.As(&texture)))
        return false;
    D3D11_TEXTURE2D_DESC d{};
    texture->GetDesc(&d);
    return d.Width == target.Width && d.Height == target.Height && d.SampleDesc.Count == 1 && d.ArraySize == 1;
}
} // namespace
HRESULT EntityEffects::Initialize(ID3D11Device *device) {
    if (device_.Get() != device) {
        *this = EntityEffects{};
        device_ = device;
    }
    if (compositeShader_)
        return S_OK;
    Ptr<ID3DBlob> code, error;
    const auto compile = [&](const char *entry, const char *profile) {
        code.Reset();
        error.Reset();
        return D3DCompile(shaders, sizeof(shaders) - 1, "EntityEffects", nullptr, nullptr, entry, profile,
                          D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error);
    };
    HRESULT hr;
#define CHECK(call)                                                                                                    \
    if (FAILED(hr = (call)))                                                                                           \
    return hr
    CHECK(compile("Geometry", "vs_5_0"));
    CHECK(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                     geometryShader_.ReleaseAndGetAddressOf()));
    const D3D11_INPUT_ELEMENT_DESC elements[]{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    CHECK(device->CreateInputLayout(elements, 2, code->GetBufferPointer(), code->GetBufferSize(),
                                    layout_.ReleaseAndGetAddressOf()));
    CHECK(compile("Screen", "vs_5_0"));
    CHECK(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                     screenShader_.ReleaseAndGetAddressOf()));
    CHECK(compile("Mask", "ps_5_0"));
    CHECK(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                    maskShader_.ReleaseAndGetAddressOf()));
    CHECK(compile("Fill", "ps_5_0"));
    CHECK(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                    fillShader_.ReleaseAndGetAddressOf()));
    CHECK(compile("Expand", "ps_5_0"));
    CHECK(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                    expandShader_.ReleaseAndGetAddressOf()));
    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = sizeof(Parameters);
    buffer.Usage = D3D11_USAGE_DEFAULT;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    CHECK(device->CreateBuffer(&buffer, nullptr, constants_.ReleaseAndGetAddressOf()));
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    raster.ScissorEnable = TRUE;
    CHECK(device->CreateRasterizerState(&raster, raster_.ReleaseAndGetAddressOf()));
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    CHECK(device->CreateDepthStencilState(&depth, always_.ReleaseAndGetAddressOf()));
    depth.StencilEnable = TRUE;
    depth.StencilReadMask = depth.StencilWriteMask = 0xff;
    depth.FrontFace.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;
    depth.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
    depth.FrontFace.StencilFailOp = depth.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    depth.BackFace = depth.FrontFace;
    CHECK(device->CreateDepthStencilState(&depth, stencilOnce_.ReleaseAndGetAddressOf()));
    depth.StencilEnable = FALSE;
    depth.DepthEnable = TRUE;
    depth.DepthFunc = D3D11_COMPARISON_GREATER;
    CHECK(device->CreateDepthStencilState(&depth, behind_.ReleaseAndGetAddressOf()));
    depth.DepthFunc = D3D11_COMPARISON_LESS;
    CHECK(device->CreateDepthStencilState(&depth, behindReversed_.ReleaseAndGetAddressOf()));
    depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    CHECK(device->CreateDepthStencilState(&depth, visible_.ReleaseAndGetAddressOf()));
    depth.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    CHECK(device->CreateDepthStencilState(&depth, visibleReversed_.ReleaseAndGetAddressOf()));
    D3D11_BLEND_DESC blend{};
    auto &b = blend.RenderTarget[0];
    b.BlendEnable = TRUE;
    b.SrcBlend = b.DestBlend = b.SrcBlendAlpha = b.DestBlendAlpha = D3D11_BLEND_ONE;
    b.BlendOp = b.BlendOpAlpha = D3D11_BLEND_OP_MAX;
    b.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    CHECK(device->CreateBlendState(&blend, coverageBlend_.ReleaseAndGetAddressOf()));
    b.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    b.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    b.SrcBlendAlpha = D3D11_BLEND_ONE;
    b.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    b.BlendOp = b.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    CHECK(device->CreateBlendState(&blend, alphaBlend_.ReleaseAndGetAddressOf()));
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    sampler.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    CHECK(device->CreateSamplerState(&sampler, sampler_.ReleaseAndGetAddressOf()));
    // Created last: marks the entire shader/state setup as complete.
    CHECK(compile("Composite", "ps_5_0"));
    CHECK(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                    compositeShader_.ReleaseAndGetAddressOf()));
    return S_OK;
}
HRESULT EntityEffects::Resize(ID3D11Device *device, UINT width, UINT height) {
    if (width == width_ && height == height_)
        return S_OK;
    std::array<Surface, 3> next;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = width;
    d.Height = height;
    d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_R8G8_UNORM;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr;
    for (std::size_t i = 0; i < next.size(); ++i) {
        auto &s = next[i];
        d.Width = i ? (width + 1) / 2 : width;
        d.Height = i ? (height + 1) / 2 : height;
        CHECK(device->CreateTexture2D(&d, nullptr, &s.texture));
        CHECK(device->CreateRenderTargetView(s.texture.Get(), nullptr, &s.target));
        CHECK(device->CreateShaderResourceView(s.texture.Get(), nullptr, &s.resource));
    }
    surfaces_ = std::move(next);
    width_ = width;
    height_ = height;
    return S_OK;
}
HRESULT EntityEffects::Render(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11RenderTargetView *target,
                              const D3D11_TEXTURE2D_DESC &desc, ID3D11DepthStencilView *depth, bool reversed,
                              const Matrix4x4 &matrix, Viewport view, std::span<const EffectVertex> vertices,
                              const EffectsConfiguration &config, EffectsState &state) {
    state.depthAvailable = 0;
    if (!config.materialEnabled && !config.glowEnabled) {
        state.status = EffectsStatus::Disabled;
        return S_FALSE;
    }
    if (!device || !context || !target || !desc.Width || !desc.Height || !Valid(view) ||
        !ValidEffectsConfiguration(config) || vertices.size() > MaxEffectVertices || vertices.size() % 3) {
        state.status = EffectsStatus::Failed;
        state.result = E_INVALIDARG;
        return E_INVALIDARG;
    }
    state.depthAvailable = MatchingDepth(device, depth, desc);
    if (desc.SampleDesc.Count != 1) {
        state.status = EffectsStatus::UnsupportedTarget;
        return S_FALSE;
    }
    if (config.visibility != EffectVisibility::AlwaysVisible && !state.depthAvailable) {
        state.status = EffectsStatus::DepthUnavailable;
        return S_FALSE;
    }
    if (vertices.empty()) {
        state.status = EffectsStatus::NoGeometry;
        return S_FALSE;
    }
    state.status = EffectsStatus::Failed;
    HRESULT hr;
    CHECK(Initialize(device));
    const bool stencilFill =
        config.materialEnabled && !config.glowEnabled && config.visibility == EffectVisibility::AlwaysVisible;
    if (!stencilFill)
        CHECK(Resize(device, desc.Width, desc.Height));
    if (vertices.size() > capacity_) {
        D3D11_BUFFER_DESC b{};
        const auto grown =
            std::max<std::size_t>(vertices.size(), std::max<std::size_t>(4096, capacity_ + capacity_ / 2));
        b.ByteWidth = static_cast<UINT>(grown * sizeof(EffectVertex));
        b.Usage = D3D11_USAGE_DYNAMIC;
        b.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        b.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        Ptr<ID3D11Buffer> next;
        CHECK(device->CreateBuffer(&b, nullptr, &next));
        vertices_ = std::move(next);
        capacity_ = static_cast<UINT>(grown);
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    CHECK(context->Map(vertices_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
    std::memcpy(mapped.pData, vertices.data(), vertices.size_bytes());
    context->Unmap(vertices_.Get(), 0);
    Parameters p{matrix,
                 config.materialColor,
                 config.glowColor,
                 {1.f / desc.Width, 1.f / desc.Height, config.glowWidth, 1},
                 {config.visibility == EffectVisibility::TwoColor ? 1.f : 0.f, 0, config.materialEnabled ? 1.f : 0.f,
                  config.glowEnabled ? 1.f : 0.f}};
    if (!config.materialEnabled && config.visibility != EffectVisibility::TwoColor)
        p.material.a = 0;
    if (!config.glowEnabled && config.visibility != EffectVisibility::TwoColor)
        p.glow.a = 0;
    context->UpdateSubresource(constants_.Get(), 0, nullptr, &p, 0, 0);
    auto *cb = constants_.Get();
    context->VSSetConstantBuffers(0, 1, &cb);
    context->PSSetConstantBuffers(0, 1, &cb);
    context->RSSetState(raster_.Get());
    D3D11_VIEWPORT viewport{view.x, view.y, view.width, view.height, 0, 1};
    context->RSSetViewports(1, &viewport);
    D3D11_RECT clip{static_cast<LONG>(view.x), static_cast<LONG>(view.y), static_cast<LONG>(view.x + view.width),
                    static_cast<LONG>(view.y + view.height)};
    context->RSSetScissorRects(1, &clip);
    ID3D11ShaderResourceView *empty[2]{};
    context->PSSetShaderResources(0, 2, empty);
    if (stencilFill) {
        if (!fillStencil_ || fillWidth_ != desc.Width || fillHeight_ != desc.Height) {
            D3D11_TEXTURE2D_DESC sd{};
            sd.Width = desc.Width;
            sd.Height = desc.Height;
            sd.MipLevels = sd.ArraySize = sd.SampleDesc.Count = 1;
            sd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            sd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            Ptr<ID3D11Texture2D> texture;
            Ptr<ID3D11DepthStencilView> stencilView;
            CHECK(device->CreateTexture2D(&sd, nullptr, &texture));
            CHECK(device->CreateDepthStencilView(texture.Get(), nullptr, &stencilView));
            fillStencil_ = std::move(stencilView);
            fillWidth_ = desc.Width;
            fillHeight_ = desc.Height;
            ++stencilAllocations_;
        }
        // This stencil belongs to the overlay. Never clear or modify host depth.
        context->ClearDepthStencilView(fillStencil_.Get(), D3D11_CLEAR_STENCIL, 1, 0);
        context->OMSetRenderTargets(1, &target, fillStencil_.Get());
        context->OMSetDepthStencilState(stencilOnce_.Get(), 1);
        context->OMSetBlendState(alphaBlend_.Get(), nullptr, 0xffffffff);
        auto *vb = vertices_.Get();
        const UINT stride = sizeof(EffectVertex), offset = 0;
        context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        context->IASetInputLayout(layout_.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(geometryShader_.Get(), nullptr, 0);
        context->PSSetShader(fillShader_.Get(), nullptr, 0);
        context->Draw(static_cast<UINT>(vertices.size()), 0);
        state.status = EffectsStatus::Ready;
        return S_OK;
    }
    auto *rt = surfaces_[0].target.Get();
    const float clear[4]{};
    context->ClearRenderTargetView(rt, clear);
    const bool occluded = config.visibility == EffectVisibility::OccludedOnly;
    context->OMSetRenderTargets(1, &rt, occluded ? depth : nullptr);
    context->OMSetDepthStencilState(occluded ? (reversed ? behindReversed_.Get() : behind_.Get()) : always_.Get(), 0);
    context->OMSetBlendState(coverageBlend_.Get(), nullptr, 0xFFFFFFFF);
    auto *vb = vertices_.Get();
    const UINT stride = sizeof(EffectVertex), offset = 0;
    context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    context->IASetInputLayout(layout_.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(geometryShader_.Get(), nullptr, 0);
    context->PSSetShader(maskShader_.Get(), nullptr, 0);
    context->Draw(static_cast<UINT>(vertices.size()), 0);
    if (config.visibility == EffectVisibility::TwoColor) {
        // Keep the full silhouette in R; add only scene-visible fragments to G.
        // Depth/stencil writes stay disabled in both passes.
        p.mode[1] = 1;
        context->UpdateSubresource(constants_.Get(), 0, nullptr, &p, 0, 0);
        context->OMSetRenderTargets(1, &rt, depth);
        context->OMSetDepthStencilState(reversed ? visibleReversed_.Get() : visible_.Get(), 0);
        context->Draw(static_cast<UINT>(vertices.size()), 0);
    }
    viewport = {0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0, 1};
    context->RSSetViewports(1, &viewport);
    context->IASetInputLayout(nullptr);
    context->VSSetShader(screenShader_.Get(), nullptr, 0);
    context->OMSetDepthStencilState(always_.Get(), 0);
    context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    auto *sampler = sampler_.Get();
    context->PSSetSamplers(0, 1, &sampler);
    if (config.glowEnabled) {
        const UINT blurWidth = (desc.Width + 1) / 2, blurHeight = (desc.Height + 1) / 2;
        viewport.Width = static_cast<float>(blurWidth);
        viewport.Height = static_cast<float>(blurHeight);
        context->RSSetViewports(1, &viewport);
        const D3D11_RECT blurClip{0, 0, static_cast<LONG>(blurWidth), static_cast<LONG>(blurHeight)};
        context->RSSetScissorRects(1, &blurClip);
        context->PSSetShader(expandShader_.Get(), nullptr, 0);
        for (int pass = 0; pass < 2; ++pass) {
            context->PSSetShaderResources(0, 2, empty);
            rt = surfaces_[pass + 1].target.Get();
            context->ClearRenderTargetView(rt, clear);
            context->OMSetRenderTargets(1, &rt, nullptr);
            auto *srv = surfaces_[pass].resource.Get();
            context->PSSetShaderResources(0, 1, &srv);
            p.kernel[3] = pass == 0 ? 1.f : 0.f;
            p.kernel[1] = pass == 0 ? 1.f / desc.Height : 1.f / blurHeight;
            p.kernel[2] = pass == 0 ? config.glowWidth : config.glowWidth * blurHeight / desc.Height;
            context->UpdateSubresource(constants_.Get(), 0, nullptr, &p, 0, 0);
            context->Draw(3, 0);
        }
    }
    viewport.Width = static_cast<float>(desc.Width);
    viewport.Height = static_cast<float>(desc.Height);
    context->RSSetViewports(1, &viewport);
    context->RSSetScissorRects(1, &clip);
    context->PSSetShaderResources(0, 2, empty);
    context->OMSetRenderTargets(1, &target, nullptr);
    ID3D11ShaderResourceView *views[]{surfaces_[0].resource.Get(),
                                      config.glowEnabled ? surfaces_[2].resource.Get() : surfaces_[0].resource.Get()};
    context->PSSetShaderResources(0, 2, views);
    context->PSSetShader(compositeShader_.Get(), nullptr, 0);
    context->OMSetBlendState(alphaBlend_.Get(), nullptr, 0xFFFFFFFF);
    context->Draw(3, 0);
    context->PSSetShaderResources(0, 2, empty);
    state.status = EffectsStatus::Ready;
    return S_OK;
#undef CHECK
}
} // namespace awareness
