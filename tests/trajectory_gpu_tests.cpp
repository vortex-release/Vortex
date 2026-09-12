#include "trajectory_gpu.hpp"
#include "scene_depth_capture.hpp"
#include "area_renderer.hpp"
#include "depth_resolve.hpp"
#include <MinHook.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <stdexcept>
#include <memory>
using namespace awareness;
using namespace awareness::flight;
using Microsoft::WRL::ComPtr;
int main() {
    int failures{};
    auto check = [&](bool ok, const char *message) {
        if (!ok) {
            ++failures;
            std::printf("FAIL: %s\n", message);
        }
    };
    auto require = [](HRESULT hr) {
        if (FAILED(hr))
            throw std::runtime_error("D3D fixture failed");
    };
    try {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        require(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device,
                                  nullptr, &context));
        D3D11_TEXTURE2D_DESC d{};
        d.Width = d.Height = 96;
        d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> color, depth;
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<ID3D11DepthStencilView> dsv;
        require(device->CreateTexture2D(&d, nullptr, &color));
        require(device->CreateRenderTargetView(color.Get(), nullptr, &target));
        d.Format = DXGI_FORMAT_D32_FLOAT;
        d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        require(device->CreateTexture2D(&d, nullptr, &depth));
        require(device->CreateDepthStencilView(depth.Get(), nullptr, &dsv));
        auto read = [&](ID3D11Texture2D *texture) {
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            desc.BindFlags = 0;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> stage;
            require(device->CreateTexture2D(&desc, nullptr, &stage));
            context->CopyResource(stage.Get(), texture);
            D3D11_MAPPED_SUBRESOURCE map{};
            require(context->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &map));
            std::vector<unsigned char> result(96 * 96 * 4);
            for (unsigned y = 0; y < 96; ++y)
                std::memcpy(result.data() + y * 96 * 4, static_cast<unsigned char *>(map.pData) + y * map.RowPitch,
                            96 * 4);
            context->Unmap(stage.Get(), 0);
            return result;
        };
        DepthRenderer renderer;
        RenderStatus status;
        Trails trails;
        Prediction prediction;
        Tracers shots;
        PathStyle style;
        style.shotStart = style.shotEnd = {1, 1, 1, 1};
        style.shotGlow = 0;
        style.previewHE = {1, 1, 1, 1};
        style.previewGlow = 0;
        style.shotWidth = style.previewWidth = 2;
        style.shotLifetime = 2;
        const auto matrix = Matrix4x4::Identity();
        Viewport view{0, 0, 96, 96};
        shots.Add({{-.8f, 0, .2f}, {.8f, 0, .8f}, 1, 2, 10});
        auto render = [&](ID3D11DepthStencilView *input, bool reverse = false) {
            float clear[4]{};
            context->ClearRenderTargetView(target.Get(), clear);
            check(SUCCEEDED(renderer.Render(device.Get(), context.Get(), target.Get(), input, reverse, view, trails,
                                            prediction, shots, false, prediction.valid, true, Shots::All, 1, 2, matrix,
                                            10, 1, style, status)),
                  "GPU trajectory render succeeds");
            return read(color.Get());
        };
        context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, .5f, 0);
        auto originalDepth = read(depth.Get());
        auto image = render(dsv.Get());
        auto pixel = [](const auto &bytes, int x, int y) { return bytes[(y * 96 + x) * 4]; };
        check(pixel(image, 24, 48) > 180 && pixel(image, 72, 48) == 0,
              "bullet segment is visible in front and absent behind wall");
        check(status.drawCalls == 1, "all shots use one GPU draw");
        check(read(depth.Get()) == originalDepth, "trajectory pass never changes scene depth");
        image = render(dsv.Get(), true);
        check(pixel(image, 24, 48) == 0 && pixel(image, 72, 48) > 180, "reversed depth clips the opposite half");
        image = render(nullptr);
        check(!status.depthAvailable && std::accumulate(image.begin(), image.end(), 0u) == 0,
              "missing scene depth fails closed with no tracer pixels");
        shots.Clear();
        prediction.valid = true;
        prediction.type = Utility::HE;
        prediction.count = 2;
        prediction.points[0] = {-.8f, 0, .2f};
        prediction.points[1] = {.8f, 0, .8f};
        image = render(dsv.Get());
        unsigned hidden{}, gaps{};
        int maximum{};
        for (int x = 52; x < 84; ++x) {
            const auto v = pixel(image, x, 48);
            hidden += v > 0;
            gaps += v == 0;
            maximum = std::max(maximum, int(v));
        }
        check(pixel(image, 24, 48) > 180 && hidden > 5 && gaps > 5 && maximum < 80,
              "hidden utility path is dimmer and dashed, visible path solid");
        check(status.drawCalls == 2, "visible and hidden utility sections use two batched draws");
        prediction.count = 514;
        check(renderer.Render(device.Get(), context.Get(), target.Get(), dsv.Get(), false, view, trails, prediction,
                              shots, false, true, false, Shots::All, 1, 2, matrix, 10, 1, style,
                              status) == E_INVALIDARG,
              "malformed prediction count rejected");
        prediction.count = 2;
        renderer.Clear();
        auto nearMatrix = matrix;
        nearMatrix.m[3][3] = 0;
        nearMatrix.m[3][2] = 1;
        check(renderer.Stroke({-.1f, 0, -1}, {.1f, 0, 1}, nearMatrix, view, {1, 1, 1, 1}, {1, 1, 1, 1}, 1, 1),
              "near-plane crossing stroke remains clipped and renderable");
        for (const auto &v : renderer.Vertices())
            check(Finite(v.position) && std::abs(v.position.x / v.position.w) < 1.2f,
                  "near-plane geometry remains finite and bounded");
        combat::AreaRenderer areas;
        auto world = std::make_unique<combat::WorldSnapshot>();
        world->areaCount = 1;
        auto &fire = world->areas[0];
        fire.handle = 17;
        fire.type = combat::AreaType::Fire;
        fire.remaining = 5;
        fire.cellCount = 3;
        fire.cellRadius = .2f;
        fire.cells[0] = fire.cells[1] = {-.5f, 0, .2f};
        fire.cells[2] = {.5f, 0, .2f};
        combat::Options options;
        options.areas = options.fireArea = options.areaFill = 1;
        options.areaOutline = 0;
        options.fireColor = {1, 0, 0, .25f};
        EffectsState areaStatus;
        D3D11_TEXTURE2D_DESC colorDesc{};
        color->GetDesc(&colorDesc);
        const float clear[4]{};
        context->ClearRenderTargetView(target.Get(), clear);
        context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, .5f, 0);
        require(areas.Render(device.Get(), context.Get(), target.Get(), colorDesc, dsv.Get(), false, matrix, view,
                             *world, options, 1, areaStatus));
        context->OMSetRenderTargets(0, nullptr, nullptr);
        auto footprint = read(color.Get());
        check(pixel(footprint, 24, 48) > 55 && pixel(footprint, 24, 48) < 70,
              "overlapping fire cells composite once without stacking opacity");
        check(pixel(footprint, 48, 48) == 0 && pixel(footprint, 72, 48) > 55,
              "separate fire clusters preserve the gap instead of drawing a convex bridge");
        const auto builds = areas.GeometryRebuilds();
        areas.Render(device.Get(), context.Get(), target.Get(), colorDesc, dsv.Get(), false, matrix, view, *world,
                     options, 1, areaStatus);
        check(areas.GeometryRebuilds() == builds, "unchanged fire cells reuse cached geometry");
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->ClearRenderTargetView(target.Get(), clear);
        context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, .1f, 0);
        areas.Render(device.Get(), context.Get(), target.Get(), colorDesc, dsv.Get(), false, matrix, view, *world,
                     options, 1, areaStatus);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        footprint = read(color.Get());
        check(std::accumulate(footprint.begin(), footprint.end(), 0u) == 0,
              "fire footprint is fully hidden behind scene geometry");
        areas.Render(device.Get(), context.Get(), target.Get(), colorDesc, nullptr, false, matrix, view, *world,
                     options, 1, areaStatus);
        check(areaStatus.status == EffectsStatus::DepthUnavailable, "fire footprint reports unavailable depth");
        check(MH_Initialize() == MH_OK, "MinHook initialized for capture fixture");
        require(scene_depth::Start(device.Get(), context.Get(), 96, 96));
        scene_depth::EndOverlay();
        const auto references = [](IUnknown *object) {
            const auto count = object->AddRef();
            object->Release();
            return count;
        };
        const auto targetReferences = references(target.Get());
        auto *rtv = target.Get();
        context->OMSetRenderTargets(1, &rtv, dsv.Get());
        D3D11_VIEWPORT viewport{0, 0, 96, 96, 0, 1};
        context->RSSetViewports(1, &viewport);
        context->OMSetDepthStencilState(nullptr, 0);
        context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        check(references(target.Get()) == targetReferences,
              "depth capture retains no color target references after unbind");
        auto empty = scene_depth::BeginOverlay();
        check(!empty.view, "a clear and bind without scene draws is not accepted as depth");
        require(scene_depth::Start(device.Get(), context.Get(), 96, 96));
        scene_depth::EndOverlay();
        constexpr char shader[] =
            R"(float4 VS(uint id:SV_VertexID):SV_POSITION { float2 p[3]={float2(-1,-1),float2(0,1),float2(1,-1)};return float4(p[id],.4,1); })";
        ComPtr<ID3DBlob> code, error;
        require(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, &code, &error));
        ComPtr<ID3D11VertexShader> vs;
        require(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vs));
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        ComPtr<ID3D11RasterizerState> rs;
        require(device->CreateRasterizerState(&raster, &rs));
        context->RSSetState(rs.Get());
        context->OMSetRenderTargets(1, &rtv, dsv.Get());
        context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(), nullptr, 0);
        context->PSSetShader(nullptr, nullptr, 0);
        context->Draw(3, 0);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        // Simulates the engine clearing the same depth for the first-person weapon.
        context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        // Once the scene is frozen, later first-person/effect clears must neither
        // replace it nor incur more copy/descriptor work.
        for (unsigned i = 0; i < 16; ++i) {
            context->OMSetRenderTargets(1, &rtv, dsv.Get());
            context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 0, 0);
            context->RSSetViewports(1, &viewport);
            context->OMSetDepthStencilState(nullptr, 0);
        }
        auto snapshot = scene_depth::BeginOverlay();
        if (!snapshot.view)
            std::printf("Capture state: binds=%u clears=%u draws=%u copies=%u\n", snapshot.eligibleBindings,
                        snapshot.depthClears, snapshot.qualifiedDraws, snapshot.copies);
        check(snapshot.view && !snapshot.reversed, "full scene capture survives later weapon depth clear");
        check(snapshot.copies == 1 && snapshot.depthClears == 2,
              "frozen capture bypasses later render-state and clear work");
        if (snapshot.view) {
            ComPtr<ID3D11Resource> resource;
            ComPtr<ID3D11Texture2D> texture;
            snapshot.view->GetResource(&resource);
            resource.As(&texture);
            auto bytes = read(texture.Get());
            float z{};
            std::memcpy(&z, bytes.data() + (48 * 96 + 48) * 4, 4);
            check(std::abs(z - .4f) < .001f, "capture contains rendered scene depth rather than clear depth");
        }
        scene_depth::EndOverlay();
        snapshot = scene_depth::BeginOverlay();
        check(!snapshot.view, "capture cannot leak into a stale next frame");
        scene_depth::EndOverlay();
        auto multiDesc = colorDesc;
        multiDesc.SampleDesc = {4, 0};
        ComPtr<ID3D11Texture2D> multiColor, multiDepth;
        ComPtr<ID3D11RenderTargetView> multiTarget;
        ComPtr<ID3D11DepthStencilView> multiView;
        require(device->CreateTexture2D(&multiDesc, nullptr, &multiColor));
        require(device->CreateRenderTargetView(multiColor.Get(), nullptr, &multiTarget));
        multiDesc.Format = DXGI_FORMAT_D32_FLOAT;
        multiDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        require(device->CreateTexture2D(&multiDesc, nullptr, &multiDepth));
        require(device->CreateDepthStencilView(multiDepth.Get(), nullptr, &multiView));
        constexpr char sampleShader[] = "float PS(uint sample:SV_SampleIndex):SV_Depth { return .15 + sample * .2; }";
        code.Reset();
        error.Reset();
        require(D3DCompile(sampleShader, sizeof(sampleShader), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &code,
                           &error));
        ComPtr<ID3D11PixelShader> samplePs;
        require(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &samplePs));
        context->PSSetShader(samplePs.Get(), nullptr, 0);
        auto *msTarget = multiTarget.Get();
        context->OMSetRenderTargets(1, &msTarget, multiView.Get());
        context->ClearDepthStencilView(multiView.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        // Rebinding the same target repeatedly must not spend a finite copy budget.
        for (unsigned pass = 0; pass < 8; ++pass) {
            context->OMSetRenderTargets(1, &msTarget, multiView.Get());
            context->Draw(3, 0);
        }
        context->OMSetRenderTargets(0, nullptr, nullptr);
        auto multiSnapshot = scene_depth::BeginOverlay();
        if (!multiSnapshot.view)
            std::printf("MSAA capture state: binds=%u clears=%u draws=%u copies=%u\n", multiSnapshot.eligibleBindings,
                        multiSnapshot.depthClears, multiSnapshot.qualifiedDraws, multiSnapshot.copies);
        check(multiSnapshot.view && multiSnapshot.copies == 1, "4x MSAA scene is captured once after all color passes");
        scene_depth::Resolver resolver;
        ComPtr<ID3D11DepthStencilView> resolved;
        require(resolver.Resolve(device.Get(), context.Get(), colorDesc, multiView.Get(), false, resolved));
        const auto readDepth = [&](ID3D11DepthStencilView *view) {
            ComPtr<ID3D11Resource> r;
            ComPtr<ID3D11Texture2D> t;
            view->GetResource(&r);
            r.As(&t);
            auto bytes = read(t.Get());
            float z{};
            std::memcpy(&z, bytes.data() + (48 * 96 + 48) * 4, 4);
            return z;
        };
        check(resolved && std::abs(readDepth(resolved.Get()) - .15f) < .001f,
              "4x MSAA conventional depth resolves the nearest sample");
        require(resolver.Resolve(device.Get(), context.Get(), colorDesc, multiView.Get(), true, resolved));
        check(resolved && std::abs(readDepth(resolved.Get()) - .75f) < .001f,
              "4x MSAA reversed depth resolves the nearest sample");
        require(scene_depth::Stop());
        check(MH_Uninitialize() == MH_OK, "capture hooks are cleanly removed");
        context->ClearState();
        context->Flush();
    } catch (const std::exception &e) {
        ++failures;
        std::printf("FAIL: %s\n", e.what());
        scene_depth::Stop();
        MH_Uninitialize();
    }
    std::printf("Depth trajectories and capture: %d failures\n", failures);
    return failures ? 1 : 0;
}
