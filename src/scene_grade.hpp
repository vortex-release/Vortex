#pragma once
#include "combat_features.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
namespace awareness {
// Runs inside the overlay's isolated context state, before its HUD and menu.
class SceneGrade {
    template <class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11VertexShader> vs_;
    Ptr<ID3D11PixelShader> ps_;
    Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11SamplerState> sampler_;
    Ptr<ID3D11RasterizerState> raster_;
    Ptr<ID3D11DepthStencilState> depth_;
    Ptr<ID3D11Texture2D> image_;
    Ptr<ID3D11ShaderResourceView> view_;
    D3D11_TEXTURE2D_DESC size_{};
    struct Values {
        float gain, contrast, saturation, vignette;
        float tint[4];
        float tone[4];
        float highlights[4];
    } previous_{};
    bool uploaded_{};
    HRESULT Initialize(ID3D11Device *device) {
        static constexpr char shader[] = R"(
cbuffer Grade:register(b0) { float gain, contrast, saturation, vignette; float4 tint; float4 tone; float4 highlights; };
Texture2D scene:register(t0); SamplerState linearSampler:register(s0);
struct Output { float4 p:SV_POSITION; float2 uv:TEXCOORD; };
Output VS(uint id:SV_VertexID) {
    Output o; o.uv=float2((id<<1)&2,id&2); o.p=float4(o.uv*float2(2,-2)+float2(-1,1),0,1); return o;
}
float4 PS(Output i):SV_TARGET {
    float4 source=scene.Sample(linearSampler,i.uv);
    float3 rgb=source.rgb*gain;
    rgb=(rgb-.5)*contrast+.5;
    float grey=dot(rgb,float3(.2126,.7152,.0722));
    float chroma=saturate(max(rgb.r,max(rgb.g,rgb.b))-min(rgb.r,min(rgb.g,rgb.b)));
    rgb=lerp(grey.xxx,rgb,saturation*(1+tone.y*(1-chroma)));
    rgb*=float3(1+.15*tone.z,1,1-.15*tone.z);
    rgb+=tone.w*pow(1-saturate(grey),2)+highlights.x*smoothstep(.4,1,grey);
    rgb=pow(max(rgb,0),1/tone.x);
    rgb*=lerp(float3(1,1,1),tint.rgb,tint.a);
    float2 edge=i.uv*2-1;
    rgb*=1-vignette*smoothstep(.2,1.5,dot(edge,edge));
    return float4(saturate(rgb),source.a);
})";
        Ptr<ID3DBlob> vs, ps, error;
        HRESULT hr;
        if (FAILED(hr = D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "VS", "vs_5_0",
                                   D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &error)) ||
            FAILED(hr = D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "PS", "ps_5_0",
                                   D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &error)))
            return hr;
        if (FAILED(hr = device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vs_)) ||
            FAILED(hr = device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &ps_)))
            return hr;
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(Values);
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(hr = device->CreateBuffer(&buffer, nullptr, &constants_)))
            return hr;
        D3D11_SAMPLER_DESC sample{};
        sample.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sample.AddressU = sample.AddressV = sample.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sample.MaxLOD = D3D11_FLOAT32_MAX;
        sample.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        if (FAILED(hr = device->CreateSamplerState(&sample, &sampler_)))
            return hr;
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        if (FAILED(hr = device->CreateRasterizerState(&raster, &raster_)))
            return hr;
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
        return device->CreateDepthStencilState(&depth, &depth_);
    }

  public:
    bool Active(const combat::Options &o) const noexcept {
        return o.contrast &&
               (o.worldDarkness > 0 || o.sceneExposure != 0 || o.sceneContrast != 1 || o.sceneSaturation != 1 ||
                o.sceneVignette > 0 || o.sceneTintStrength > 0 || o.sceneGamma != 1 || o.sceneVibrance != 0 ||
                o.sceneTemperature != 0 || o.sceneShadows != 0 || o.sceneHighlights != 0);
    }
    HRESULT Render(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *source,
                   ID3D11RenderTargetView *target, const combat::Options &o) {
        if (!Active(o))
            return S_FALSE;
        if (!device || !context || !source || !target || !combat::Valid(o))
            return E_INVALIDARG;
        // A renderer can survive swap-chain/device recovery. Cached objects from
        // the old device must never be submitted on its replacement context.
        if (device_.Get() != device) {
            *this = SceneGrade{};
            device_ = device;
        }
        HRESULT hr;
        if ((!vs_ || !ps_ || !constants_ || !sampler_ || !raster_ || !depth_) && FAILED(hr = Initialize(device)))
            return hr;
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        if (desc.SampleDesc.Count != 1 || desc.MipLevels != 1 || desc.ArraySize != 1)
            return S_FALSE;
        if (!view_ || desc.Width != size_.Width || desc.Height != size_.Height || desc.Format != size_.Format) {
            view_.Reset();
            image_.Reset();
            size_ = desc;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.CPUAccessFlags = desc.MiscFlags = 0;
            if (FAILED(hr = device->CreateTexture2D(&desc, nullptr, &image_)) ||
                FAILED(hr = device->CreateShaderResourceView(image_.Get(), nullptr, &view_)))
                return hr;
        }
        const Values values{std::exp2(o.sceneExposure) * (1 - o.worldDarkness),
                            o.sceneContrast,
                            o.sceneSaturation,
                            o.sceneVignette,
                            {o.sceneTint.r, o.sceneTint.g, o.sceneTint.b, o.sceneTintStrength},
                            {o.sceneGamma, o.sceneVibrance, o.sceneTemperature, o.sceneShadows},
                            {o.sceneHighlights, 0, 0, 0}};
        if (!uploaded_ || std::memcmp(&previous_, &values, sizeof(values))) {
            context->UpdateSubresource(constants_.Get(), 0, nullptr, &values, 0, 0);
            previous_ = values;
            uploaded_ = true;
        }
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->CopyResource(image_.Get(), source);
        context->OMSetRenderTargets(1, &target, nullptr);
        context->OMSetBlendState(nullptr, nullptr, ~0u);
        context->OMSetDepthStencilState(depth_.Get(), 0);
        context->RSSetState(raster_.Get());
        const D3D11_VIEWPORT viewport{0, 0, float(size_.Width), float(size_.Height), 0, 1};
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs_.Get(), nullptr, 0);
        context->PSSetShader(ps_.Get(), nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        auto *buffer = constants_.Get();
        auto *sampler = sampler_.Get();
        auto *view = view_.Get();
        context->PSSetConstantBuffers(0, 1, &buffer);
        context->PSSetSamplers(0, 1, &sampler);
        context->PSSetShaderResources(0, 1, &view);
        context->Draw(3, 0);
        view = nullptr;
        context->PSSetShaderResources(0, 1, &view);
        return S_OK;
    }
};
} // namespace awareness
