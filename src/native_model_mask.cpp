#include "native_model_mask.hpp"
#include "native_draw_annotations.hpp"
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <utility>
namespace awareness::native_mask {
namespace {
using Microsoft::WRL::ComPtr;
thread_local std::uint32_t selectedColor{};
thread_local bool internal{};
struct InternalScope {
    bool previous{internal};
    InternalScope() { internal = true; }
    ~InternalScope() { internal = previous; }
};
constexpr char Shader[] = R"(
cbuffer Settings:register(b0) { float4 color; uint reversed; float bias; float2 padding; };
Texture2D<float4> colors:register(t0);
Texture2D<float> depths:register(t1);
float4 CapturePS(float4 p:SV_POSITION):SV_TARGET { return color; }
float4 ScreenVS(uint id:SV_VertexID):SV_POSITION {
    float2 uv=float2((id<<1)&2,id&2); return float4(uv*float2(2,-2)+float2(-1,1),0,1);
}
struct Result { float4 color:SV_TARGET; float depth:SV_DEPTH; };
Result CompositePS(float4 p:SV_POSITION) {
    Result result; result.color=colors.Load(int3(int2(p.xy),0));
    clip(result.color.a-0.001);
    float z=depths.Load(int3(int2(p.xy),0));
    result.depth=saturate(z+(reversed?bias:-bias));
    return result;
}
)";
struct Constants {
    float color[4]{};
    unsigned reversed{};
    float bias{0.000001f};
    float padding[2]{};
};
struct Resources {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Texture2D> color, depth;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> depthView;
    ComPtr<ID3D11ShaderResourceView> colorResource, depthResource;
    ComPtr<ID3D11PixelShader> capture, composite;
    ComPtr<ID3D11VertexShader> screen;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11DepthStencilState> nearState[2], behindState[2];
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11RasterizerState> raster;
    unsigned width{}, height{};
};
struct State {
    std::mutex mutex;
    Resources resources;
    Annotations<> annotations;
    std::atomic<ID3D11DeviceContext *> watched{};
    std::atomic<bool> enabled{};
    std::atomic<ULONGLONG> deadline{};
    std::atomic<std::uintptr_t> replaySite{};
    std::atomic<unsigned> queued{}, matched{}, dropped{}, captured{};
    std::array<ID3D11Asynchronous *, 16> queries{};
    std::atomic<unsigned> queryCount{};
    unsigned queryOverflow{};
    unsigned draws{};
    bool reversed{};
} state;
HRESULT Create(ID3D11Device *device, UINT width, UINT height, Resources &out) {
    Resources next;
    next.device = device;
    next.width = width;
    next.height = height;
    HRESULT hr;
#define MASK_CHECK(x)                                                                                                  \
    if (FAILED(hr = (x)))                                                                                              \
    return hr
    ComPtr<ID3DBlob> code, errors;
    const auto compile = [&](const char *entry, const char *profile) {
        code.Reset();
        errors.Reset();
        return D3DCompile(Shader, sizeof(Shader) - 1, "NativeModelMask", nullptr, nullptr, entry, profile,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    };
    MASK_CHECK(compile("CapturePS", "ps_5_0"));
    MASK_CHECK(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &next.capture));
    MASK_CHECK(compile("CompositePS", "ps_5_0"));
    MASK_CHECK(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &next.composite));
    MASK_CHECK(compile("ScreenVS", "vs_5_0"));
    MASK_CHECK(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &next.screen));
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Constants);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    MASK_CHECK(device->CreateBuffer(&cb, nullptr, &next.constants));
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = width;
    texture.Height = height;
    texture.ArraySize = texture.MipLevels = 1;
    texture.SampleDesc.Count = 1;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    MASK_CHECK(device->CreateTexture2D(&texture, nullptr, &next.color));
    MASK_CHECK(device->CreateRenderTargetView(next.color.Get(), nullptr, &next.target));
    MASK_CHECK(device->CreateShaderResourceView(next.color.Get(), nullptr, &next.colorResource));
    texture.Format = DXGI_FORMAT_R32_TYPELESS;
    texture.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    MASK_CHECK(device->CreateTexture2D(&texture, nullptr, &next.depth));
    D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    MASK_CHECK(device->CreateDepthStencilView(next.depth.Get(), &dsv, &next.depthView));
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    MASK_CHECK(device->CreateShaderResourceView(next.depth.Get(), &srv, &next.depthResource));
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = TRUE;
    for (unsigned i = 0; i < 2; ++i) {
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = i ? D3D11_COMPARISON_GREATER_EQUAL : D3D11_COMPARISON_LESS_EQUAL;
        MASK_CHECK(device->CreateDepthStencilState(&depth, &next.nearState[i]));
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depth.DepthFunc = i ? D3D11_COMPARISON_LESS : D3D11_COMPARISON_GREATER;
        MASK_CHECK(device->CreateDepthStencilState(&depth, &next.behindState[i]));
    }
    D3D11_BLEND_DESC blend{};
    auto &b = blend.RenderTarget[0];
    b.BlendEnable = TRUE;
    b.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    b.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    b.BlendOp = D3D11_BLEND_OP_ADD;
    b.SrcBlendAlpha = D3D11_BLEND_ONE;
    b.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    b.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    b.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    MASK_CHECK(device->CreateBlendState(&blend, &next.blend));
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    MASK_CHECK(device->CreateRasterizerState(&raster, &next.raster));
    out = std::move(next);
    return S_OK;
