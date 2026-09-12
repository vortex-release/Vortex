#include "trajectory_gpu.hpp"
#include <d3dcompiler.h>
#include <cstring>
namespace awareness::flight {
namespace {
constexpr char Shader[] = R"(
struct In { float4 p:POSITION; float4 color:COLOR; float4 shape:TEXCOORD; };
struct Out { float4 p:SV_POSITION; float4 color:COLOR; noperspective float4 shape:TEXCOORD; };
Out VS(In i) { Out o; o.p=i.p; o.color=i.color; o.shape=i.shape; return o; }
float4 Shade(Out i, bool hidden) {
    float edge=abs(i.shape.x),halfWidth=i.shape.z;
    float core=1-smoothstep(max(0,halfWidth-.55),halfWidth+.55,edge);
    float halo=exp(-max(0,edge-halfWidth)*1.45)*i.shape.w*.14;
    float coverage=max(core,halo);
    if(hidden) { clip(6-fmod(i.shape.y,11)); coverage=core*.22; }
    float alpha=i.color.a*coverage;
    clip(alpha-.002);
    return float4(i.color.rgb,alpha);
}
float4 PS(Out i):SV_TARGET { return Shade(i,false); }
float4 Hidden(Out i):SV_TARGET { return Shade(i,true); }
)";
}
bool MatchingSceneDepth(ID3D11Device *device, ID3D11RenderTargetView *target, ID3D11DepthStencilView *depth) noexcept {
    if (!device || !target || !depth)
        return false;
    Microsoft::WRL::ComPtr<ID3D11Device> owner;
    depth->GetDevice(&owner);
    if (owner.Get() != device)
        return false;
    target->GetDevice(owner.ReleaseAndGetAddressOf());
    if (owner.Get() != device)
        return false;
    D3D11_DEPTH_STENCIL_VIEW_DESC vd{};
    depth->GetDesc(&vd);
    if (vd.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D || vd.Texture2D.MipSlice)
        return false;
    Microsoft::WRL::ComPtr<ID3D11Resource> a, b;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> color, texture;
    target->GetResource(&a);
    depth->GetResource(&b);
    if (FAILED(a.As(&color)) || FAILED(b.As(&texture)))
        return false;
    D3D11_TEXTURE2D_DESC c{}, d{};
    color->GetDesc(&c);
    texture->GetDesc(&d);
    return c.Width == d.Width && c.Height == d.Height && c.SampleDesc.Count == 1 && d.SampleDesc.Count == 1 &&
           d.ArraySize == 1;
}
HRESULT DepthRenderer::Initialize(ID3D11Device *device) {
    if (device_.Get() != device) {
        *this = DepthRenderer{};
        device_ = device;
    }
    if (hidden_)
        return S_OK;
    HRESULT hr;
    Ptr<ID3DBlob> code, error;
#define GPU_CHECK(x)                                                                                                   \
    if (FAILED(hr = (x)))                                                                                              \
    return hr
    auto compile = [&](const char *entry, const char *profile) {
        return D3DCompile(Shader, sizeof(Shader) - 1, "DepthTrajectories", nullptr, nullptr, entry, profile,
                          D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                          code.ReleaseAndGetAddressOf(), error.ReleaseAndGetAddressOf());
    };
    GPU_CHECK(compile("VS", "vs_5_0"));
    GPU_CHECK(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                         vs_.ReleaseAndGetAddressOf()));
    D3D11_INPUT_ELEMENT_DESC elements[]{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    GPU_CHECK(device->CreateInputLayout(elements, 3, code->GetBufferPointer(), code->GetBufferSize(),
                                        layout_.ReleaseAndGetAddressOf()));
    GPU_CHECK(compile("PS", "ps_5_0"));
    GPU_CHECK(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                        ps_.ReleaseAndGetAddressOf()));
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    raster.ScissorEnable = TRUE;
    GPU_CHECK(device->CreateRasterizerState(&raster, raster_.ReleaseAndGetAddressOf()));
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
    GPU_CHECK(device->CreateBlendState(&blend, blend_.ReleaseAndGetAddressOf()));
    D3D11_DEPTH_STENCIL_DESC d{};
    d.DepthEnable = TRUE;
    d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    d.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    GPU_CHECK(device->CreateDepthStencilState(&d, front_.ReleaseAndGetAddressOf()));
    d.DepthFunc = D3D11_COMPARISON_GREATER;
    GPU_CHECK(device->CreateDepthStencilState(&d, back_.ReleaseAndGetAddressOf()));
    d.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    GPU_CHECK(device->CreateDepthStencilState(&d, frontReverse_.ReleaseAndGetAddressOf()));
    d.DepthFunc = D3D11_COMPARISON_LESS;
    GPU_CHECK(device->CreateDepthStencilState(&d, backReverse_.ReleaseAndGetAddressOf()));
    GPU_CHECK(compile("Hidden", "ps_5_0"));
    GPU_CHECK(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                        hidden_.ReleaseAndGetAddressOf()));
    return S_OK;
}
bool DepthRenderer::Stroke(Vector3 a, Vector3 b, const Matrix4x4 &matrix, Viewport view, Color first, Color last,
                           float alpha, float width, float glow, float phase) {
    if (!Valid(view) || !std::isfinite(alpha) || !std::isfinite(width) || !std::isfinite(glow) || alpha <= 0 ||
        width <= 0 || vertices_.size() + 6 > MaxVertices)
        return false;
    auto p = Transform(a, matrix), q = Transform(b, matrix);
    if (!ClipDepth(p, q))
        return false;
    // Homogeneous side clipping preserves depth/perspective; no giant screen vertices at the near plane.
    float lo = 0, hi = 1;
    const float pa[]{p.x + p.w, p.w - p.x, p.y + p.w, p.w - p.y};
    const float qa[]{q.x + q.w, q.w - q.x, q.y + q.w, q.w - q.y};
    for (int i = 0; i < 4; ++i) {
        if (pa[i] < 0 && qa[i] < 0)
            return false;
        if ((pa[i] < 0) != (qa[i] < 0)) {
            float t = pa[i] / (pa[i] - qa[i]);
            if (pa[i] < 0)
                lo = std::max(lo, t);
            else
                hi = std::min(hi, t);
        }
    }
    if (lo > hi)
        return false;
    const auto original = p;
    p = Lerp(original, q, lo);
    q = Lerp(original, q, hi);
    const auto start = Project(p, view), end = Project(q, view);
    float dx = end.x - start.x, dy = end.y - start.y, length = std::hypot(dx, dy);
    if (!std::isfinite(length))
        return false;
    // An eye-to-impact shot may project to a dot. Give the actual impact a tiny cap at its own depth.
    if (length < .1f) {
        p = q;
        dx = 1;
        dy = 0;
        length = 1.5f;
        p.x -= length / view.width * p.w;
        q.x += length / view.width * q.w;
    }
    float nx = -dy / std::hypot(dx, dy), ny = dx / std::hypot(dx, dy);
    const float half = std::clamp(width, .7f, 5.f) * .5f, radius = half + 1.f + std::clamp(glow, 0.f, 3.f) * .65f;
    first.a *= std::clamp(alpha, 0.f, 1.f);
    last.a *= std::clamp(alpha, 0.f, 1.f);
    auto vertex = [&](ClipPoint v, Color c, float side, float along) {
        v.x += nx * side * 2 / view.width * v.w;
        v.y -= ny * side * 2 / view.height * v.w;
        return DepthStrokeVertex{v, c, side, along, half, std::clamp(glow, 0.f, 3.f)};
    };
    const DepthStrokeVertex quad[]{vertex(p, first, -radius, phase), vertex(p, first, radius, phase),
                                   vertex(q, last, -radius, phase + length), vertex(q, last, radius, phase + length)};
    for (int index : {0, 1, 2, 2, 1, 3})
        vertices_.push_back(quad[index]);
    return true;
}
#undef GPU_CHECK
#define GPU_CHECK(x)                                                                                                   \
    if (FAILED(hr = (x)))                                                                                              \
    return status.result = hr
