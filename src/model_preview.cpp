#include "model_preview.hpp"
#include "runtime_support.hpp"
#include "image_texture.hpp"
#include "configuration.hpp"
#include "entity_filter.hpp"
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <span>
#include <vector>

namespace awareness {
using namespace DirectX;
using Microsoft::WRL::ComPtr;
namespace {
struct Bone {
    std::int32_t parent;
    XMFLOAT3 translation;
    XMFLOAT4 rotation;
    XMFLOAT3 scale;
    XMFLOAT4X4 inverse;
};
struct Vertex {
    XMFLOAT3 position, normal;
    XMFLOAT2 uv;
    std::array<UINT, 4> joints;
    XMFLOAT4 weights;
};
struct Part {
    UINT start, count, material;
};
struct Constants {
    XMFLOAT4X4 viewProjection;
    XMFLOAT4X4 skin[128];
    XMFLOAT4 color, options;
};
static_assert(sizeof(Bone) == 108 && sizeof(Vertex) == 64);
std::span<const BYTE> Resource(int id) {
    const auto resource = FindResourceW(OverlayModule(), MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource)
        return {};
    const auto *p = static_cast<const BYTE *>(LockResource(LoadResource(OverlayModule(), resource)));
    return p ? std::span<const BYTE>(p, SizeofResource(OverlayModule(), resource)) : std::span<const BYTE>{};
}
constexpr char Shader[] = R"(
cbuffer Scene : register(b0) {
    row_major float4x4 viewProjection;
    row_major float4x4 skin[128];
    float4 color; float4 options;
};
struct Input { float3 p:POSITION; float3 n:NORMAL; float2 uv:TEXCOORD; uint4 joints:BLENDINDICES; float4 weights:BLENDWEIGHT; };
struct Output { float4 p:SV_POSITION; float3 n:NORMAL; float2 uv:TEXCOORD; };
Output VS(Input v) {
    row_major float4x4 m=skin[v.joints.x]*v.weights.x+skin[v.joints.y]*v.weights.y+
                        skin[v.joints.z]*v.weights.z+skin[v.joints.w]*v.weights.w;
    Output o; o.p=mul(mul(float4(v.p,1),m),viewProjection);
    o.n=normalize(mul(float4(v.n,0),m).xyz); o.uv=v.uv; return o;
}
Texture2D albedo:register(t0);
SamplerState linearSampler:register(s0);
float4 PS(Output v):SV_TARGET {
    if(options.y>0) {
        float lighting=.55+.45*saturate(dot(normalize(v.n),normalize(float3(-.4,.7,1))));
        return float4(color.rgb*lighting,color.a);
    }
    float3 n=normalize(v.n);
    float light=.48+.57*saturate(dot(n,normalize(float3(-.4,.7,1))))+.2*saturate(dot(n,float3(1,0,0)));
    float3 base=albedo.Sample(linearSampler,v.uv).rgb*light;
    if(options.x>0) base=lerp(base,color.rgb,color.a);
    return float4(base,1);
})";
} // namespace
struct ModelPreview::Data {
    ComPtr<ID3D11Buffer> vertices, indices, constants;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depthState;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11ShaderResourceView> view;
    ComPtr<ID3D11DepthStencilView> depth, ghostDepth;
    ComPtr<ID3D11BlendState> ghostBlend, ghostMask;
    ComPtr<ID3D11DepthStencilState> ghostEqual, ghostLess, ghostGreater;
    ComPtr<ID3D11Texture2D> ghostDepthTexture;
    DXGI_FORMAT ghostFormat{DXGI_FORMAT_UNKNOWN};
    UINT ghostWidth{}, ghostHeight{};
    std::array<ComPtr<ID3D11ShaderResourceView>, 5> textures;
    std::vector<Bone> bones;
    UINT indexCount{};
    std::vector<Part> parts;
    float time{}, angle{.22f};
};
ModelPreview::ModelPreview() = default;
ModelPreview::~ModelPreview() = default;
HRESULT ModelPreview::Initialize(ID3D11Device *device) {
    if (!device)
        return E_INVALIDARG;
    auto data = std::make_unique<Data>();
    auto bytes = Resource(102);
    UINT header[6]{};
    if (bytes.size() < sizeof(header))
        return E_FAIL;
    std::memcpy(header, bytes.data(), sizeof(header));
    bytes = bytes.subspan(sizeof(header));
    if (header[0] != 0x36565250 || header[1] != 1 || !header[2] || header[2] > 100000 || !header[3] ||
        header[3] > 300000 || header[4] != 94 || header[5] > 5)
        return E_INVALIDARG;
    const auto expected = std::size_t(header[4]) * sizeof(Bone) + std::size_t(header[2]) * sizeof(Vertex) +
                          std::size_t(header[3]) * sizeof(UINT) + std::size_t(header[5]) * sizeof(Part);
    if (bytes.size() != expected)
        return E_INVALIDARG;
    data->bones.resize(header[4]);
    std::memcpy(data->bones.data(), bytes.data(), data->bones.size() * sizeof(Bone));
    bytes = bytes.subspan(data->bones.size() * sizeof(Bone));
    const auto createBuffer = [&](UINT size, UINT flags, const void *initial, ComPtr<ID3D11Buffer> &buffer) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = size;
        desc.BindFlags = flags;
        desc.Usage = initial ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA resource{initial, 0, 0};
        return device->CreateBuffer(&desc, initial ? &resource : nullptr, &buffer);
    };
    HRESULT hr = createBuffer(header[2] * sizeof(Vertex), D3D11_BIND_VERTEX_BUFFER, bytes.data(), data->vertices);
    if (FAILED(hr))
        return hr;
    bytes = bytes.subspan(std::size_t(header[2]) * sizeof(Vertex));
    if (FAILED(hr = createBuffer(header[3] * sizeof(UINT), D3D11_BIND_INDEX_BUFFER, bytes.data(), data->indices)))
        return hr;
    data->indexCount = header[3];
    bytes = bytes.subspan(std::size_t(header[3]) * sizeof(UINT));
    data->parts.resize(header[5]);
    std::memcpy(data->parts.data(), bytes.data(), bytes.size());
    for (const auto &part : data->parts)
        if (part.material >= 5 || part.start > header[3] || part.count > header[3] - part.start)
            return E_INVALIDARG;
    for (std::size_t i = 0; i < data->bones.size(); ++i)
        if (data->bones[i].parent < -1 || data->bones[i].parent >= static_cast<int>(data->bones.size()) ||
            data->bones[i].parent == i)
            return E_INVALIDARG;
    if (FAILED(hr = createBuffer(sizeof(Constants), D3D11_BIND_CONSTANT_BUFFER, nullptr, data->constants)))
        return hr;
    ComPtr<ID3DBlob> vertexCode, pixelCode, error;
    if (FAILED(hr = D3DCompile(Shader, sizeof(Shader), nullptr, nullptr, nullptr, "VS", "vs_5_0",
                               D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vertexCode, &error)) ||
        FAILED(hr = D3DCompile(Shader, sizeof(Shader), nullptr, nullptr, nullptr, "PS", "ps_5_0",
                               D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pixelCode, &error)))
        return hr;
    if (FAILED(hr = device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr,
                                               &data->vs)) ||
        FAILED(hr = device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr,
                                              &data->ps)))
        return hr;
    const D3D11_INPUT_ELEMENT_DESC elements[]{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    if (FAILED(hr = device->CreateInputLayout(elements, 5, vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(),
                                              &data->layout)))
        return hr;
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    if (FAILED(hr = device->CreateRasterizerState(&raster, &data->raster)))
        return hr;
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D11_COMPARISON_LESS;
    if (FAILED(hr = device->CreateDepthStencilState(&depth, &data->depthState)))
        return hr;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(hr = device->CreateSamplerState(&sampler, &data->sampler)))
        return hr;
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = 384;
    texture.Height = 576;
    texture.ArraySize = texture.MipLevels = texture.SampleDesc.Count = 1;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> color;
    if (FAILED(hr = device->CreateTexture2D(&texture, nullptr, &color)) ||
        FAILED(hr = device->CreateRenderTargetView(color.Get(), nullptr, &data->target)) ||
        FAILED(hr = device->CreateShaderResourceView(color.Get(), nullptr, &data->view)))
        return hr;
    texture.Format = DXGI_FORMAT_D32_FLOAT;
    texture.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depthTexture;
    if (FAILED(hr = device->CreateTexture2D(&texture, nullptr, &depthTexture)) ||
        FAILED(hr = device->CreateDepthStencilView(depthTexture.Get(), nullptr, &data->depth)))
        return hr;
    for (int i = 0; i < 5; ++i) {
        ImagePixels pixels;
        if (FAILED(hr = DecodeImage(nullptr, Resource(111 + i), pixels)) ||
            FAILED(hr = UploadImage(device, pixels, data->textures[i])))
            return hr;
    }
    data_ = std::move(data);
    return S_OK;
}
HRESULT ModelPreview::Render(ID3D11DeviceContext *context, const PreviewPose *pose, float seconds, bool rotate,
                             const EffectsConfiguration &effects, const GhostSurface *ghost) {
    if (!data_)
        return E_FAIL;
    if (!context || !std::isfinite(seconds) || !ValidEffectsConfiguration(effects))
        return E_INVALIDARG;
    auto &d = *data_;
    const auto delta = std::clamp(seconds, 0.f, .1f);
    d.time = std::fmod(d.time + delta, XM_2PI / 1.7f);
    if (rotate)
        d.angle = std::remainder(d.angle + delta * .22f, XM_2PI);
    std::array<XMMATRIX, 128> world{};
    std::array<BYTE, 128> visited{};
    const bool live = pose && pose->valid;
    const auto basis = XMMatrixSet(0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1);
    const auto evaluate = [&](auto &&self, std::size_t i) -> bool {
        if (visited[i] == 2)
            return true;
        if (visited[i] == 1)
            return false;
        visited[i] = 1;
        const auto &bone = d.bones[i];
        if (live && i < PreviewJointCount && (pose->validMask & (1u << i))) {
            const auto &j = pose->joints[i];
            const auto position = j.position - pose->entity.origin;
            const auto q =
                XMQuaternionNormalize(XMVectorSet(j.rotation[0], j.rotation[1], j.rotation[2], j.rotation[3]));
            world[i] = XMMatrixScaling(j.scale, j.scale, j.scale) * XMMatrixRotationQuaternion(q) * basis;
            world[i].r[3] = XMVectorSet(position.y * .0254f, position.z * .0254f, position.x * .0254f, 1);
        } else {
            auto rotation = XMLoadFloat4(&bone.rotation);
            if (!live && (i == 3 || i == 4))
                rotation = XMQuaternionMultiply(
                    rotation, XMQuaternionRotationAxis(XMVectorSet(0, 0, 1, 0), std::sin(d.time * 1.7f) * .012f));
            const auto local = XMMatrixScaling(bone.scale.x, bone.scale.y, bone.scale.z) *
                               XMMatrixRotationQuaternion(rotation) *
                               XMMatrixTranslation(bone.translation.x, bone.translation.y, bone.translation.z);
            if (bone.parent >= 0) {
                if (!self(self, static_cast<std::size_t>(bone.parent)))
                    return false;
                world[i] = local * world[bone.parent];
            } else
                world[i] = local;
        }
        visited[i] = 2;
        return true;
    };
    Constants constants{};
    for (std::size_t i = 0; i < d.bones.size(); ++i) {
        if (!evaluate(evaluate, i))
            return E_INVALIDARG;
        XMStoreFloat4x4(&constants.skin[i], XMLoadFloat4x4(&d.bones[i].inverse) * world[i]);
    }
    const auto view = XMMatrixLookAtRH(XMVectorSet(std::sin(d.angle) * 3, 1.1f, std::cos(d.angle) * 3, 1),
                                       XMVectorSet(0, .95f, 0, 1), XMVectorSet(0, 1, 0, 0));
    XMStoreFloat4x4(&constants.viewProjection, view * XMMatrixOrthographicRH(1.6f, 2.4f, .1f, 10));
    if (ghost && live) {
        const auto restore = XMMatrixTranspose(basis) * XMMatrixScaling(1 / .0254f, 1 / .0254f, 1 / .0254f) *
                             XMMatrixTranslation(pose->entity.origin.x, pose->entity.origin.y, pose->entity.origin.z);
        for (std::size_t i = 0; i < d.bones.size(); ++i)
            XMStoreFloat4x4(&constants.skin[i], XMLoadFloat4x4(&d.bones[i].inverse) * world[i] * restore);
        XMFLOAT4X4 vp;
        std::memcpy(&vp, &ghost->matrix, sizeof(vp));
        XMStoreFloat4x4(&constants.viewProjection, XMMatrixTranspose(XMLoadFloat4x4(&vp)));
        constants.options.y = 1;
    }
    constants.color = {effects.materialColor.r, effects.materialColor.g, effects.materialColor.b,
                       effects.materialColor.a};
    constants.options.x = (effects.materialEnabled || effects.glowEnabled) ? 1.f : 0.f;
    context->UpdateSubresource(d.constants.Get(), 0, nullptr, &constants, 0, 0);
    ID3D11ShaderResourceView *empty{};
    context->PSSetShaderResources(0, 1, &empty);
    const float clear[4]{};
    if (!ghost) {
        context->ClearRenderTargetView(d.target.Get(), clear);
        context->ClearDepthStencilView(d.depth.Get(), D3D11_CLEAR_DEPTH, 1, 0);
    }
    auto *target = ghost ? ghost->target : d.target.Get();
    context->OMSetRenderTargets(1, &target, ghost ? ghost->depth : d.depth.Get());
    context->OMSetDepthStencilState(
        ghost ? (ghost->reversed ? d.ghostGreater.Get() : d.ghostLess.Get()) : d.depthState.Get(), 0);
    context->OMSetBlendState(ghost && !ghost->mask ? d.ghostBlend.Get() : nullptr, nullptr, ~0u);
    context->RSSetState(d.raster.Get());
    const D3D11_VIEWPORT viewport =
        ghost
            ? D3D11_VIEWPORT{ghost->viewport.x, ghost->viewport.y, ghost->viewport.width, ghost->viewport.height, 0, 1}
            : D3D11_VIEWPORT{0, 0, 384, 576, 0, 1};
    context->RSSetViewports(1, &viewport);
    context->IASetInputLayout(d.layout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(Vertex), offset = 0;
    auto *vertices = d.vertices.Get();
    context->IASetVertexBuffers(0, 1, &vertices, &stride, &offset);
    context->IASetIndexBuffer(d.indices.Get(), DXGI_FORMAT_R32_UINT, 0);
    context->VSSetShader(d.vs.Get(), nullptr, 0);
    context->PSSetShader(d.ps.Get(), nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    auto *buffer = d.constants.Get();
    context->VSSetConstantBuffers(0, 1, &buffer);
    context->PSSetConstantBuffers(0, 1, &buffer);
    auto *sampler = d.sampler.Get();
    context->PSSetSamplers(0, 1, &sampler);
    const auto drawParts = [&] {
        if (ghost) {
            context->DrawIndexed(d.indexCount, 0, 0);
            return;
        }
        for (const auto &part : d.parts) {
            auto *texture = d.textures[part.material].Get();
            context->PSSetShaderResources(0, 1, &texture);
            context->DrawIndexed(part.count, part.start, 0);
        }
    };
    if (ghost && !ghost->mask) {
        // Resolve the nearest surface before blending. Hidden mesh pieces must not
        // accumulate opacity inside one character silhouette.
        context->OMSetBlendState(d.ghostMask.Get(), nullptr, ~0u);
        drawParts();
        context->OMSetDepthStencilState(d.ghostEqual.Get(), 0);
        context->OMSetBlendState(d.ghostBlend.Get(), nullptr, ~0u);
    }
    drawParts();
    context->OMSetRenderTargets(0, nullptr, nullptr);
    return S_OK;
}
HRESULT ModelPreview::RenderGhosts(ID3D11DeviceContext *context, ID3D11RenderTargetView *target,
                                   const FrameSnapshot &frame, Viewport viewport, const Configuration &config,
                                   const combat::Options &options, const combat::ReplayFrame &history, double now,
                                   ID3D11DepthStencilView *sceneDepth, bool reversed) {
    if (!data_ || !options.ghosts || !target)
        return S_FALSE;
    if (!context || !std::isfinite(now) || !std::isfinite(history.sampledAt) || !Valid(viewport) ||
        history.count > history.actors.size())
        return E_INVALIDARG;
    struct Candidate {
        const combat::ReplayActor *track;
        float distance;
    };
    std::array<Candidate, MaxEntities> candidates{};
    std::size_t count{};
    for (std::uint32_t i = 0; i < history.count; ++i) {
        const auto &track = history.actors[i];
        if (!track.handle || !track.ready || !std::isfinite(track.movement) || track.movement <= .01f ||
            now < history.sampledAt || now - history.sampledAt > .15)
            continue;
        const auto &pose = track.past;
        if (EntityOpacity(frame, pose.entity, config) <= 0)
            continue;
        candidates[count++] = {&track, Distance(frame.cameraOrigin, pose.entity.origin)};
    }
    std::sort(candidates.begin(), candidates.begin() + count, [](auto a, auto b) { return a.distance < b.distance; });
    // One delayed player per history; cap expensive mesh work to eight nearby actors.
    count = std::min<std::size_t>(count, 8);
    if (!count)
        return S_FALSE;
    auto &d = *data_;
    ComPtr<ID3D11Device> device;
    context->GetDevice(&device);
    if (!d.ghostBlend || !d.ghostMask || !d.ghostEqual || !d.ghostLess || !d.ghostGreater) {
        d.ghostBlend.Reset();
        d.ghostMask.Reset();
        d.ghostEqual.Reset();
        d.ghostLess.Reset();
        d.ghostGreater.Reset();
        D3D11_BLEND_DESC blend{};
        auto &b = blend.RenderTarget[0];
        b.BlendEnable = TRUE;
        b.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        b.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        b.BlendOp = D3D11_BLEND_OP_ADD;
        b.SrcBlendAlpha = D3D11_BLEND_ONE;
        b.DestBlendAlpha = D3D11_BLEND_ONE;
        b.BlendOpAlpha = D3D11_BLEND_OP_MAX;
        b.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        const auto hr = device->CreateBlendState(&blend, &d.ghostBlend);
        if (FAILED(hr))
            return hr;
        D3D11_BLEND_DESC mask{};
        auto result = device->CreateBlendState(&mask, &d.ghostMask);
        if (FAILED(result))
            return result;
        D3D11_DEPTH_STENCIL_DESC equal{};
        equal.DepthEnable = TRUE;
        equal.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        equal.DepthFunc = D3D11_COMPARISON_EQUAL;
        result = device->CreateDepthStencilState(&equal, &d.ghostEqual);
        if (FAILED(result))
            return result;
        equal.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        equal.DepthFunc = D3D11_COMPARISON_LESS;
        if (FAILED(result = device->CreateDepthStencilState(&equal, &d.ghostLess)))
            return result;
        equal.DepthFunc = D3D11_COMPARISON_GREATER;
        if (FAILED(result = device->CreateDepthStencilState(&equal, &d.ghostGreater)))
            return result;
    }
    ComPtr<ID3D11Resource> targetResource;
    ComPtr<ID3D11Texture2D> targetTexture, sourceDepth;
    target->GetResource(&targetResource);
    if (FAILED(targetResource.As(&targetTexture)))
        return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC targetDesc{}, depthDesc{};
    targetTexture->GetDesc(&targetDesc);
    if (targetDesc.SampleDesc.Count != 1)
        return S_FALSE;
    const UINT width = targetDesc.Width, height = targetDesc.Height;
    D3D11_DEPTH_STENCIL_VIEW_DESC depthView{};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = depthDesc.ArraySize = depthDesc.SampleDesc.Count = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthView.Format = DXGI_FORMAT_D32_FLOAT;
    depthView.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    if (sceneDepth) {
        ComPtr<ID3D11Resource> resource;
        sceneDepth->GetResource(&resource);
        D3D11_TEXTURE2D_DESC sourceDesc{};
        if (SUCCEEDED(resource.As(&sourceDepth)))
            sourceDepth->GetDesc(&sourceDesc);
        if (sourceDesc.Width == width && sourceDesc.Height == height && sourceDesc.SampleDesc.Count == 1 &&
            sourceDesc.MipLevels == 1 && sourceDesc.ArraySize == 1) {
            depthDesc = sourceDesc;
            sceneDepth->GetDesc(&depthView);
            depthView.Flags = 0;
        } else
            sourceDepth.Reset();
    }
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.CPUAccessFlags = depthDesc.MiscFlags = 0;
    if (!d.ghostDepth || d.ghostWidth != width || d.ghostHeight != height || d.ghostFormat != depthDesc.Format) {
        d.ghostDepth.Reset();
        d.ghostDepthTexture.Reset();
        HRESULT hr = device->CreateTexture2D(&depthDesc, nullptr, &d.ghostDepthTexture);
        if (FAILED(hr))
            return hr;
        if (FAILED(hr = device->CreateDepthStencilView(d.ghostDepthTexture.Get(), &depthView, &d.ghostDepth)))
            return hr;
        d.ghostWidth = width;
        d.ghostHeight = height;
        d.ghostFormat = depthDesc.Format;
    }
    // Use a private copy when the host supplies scene depth; never write to it.
    if (sourceDepth)
        context->CopyResource(d.ghostDepthTexture.Get(), sourceDepth.Get());
    else
        context->ClearDepthStencilView(d.ghostDepth.Get(), D3D11_CLEAR_DEPTH, reversed ? 0.f : 1.f, 0);
    GhostSurface surface{frame.viewProjection, viewport, target, d.ghostDepth.Get(), false, reversed};
    for (std::size_t t = count; t-- > 0;) {
        const auto &track = *candidates[t].track;
        {
            const auto &pose = track.past;
            EffectsConfiguration effects;
            effects.materialColor = pose.entity.team == frame.localTeam ? options.ghostFriend : options.ghostEnemy;
            effects.materialColor.a *=
                options.ghostOpacity * track.movement * EntityOpacity(frame, pose.entity, config);
            if (effects.materialColor.a < .006f)
                continue;
            const auto hr = Render(context, &pose, 0, false, effects, &surface);
            if (FAILED(hr))
                return hr;
        }
    }
    return S_OK;
}
void ModelPreview::Drag(float amount) noexcept {
    if (data_ && std::isfinite(amount))
        data_->angle = std::remainder(data_->angle + amount, XM_2PI);
}
ID3D11ShaderResourceView *ModelPreview::View() const noexcept {
    return data_ ? data_->view.Get() : nullptr;
}
} // namespace awareness