#undef MASK_CHECK
}
// Only modified stages are saved. Vertex/bone/instance/index buffers stay bound and are never retained.
struct Bindings {
    ID3D11DeviceContext *context;
    ID3D11RenderTargetView *targets[8]{};
    ID3D11UnorderedAccessView *uavs[64]{};
    ID3D11DepthStencilView *depth{};
    ID3D11DepthStencilState *depthState{};
    ID3D11BlendState *blend{};
    ID3D11PixelShader *pixel{};
    ID3D11ClassInstance *classes[256]{};
    ID3D11Buffer *constant{};
    ID3D11Predicate *predicate{};
    BOOL predicateValue{};
    UINT classCount{256}, stencil{}, sampleMask{}, uavCount{};
    FLOAT factors[4]{};
    explicit Bindings(ID3D11DeviceContext *c, UINT count) : context(c), uavCount(count) {
        c->OMGetRenderTargetsAndUnorderedAccessViews(8, targets, &depth, 0, uavCount, uavs);
        c->OMGetDepthStencilState(&depthState, &stencil);
        c->OMGetBlendState(&blend, factors, &sampleMask);
        c->PSGetShader(&pixel, classes, &classCount);
        c->PSGetConstantBuffers(0, 1, &constant);
        c->GetPredication(&predicate, &predicateValue);
    }
    ~Bindings() {
        UINT count{};
        for (unsigned i = 0; i < 8; ++i)
            if (targets[i])
                count = i + 1;
        context->OMSetRenderTargetsAndUnorderedAccessViews(count, targets, depth, count, uavCount - count, uavs + count,
                                                           nullptr);
        context->OMSetDepthStencilState(depthState, stencil);
        context->OMSetBlendState(blend, factors, sampleMask);
        context->PSSetShader(pixel, classes, classCount);
        context->PSSetConstantBuffers(0, 1, &constant);
        context->SetPredication(predicate, predicateValue);
        for (auto *v : targets)
            if (v)
                v->Release();
        for (auto *v : uavs)
            if (v)
                v->Release();
        for (UINT i = 0; i < classCount; ++i)
            if (classes[i])
                classes[i]->Release();
        if (depth)
            depth->Release();
        if (depthState)
            depthState->Release();
        if (blend)
            blend->Release();
        if (pixel)
            pixel->Release();
        if (constant)
            constant->Release();
        if (predicate)
            predicate->Release();
    }
};
void Submit(ID3D11DeviceContext *c, const Draw &d) {
    const auto &a = d.args;
    switch (d.kind) {
    case DrawKind::Indexed:
        c->DrawIndexed(a[0], a[1], static_cast<INT>(a[2]));
        break;
    case DrawKind::Vertices:
        c->Draw(a[0], a[1]);
        break;
    case DrawKind::IndexedInstanced:
        c->DrawIndexedInstanced(a[0], a[1], a[2], static_cast<INT>(a[3]), a[4]);
        break;
    case DrawKind::Instanced:
        c->DrawInstanced(a[0], a[1], a[2], a[3]);
        break;
    case DrawKind::Auto:
        c->DrawAuto();
        break;
    case DrawKind::IndexedIndirect:
        c->DrawIndexedInstancedIndirect(d.indirect, a[0]);
        break;
    case DrawKind::Indirect:
        c->DrawInstancedIndirect(d.indirect, a[0]);
        break;
    }
}
bool DepthMatches(ID3D11DepthStencilView *depth, ID3D11Device *device, UINT width, UINT height, bool single) {
    if (!depth)
        return false;
    ComPtr<ID3D11Device> owner;
    depth->GetDevice(&owner);
    if (owner.Get() != device)
        return false;
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Texture2D> texture;
    depth->GetResource(&resource);
    if (FAILED(resource.As(&texture)))
        return false;
    D3D11_TEXTURE2D_DESC d{};
    texture->GetDesc(&d);
    D3D11_DEPTH_STENCIL_VIEW_DESC view{};
    depth->GetDesc(&view);
    const bool validView = d.SampleDesc.Count == 1
                               ? (view.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2D && view.Texture2D.MipSlice == 0)
                               : view.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2DMS;
    return validView && d.Width == width && d.Height == height && d.ArraySize == 1 && d.MipLevels == 1 &&
           d.SampleDesc.Count > 0 && d.SampleDesc.Count <= 16 && (!single || d.SampleDesc.Count == 1);
}
} // namespace
Scope::Scope(std::uint32_t color) noexcept : previous_(selectedColor) {
    selectedColor = color;
}
Scope::~Scope() {
    selectedColor = previous_;
}
bool Internal() noexcept {
    return internal;
}
bool Enabled() noexcept {
    return state.enabled.load(std::memory_order_acquire) && GetTickCount64() <= state.deadline.load();
}
std::uint32_t SelectedColor() noexcept {
    return selectedColor;
}
std::uint64_t Generation() noexcept {
    return state.annotations.Generation();
}
void Forget(std::uintptr_t command) noexcept {
    state.annotations.Take(command, {});
}
void SetReplaySite(std::uintptr_t address) noexcept {
    state.replaySite.store(address, std::memory_order_release);
}
bool Annotate(std::uintptr_t address, const std::array<UINT, 5> &args, std::uint32_t color) noexcept {
    if (!Enabled())
        return false;
    if (state.annotations.Put(address, args, color)) {
        ++state.queued;
        return true;
    }
    ++state.dropped;
    return false;
}
void Query(ID3D11DeviceContext *context, ID3D11Asynchronous *async, bool begin) noexcept {
    if (internal || !async || context != state.watched.load(std::memory_order_acquire))
        return;
    ComPtr<ID3D11Query> query;
    if (FAILED(async->QueryInterface(IID_PPV_ARGS(&query))))
        return;
    D3D11_QUERY_DESC desc{};
    query->GetDesc(&desc);
    switch (desc.Query) {
    case D3D11_QUERY_OCCLUSION:
    case D3D11_QUERY_OCCLUSION_PREDICATE:
    case D3D11_QUERY_PIPELINE_STATISTICS:
    case D3D11_QUERY_SO_STATISTICS:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE:
    case D3D11_QUERY_SO_STATISTICS_STREAM0:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0:
    case D3D11_QUERY_SO_STATISTICS_STREAM1:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM1:
    case D3D11_QUERY_SO_STATISTICS_STREAM2:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM2:
    case D3D11_QUERY_SO_STATISTICS_STREAM3:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3:
        break;
    default:
        return;
    }
    try {
        std::lock_guard lock(state.mutex);
        if (context != state.watched.load(std::memory_order_acquire))
            return;
        const auto existing = std::find(state.queries.begin(), state.queries.end(), async);
        if (begin) {
            if (existing != state.queries.end())
                return;
            const auto empty = std::find(state.queries.begin(), state.queries.end(), nullptr);
            if (empty != state.queries.end())
                *empty = async;
            else
                ++state.queryOverflow;
            ++state.queryCount;
        } else if (existing != state.queries.end()) {
            *existing = nullptr;
            --state.queryCount;
        } else if (state.queryOverflow) {
            --state.queryOverflow;
            --state.queryCount;
        }
    } catch (...) {
        state.queryCount = 1;
    } // Failure must suppress duplicates, not alter host query results.
}
void Observe(ID3D11DeviceContext *context, const Draw &draw, std::uintptr_t returnAddress,
             std::uintptr_t payload) noexcept {
    if (internal || !Enabled() || context != state.watched.load(std::memory_order_acquire))
        return;
    auto color = selectedColor;
    if (!color && draw.kind == DrawKind::IndexedInstanced && payload >= 4 && returnAddress &&
        returnAddress == state.replaySite.load(std::memory_order_acquire)) {
        color = state.annotations.Take(payload - 4, draw.args);
        if (color)
            ++state.matched;
    }
    if (!(color >> 24))
        return;
    if (state.queryCount.load(std::memory_order_acquire)) {
        ++state.dropped;
        return;
    }
    try {
        std::unique_lock lock(state.mutex, std::try_to_lock);
        if (!lock || !Enabled() || state.draws >= 4096) {
            ++state.dropped;
            return;
        }
        auto &r = state.resources;
        D3D11_VIEWPORT viewport{};
        UINT n = 1;
        context->RSGetViewports(&n, &viewport);
        if (n != 1 || viewport.TopLeftX != 0 || viewport.TopLeftY != 0 || viewport.Width != r.width ||
            viewport.Height != r.height || viewport.MinDepth != 0 || viewport.MaxDepth != 1)
            return;
        // A duplicate must never advance a game's stream-output cursor.
        // Character color draws use ordinary vertex/index buffers; any unexpected
        // stream-output pass is forwarded without capture.
        ID3D11Buffer *streamOutput[D3D11_SO_BUFFER_SLOT_COUNT]{};
        context->SOGetTargets(D3D11_SO_BUFFER_SLOT_COUNT, streamOutput);
        bool hasStreamOutput{};
        for (auto *buffer : streamOutput)
            if (buffer) {
                hasStreamOutput = true;
                buffer->Release();
            }
        if (hasStreamOutput)
            return;
        InternalScope internalScope;
        const UINT uavs = r.device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1 ? 64u : 8u;
        Bindings previous(context, uavs);
        if (!DepthMatches(previous.depth, r.device.Get(), r.width, r.height, false))
            return;
        // Predication also applies to UpdateSubresource, not only Draw. Disable
        // it before the color upload or a skipped upload leaves transparent data.
        context->SetPredication(nullptr, FALSE);
        Constants settings;
        for (unsigned i = 0; i < 4; ++i)
            settings.color[i] = float((color >> (i * 8)) & 255) / 255.f;
        context->UpdateSubresource(r.constants.Get(), 0, nullptr, &settings, 0, 0);
        auto *cb = r.constants.Get();
        context->PSSetConstantBuffers(0, 1, &cb);
        auto *target = r.target.Get();
        ID3D11UnorderedAccessView *emptyUavs[64]{};
        context->OMSetRenderTargetsAndUnorderedAccessViews(1, &target, r.depthView.Get(), 1, uavs - 1, emptyUavs,
                                                           nullptr);
        context->OMSetDepthStencilState(r.nearState[state.reversed ? 1 : 0].Get(), 0);
        context->OMSetBlendState(nullptr, nullptr, ~0u);
        context->PSSetShader(r.capture.Get(), nullptr, 0);
        Submit(context, draw);
        ++state.draws;
        ++state.captured;
    } catch (...) {
        ++state.dropped;
    }
}
HRESULT Render(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11RenderTargetView *target,
               const D3D11_TEXTURE2D_DESC &desc, ID3D11DepthStencilView *sceneDepth, bool reversed, bool enabled,
               EffectsState &status) noexcept {
    state.enabled.store(false, std::memory_order_release);
    status = {};
    status.status = EffectsStatus::Disabled;
    status.result = S_FALSE;
    try {
        std::lock_guard lock(state.mutex);
        InternalScope internalScope;
        if (!enabled) {
            state.draws = 0;
            state.annotations.Advance();
            return S_FALSE;
        }
        if (!device || !context || !target || !desc.Width || !desc.Height || desc.Width > 8192 || desc.Height > 8192 ||
            desc.SampleDesc.Count != 1 || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
            status.status = EffectsStatus::Failed;
            return status.result = E_INVALIDARG;
        }
        const bool changed = state.resources.device.Get() != device || state.resources.width != desc.Width ||
                             state.resources.height != desc.Height || state.watched.load() != context;
        if (changed) {
            const auto hr = Create(device, desc.Width, desc.Height, state.resources);
            if (FAILED(hr)) {
                status.status = EffectsStatus::Failed;
                return status.result = hr;
            }
            state.draws = 0;
            state.queries = {};
            state.queryCount = state.queryOverflow = 0;
        }
        auto &r = state.resources;
        const bool depthValid = DepthMatches(sceneDepth, device, desc.Width, desc.Height, true);
        status.meshCount = state.draws;
        status.depthAvailable = depthValid;
        status.status = depthValid ? EffectsStatus::NativeMaterialPending : EffectsStatus::DepthUnavailable;
        if (depthValid && state.draws && state.reversed == reversed && !changed) {
            Constants settings;
            settings.reversed = reversed;
            context->UpdateSubresource(r.constants.Get(), 0, nullptr, &settings, 0, 0);
            auto *cb = r.constants.Get();
            context->PSSetConstantBuffers(0, 1, &cb);
            context->OMSetRenderTargets(1, &target, sceneDepth);
            context->OMSetDepthStencilState(r.behindState[reversed ? 1 : 0].Get(), 0);
            context->OMSetBlendState(r.blend.Get(), nullptr, ~0u);
            context->RSSetState(r.raster.Get());
            D3D11_VIEWPORT viewport{0, 0, float(desc.Width), float(desc.Height), 0, 1};
            context->RSSetViewports(1, &viewport);
            context->IASetInputLayout(nullptr);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(r.screen.Get(), nullptr, 0);
            context->PSSetShader(r.composite.Get(), nullptr, 0);
            context->GSSetShader(nullptr, nullptr, 0);
            context->HSSetShader(nullptr, nullptr, 0);
            context->DSSetShader(nullptr, nullptr, 0);
            ID3D11ShaderResourceView *resources[]{r.colorResource.Get(), r.depthResource.Get()};
            context->PSSetShaderResources(0, 2, resources);
            context->Draw(3, 0);
            status.status = EffectsStatus::NativeMaterialReady;
            status.result = S_OK;
        }
        ID3D11ShaderResourceView *empty[2]{};
        context->PSSetShaderResources(0, 2, empty);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        constexpr FLOAT transparent[4]{};
        context->ClearRenderTargetView(r.target.Get(), transparent);
        context->ClearDepthStencilView(r.depthView.Get(), D3D11_CLEAR_DEPTH, reversed ? 0.f : 1.f, 0);
        state.draws = 0;
        state.reversed = reversed;
        state.annotations.Advance();
        state.watched.store(context, std::memory_order_release);
        state.deadline = GetTickCount64() + 250;
        state.enabled.store(depthValid, std::memory_order_release);
        return status.result;
    } catch (...) {
        status.status = EffectsStatus::Failed;
        return status.result = E_FAIL;
    }
}
void Discard() noexcept {
    state.enabled.store(false, std::memory_order_release);
    state.deadline = 0;
    state.annotations.Advance();
    std::lock_guard lock(state.mutex);
    state.draws = 0;
}
void Release() noexcept {
    Discard();
    std::lock_guard lock(state.mutex);
    state.resources = Resources{};
    state.queries = {};
    state.queryCount = state.queryOverflow = 0;
    state.watched = nullptr;
}
Diagnostics GetDiagnostics() noexcept {
    return {state.captured.load(), state.queued.load(), state.matched.load(), state.dropped.load()};
}
} // namespace awareness::native_mask