HRESULT DepthRenderer::Render(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11RenderTargetView *target,
                              ID3D11DepthStencilView *depth, bool reversed, Viewport view, const Trails &trails,
                              const Prediction &prediction, const Tracers &shots, bool showTrails, bool showPrediction,
                              bool showShots, Shots filter, std::uint32_t local, int team, const Matrix4x4 &matrix,
                              double now, float opacity, const PathStyle &style, RenderStatus &status) {
    status = {};
    if (!device || !context || !target || !Valid(view) || !std::isfinite(now) || !std::isfinite(opacity) ||
        !ValidPathStyle(style) || prediction.count > prediction.points.size() ||
        prediction.bounceCount > prediction.bounces.size())
        return status.result = E_INVALIDARG;
    status.depthAvailable = MatchingSceneDepth(device, target, depth);
    if (!status.depthAvailable || !(showTrails || showPrediction || showShots))
        return S_FALSE;
    HRESULT hr;
    GPU_CHECK(Initialize(device));
    Clear();
    if (vertices_.capacity() < 12288)
        vertices_.reserve(12288);
    auto path = [&](std::size_t count, auto point, Color color, float alpha, float width, float glow) {
        float phase = 0;
        for (std::size_t i = 1; i < count; ++i) {
            const auto a = point(i - 1), b = point(i);
            Stroke(a, b, matrix, view, color, color, alpha, width, glow, phase);
            Vector2 p, q;
            if (WorldToScreen(a, matrix, view, p) && WorldToScreen(b, matrix, view, q))
                phase = std::fmod(phase + std::hypot(q.x - p.x, q.y - p.y), 11.f);
        }
    };
    if (showTrails)
        for (const auto &p : trails.Paths()) {
            float alpha = (p.active ? 1 : Fade(now, p.lastSeen, 5)) * opacity;
            if (alpha > 0)
                path(
                    p.points.count + (p.active ? 1 : 0),
                    [&](std::size_t i) { return i == p.points.count ? p.head : p.points[i].position; },
                    style.UtilityColor(p.type, false), alpha, style.trailWidth,
                    style.trailGlow ? style.trailStrength : 0);
        }
    if (showPrediction && prediction.valid) {
        auto color = style.UtilityColor(prediction.type, true);
        path(
            prediction.count, [&](std::size_t i) { return prediction.points[i]; }, color, opacity, style.previewWidth,
            style.previewGlow ? style.previewStrength : 0);
        auto marker = [&](Vector3 position, float radius) {
            // Ground-plane circles carry real depth as they cross walls and terrain.
            for (int i = 0; i < 24; ++i) {
                const float a = i * 6.283185307f / 24, b = (i + 1) * 6.283185307f / 24;
                Stroke(position + Vector3{std::cos(a) * radius, std::sin(a) * radius, 1},
                       position + Vector3{std::cos(b) * radius, std::sin(b) * radius, 1}, matrix, view, color, color,
                       opacity, 1.2f, 0, static_cast<float>(i));
            }
        };
        for (unsigned i = 0; i < prediction.bounceCount; ++i)
            marker(prediction.bounces[i], 2.5f);
        if (prediction.finished)
            marker(prediction.landing, 7);
    }
    const auto utilityCount = static_cast<UINT>(vertices_.size());
    if (showShots)
        for (std::size_t i = 0; i < shots.Lines().count; ++i) {
            const auto &shot = shots.Lines()[i];
            if (!Matches(shot, filter, local, team))
                continue;
            const float alpha = Fade(now, shot.time, style.shotLifetime) * opacity;
            Stroke(shot.start, shot.end, matrix, view, style.shotStart, style.shotEnd, alpha, style.shotWidth,
                   style.shotGlow ? style.shotStrength : 0);
        }
    status.vertices = static_cast<unsigned>(vertices_.size());
    if (!status.vertices)
        return S_FALSE;
    if (!buffer_ || status.vertices > capacity_) {
        const UINT size = std::min(MaxVertices, std::max(status.vertices, std::max(12288u, capacity_ + capacity_ / 2)));
        D3D11_BUFFER_DESC b{};
        b.ByteWidth = size * sizeof(DepthStrokeVertex);
        b.Usage = D3D11_USAGE_DYNAMIC;
        b.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        b.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        Ptr<ID3D11Buffer> replacement;
        GPU_CHECK(device->CreateBuffer(&b, nullptr, &replacement));
        buffer_ = std::move(replacement);
        capacity_ = size;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    GPU_CHECK(context->Map(buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
    std::memcpy(mapped.pData, vertices_.data(), vertices_.size() * sizeof(DepthStrokeVertex));
    context->Unmap(buffer_.Get(), 0);
    context->OMSetRenderTargets(1, &target, depth);
    context->OMSetBlendState(blend_.Get(), nullptr, ~0u);
    context->RSSetState(raster_.Get());
    D3D11_VIEWPORT viewport{view.x, view.y, view.width, view.height, 0, 1};
    context->RSSetViewports(1, &viewport);
    D3D11_RECT rect{static_cast<LONG>(view.x), static_cast<LONG>(view.y), static_cast<LONG>(view.x + view.width),
                    static_cast<LONG>(view.y + view.height)};
    context->RSSetScissorRects(1, &rect);
    context->IASetInputLayout(layout_.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(DepthStrokeVertex), offset = 0;
    auto *vb = buffer_.Get();
    context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    context->VSSetShader(vs_.Get(), nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    if (utilityCount) {
        context->OMSetDepthStencilState(reversed ? backReverse_.Get() : back_.Get(), 0);
        context->PSSetShader(hidden_.Get(), nullptr, 0);
        context->Draw(utilityCount, 0);
        ++status.drawCalls;
    }
    context->OMSetDepthStencilState(reversed ? frontReverse_.Get() : front_.Get(), 0);
    context->PSSetShader(ps_.Get(), nullptr, 0);
    context->Draw(status.vertices, 0);
    ++status.drawCalls;
    context->OMSetRenderTargets(0, nullptr, nullptr);
    return status.result = S_OK;
#undef GPU_CHECK
}
} // namespace awareness::flight
