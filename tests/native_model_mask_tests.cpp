#include "native_model_mask.hpp"
#include "scene_depth_capture.hpp"
#include <MinHook.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <numeric>
using namespace awareness;
using Microsoft::WRL::ComPtr;
extern "C" void VortexTestReplay(ID3D11DeviceContext *, UINT, UINT, UINT, INT, UINT, std::uintptr_t);
extern "C" char VortexTestReplayReturn;
namespace {
constexpr char Shader[] = R"(
cbuffer Shape:register(b0){float4 shape;};
float4 VS(uint id:SV_VertexID,uint instance:SV_InstanceID):SV_POSITION{
    float2 p[4]={float2(-1,-1),float2(-1,1),float2(1,1),float2(1,-1)};
    return float4(p[id%4]*shape.w+float2(shape.x,shape.y),shape.z,1);
}
float4 PS():SV_TARGET{return float4(0,1,0,1);}
)";
struct Shape {
    float x{}, y{}, z{}, radius{.4f};
};
} // namespace
int main() {
    unsigned checks{}, failures{};
    const auto check = [&](bool ok, const char *why) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("FAIL: %s\n", why);
        }
    };
    const auto require = [](HRESULT hr) {
        if (FAILED(hr)) {
            std::printf("HRESULT %08lx\n", hr);
            throw std::runtime_error("D3D fixture setup");
        }
    };
    try {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        require(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device,
                                  nullptr, &context));
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = 96;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> color, depth;
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<ID3D11DepthStencilView> dsv;
        require(device->CreateTexture2D(&desc, nullptr, &color));
        require(device->CreateRenderTargetView(color.Get(), nullptr, &target));
        auto depthDesc = desc;
        depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        require(device->CreateTexture2D(&depthDesc, nullptr, &depth));
        require(device->CreateDepthStencilView(depth.Get(), nullptr, &dsv));
        ComPtr<ID3DBlob> code, errors;
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11PixelShader> ps;
        require(
            D3DCompile(Shader, sizeof(Shader) - 1, nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, &code, &errors));
        require(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vs));
        code.Reset();
        require(
            D3DCompile(Shader, sizeof(Shader) - 1, nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &code, &errors));
        require(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &ps));
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(Shape);
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> constants, index;
        require(device->CreateBuffer(&bd, nullptr, &constants));
        const UINT indices[]{99, 99, 3, 4, 5, 3, 5, 6};
        bd.ByteWidth = sizeof(indices);
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{indices, 0, 0};
        require(device->CreateBuffer(&bd, &data, &index));
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        ComPtr<ID3D11RasterizerState> rs;
        require(device->CreateRasterizerState(&raster, &rs));
        ComPtr<ID3D11DepthStencilState> states[2];
        for (unsigned i = 0; i < 2; ++i) {
            D3D11_DEPTH_STENCIL_DESC d{};
            d.DepthEnable = TRUE;
            d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            d.DepthFunc = i ? D3D11_COMPARISON_GREATER_EQUAL : D3D11_COMPARISON_LESS_EQUAL;
            require(device->CreateDepthStencilState(&d, &states[i]));
        }
        auto read = [&](ID3D11Texture2D *source) {
            D3D11_TEXTURE2D_DESC d{};
            source->GetDesc(&d);
            d.Usage = D3D11_USAGE_STAGING;
            d.BindFlags = 0;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> stage;
            require(device->CreateTexture2D(&d, nullptr, &stage));
            context->CopyResource(stage.Get(), source);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            require(context->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &mapped));
            std::vector<unsigned char> out(d.Width * d.Height * 4);
            for (UINT y = 0; y < d.Height; ++y)
                std::memcpy(out.data() + y * d.Width * 4,
                            static_cast<unsigned char *>(mapped.pData) + y * mapped.RowPitch, d.Width * 4);
            context->Unmap(stage.Get(), 0);
            return out;
        };
        const auto pixel = [](const auto &bytes, unsigned x, unsigned y, unsigned channel = 0) {
            return bytes[(y * 96 + x) * 4 + channel];
        };
        auto bind = [&](bool reverse) {
            auto *rt = target.Get();
            context->OMSetRenderTargets(1, &rt, dsv.Get());
            context->OMSetDepthStencilState(states[reverse ? 1 : 0].Get(), 19);
            context->OMSetBlendState(nullptr, nullptr, ~0u);
            context->RSSetState(rs.Get());
            D3D11_VIEWPORT viewport{0, 0, 96, 96, 0, 1};
            context->RSSetViewports(1, &viewport);
            context->IASetInputLayout(nullptr);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->IASetIndexBuffer(index.Get(), DXGI_FORMAT_R32_UINT, 0);
            context->VSSetShader(vs.Get(), nullptr, 0);
            context->PSSetShader(ps.Get(), nullptr, 0);
            context->GSSetShader(nullptr, nullptr, 0);
            auto *cb = constants.Get();
            context->VSSetConstantBuffers(0, 1, &cb);
            context->PSSetConstantBuffers(0, 1, &cb);
        };
        auto draw = [&](Shape shape) {
            context->UpdateSubresource(constants.Get(), 0, nullptr, &shape, 0, 0);
            context->DrawIndexedInstanced(6, 2, 2, -3, 7);
        };
        constexpr FLOAT clear[4]{};
        EffectsState status;
        // Warm D3D's final immediate-context table before installing global detours.
        bind(false);
        context->ClearRenderTargetView(target.Get(), clear);
        context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        draw({0, 0, .2f, .4f});
        const auto baseline = read(color.Get());
        check(pixel(baseline, 48, 48, 1) > 250, "baseline indexed-instance geometry and stack arguments are valid");
        check(MH_Initialize() == MH_OK, "MinHook initialized");
        require(scene_depth::Start(device.Get(), context.Get(), 96, 96));
        for (unsigned reversed = 0; reversed < 2; ++reversed) {
            require(native_mask::Render(device.Get(), context.Get(), target.Get(), desc, dsv.Get(), reversed, true,
                                        status));
            // A reverse-depth change starts a fresh mask; no prior-frame pixels survive.
            context->ClearRenderTargetView(target.Get(), clear);
            context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, .5f, 0);
            bind(reversed);
            require(scene_depth::Start(device.Get(), context.Get(), 96, 96));
            require(native_mask::Render(device.Get(), context.Get(), target.Get(), desc, dsv.Get(), reversed, true,
                                        status));
            bind(reversed);
            const auto drawsBefore = native_mask::GetDiagnostics().captured;
            {
                native_mask::Scope selected(0x800000ff);
                draw({-.4f, 0, reversed ? .8f : .2f, .25f});
                draw({-.4f, 0, reversed ? .2f : .8f, .35f});
                draw({.5f, 0, reversed ? .2f : .8f, .25f});
            }
            check(native_mask::GetDiagnostics().captured == drawsBefore + 3,
                  "all selected indexed-instance draws reach the private mask through MASM");
            ComPtr<ID3D11RenderTargetView> bound;
            ComPtr<ID3D11DepthStencilView> boundDepth;
            context->OMGetRenderTargets(1, &bound, &boundDepth);
            ComPtr<ID3D11PixelShader> boundPixel;
            context->PSGetShader(&boundPixel, nullptr, nullptr);
            ComPtr<ID3D11Buffer> boundConstant;
            context->PSGetConstantBuffers(0, 1, &boundConstant);
            UINT stencil{};
            ComPtr<ID3D11DepthStencilState> boundState;
            context->OMGetDepthStencilState(&boundState, &stencil);
            check(bound.Get() == target.Get() && boundDepth.Get() == dsv.Get() && boundPixel.Get() == ps.Get() &&
                      boundConstant.Get() == constants.Get() && boundState.Get() == states[reversed].Get() &&
                      stencil == 19,
                  "native capture restores host output/depth/pixel/constant bindings");
            const auto before = read(color.Get());
            const auto depthBefore = read(depth.Get());
            check(pixel(before, 29, 48, 1) > 250 && pixel(before, 72, 48) == 0,
                  "normal native draw still executes once with correct args and wall depth");
            require(native_mask::Render(device.Get(), context.Get(), target.Get(), desc, dsv.Get(), reversed, true,
                                        status));
            const auto after = read(color.Get());
            check(status.status == EffectsStatus::NativeMaterialReady && status.meshCount == 3,
                  "hidden composite reports actual captured geometry");
            check(pixel(after, 29, 48) == 0 && pixel(after, 29, 48, 1) > 250,
                  "front surface excludes rear self-overlap from hidden tint");
            check(pixel(after, 72, 48) > 115 && pixel(after, 72, 48) < 140 && pixel(after, 72, 48, 1) == 0,
                  "hidden surface blends once at configured opacity behind wall");
            check(read(depth.Get()) == depthBefore, "hidden compositor never changes scene depth");
        }
        // Validate the deferred software bridge with exact caller site, R15 payload,
        // negative base vertex, first index, instance count and first instance.
        native_mask::SetReplaySite(reinterpret_cast<std::uintptr_t>(&VortexTestReplayReturn));
        std::array<UINT, 6> command{0x0018801f, 6, 2, 2, static_cast<UINT>(-3), 7};
        const std::array<UINT, 5> args{6, 2, 2, static_cast<UINT>(-3), 7};
        require(native_mask::Render(device.Get(), context.Get(), target.Get(), desc, dsv.Get(), false, true, status));
        context->ClearRenderTargetView(target.Get(), clear);
        context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, .5f, 0);
        bind(false);
        Shape shape{0, 0, .8f, .4f};
        context->UpdateSubresource(constants.Get(), 0, nullptr, &shape, 0, 0);
        const auto queuedBefore = native_mask::GetDiagnostics();
        check(native_mask::Annotate(reinterpret_cast<std::uintptr_t>(command.data()), args, 0x800000ff),
              "queued command annotation accepted");
        VortexTestReplay(context.Get(), 6, 2, 2, -3, 7, reinterpret_cast<std::uintptr_t>(command.data()) + 4);
        check(native_mask::GetDiagnostics().matched == queuedBefore.matched + 1,
              "MASM retains caller R15 and replay return site");
        require(native_mask::Render(device.Get(), context.Get(), target.Get(), desc, dsv.Get(), false, true, status));
        check(pixel(read(color.Get()), 48, 48) > 115,
              "queued draw produces hidden geometry with exact preserved stack args");
        // The user's native renderer uses 4x MSAA. Capture reproduces geometry
        // against a private single-sample depth surface; host attachments stay MSAA.
        auto msaaDesc = desc;
        msaaDesc.SampleDesc.Count = 4;
        ComPtr<ID3D11Texture2D> msaaColor, msaaDepth;
        ComPtr<ID3D11RenderTargetView> msaaTarget;
        ComPtr<ID3D11DepthStencilView> msaaDsv;
        require(device->CreateTexture2D(&msaaDesc, nullptr, &msaaColor));
        require(device->CreateRenderTargetView(msaaColor.Get(), nullptr, &msaaTarget));
        msaaDesc.Format = DXGI_FORMAT_D32_FLOAT;
        msaaDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        require(device->CreateTexture2D(&msaaDesc, nullptr, &msaaDepth));
        require(device->CreateDepthStencilView(msaaDepth.Get(), nullptr, &msaaDsv));
        context->ClearRenderTargetView(target.Get(), clear);
        context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, .5f, 0);
        require(native_mask::Render(device.Get(), context.Get(), target.Get(), desc, dsv.Get(), false, true, status));
        bind(false);
        auto *msaaRt = msaaTarget.Get();
        context->OMSetRenderTargets(1, &msaaRt, msaaDsv.Get());
        context->ClearDepthStencilView(msaaDsv.Get(), D3D11_CLEAR_DEPTH, .5f, 0);
        const auto msaaBefore = native_mask::GetDiagnostics().captured;
        {
            native_mask::Scope selected(0x800000ff);
            draw({0, 0, .8f, .4f});
        }
        check(native_mask::GetDiagnostics().captured == msaaBefore + 1,
              "4x MSAA native attachments can be captured into the private mask");
        require(native_mask::Render(device.Get(), context.Get(), target.Get(), desc, dsv.Get(), false, true, status));
        check(pixel(read(color.Get()), 48, 48) > 115, "MSAA native geometry composites against resolved scene depth");
        // Predication can suppress a game draw while the private hidden mask
        // still needs the submitted geometry. Exercise both predicate polarities.
        D3D11_QUERY_DESC predicateDesc{D3D11_QUERY_OCCLUSION_PREDICATE, 0};
        ComPtr<ID3D11Predicate> predicate;
        require(device->CreatePredicate(&predicateDesc, &predicate));
        context->Begin(predicate.Get());
        context->End(predicate.Get());
        for (BOOL polarity : {FALSE, TRUE}) {
            context->SetPredication(nullptr, FALSE);
            context->ClearRenderTargetView(target.Get(), clear);
            require(
                native_mask::Render(device.Get(), context.Get(), target.Get(), desc, dsv.Get(), false, true, status));
            bind(false);
            context->SetPredication(predicate.Get(), polarity);
            const auto predicateBefore = native_mask::GetDiagnostics().captured;
            {
                native_mask::Scope selected(0x800000ff);
                draw({0, 0, .8f, .4f});
            }
            check(native_mask::GetDiagnostics().captured == predicateBefore + 1,
                  "both predicate polarities reach native capture before composition");
            ComPtr<ID3D11Predicate> restored;
            BOOL value{};
            context->GetPredication(&restored, &value);
            check(restored.Get() == predicate.Get() && value == polarity,
                  "native capture restores the original occlusion predicate");
            context->SetPredication(nullptr, FALSE);
            require(
                native_mask::Render(device.Get(), context.Get(), target.Get(), desc, dsv.Get(), false, true, status));
            check(pixel(read(color.Get()), 48, 48) > 115,
                  "hidden capture is independent of either host predicate polarity");
        }
        bind(false);
        D3D11_QUERY_DESC countDesc{D3D11_QUERY_OCCLUSION, 0};
        ComPtr<ID3D11Query> occlusion;
        require(device->CreateQuery(&countDesc, &occlusion));
        const auto queryBefore = native_mask::GetDiagnostics().captured;
        context->Begin(occlusion.Get());
        {
            native_mask::Scope selected(0xff0000ff);
            draw({0, 0, .8f, .4f});
        }
        context->End(occlusion.Get());
        check(native_mask::GetDiagnostics().captured == queryBefore,
              "active native occlusion query prevents duplicate geometry");
        UINT64 visibleSamples{};
        HRESULT queryResult = S_FALSE;
        context->Flush();
        for (unsigned tries = 0; tries < 100 && queryResult == S_FALSE; ++tries) {
            queryResult = context->GetData(occlusion.Get(), &visibleSamples, sizeof(visibleSamples), 0);
            if (queryResult == S_FALSE)
                Sleep(1);
        }
        check(queryResult == S_OK && visibleSamples == 0,
              "private hidden capture cannot inflate the game's occlusion result");
        {
            native_mask::Scope selected(0xff0000ff);
            draw({0, 0, .8f, .4f});
        }
        check(native_mask::GetDiagnostics().captured == queryBefore + 1, "capture resumes after the native query ends");
        bind(false);
        D3D11_BUFFER_DESC streamDesc{};
        streamDesc.ByteWidth = 256;
        streamDesc.BindFlags = D3D11_BIND_STREAM_OUTPUT;
        ComPtr<ID3D11Buffer> stream;
        require(device->CreateBuffer(&streamDesc, nullptr, &stream));
        auto *streamTarget = stream.Get();
        UINT streamOffset{};
        context->SOSetTargets(1, &streamTarget, &streamOffset);
        const auto streamBefore = native_mask::GetDiagnostics().captured;
        {
            native_mask::Scope selected(0xff0000ff);
            draw({0, 0, .8f, .4f});
        }
        check(native_mask::GetDiagnostics().captured == streamBefore,
              "stream-output draw is never duplicated into native mask");
        context->SOSetTargets(0, nullptr, nullptr);
        native_mask::Annotate(reinterpret_cast<std::uintptr_t>(command.data()), args, 0xff0000ff);
        native_mask::Forget(reinterpret_cast<std::uintptr_t>(command.data()));
        VortexTestReplay(context.Get(), 6, 2, 2, -3, 7, reinterpret_cast<std::uintptr_t>(command.data()) + 4);
        check(native_mask::GetDiagnostics().captured == streamBefore,
              "nonselected command replacement invalidates an identical old tag");
        const auto captured = native_mask::GetDiagnostics().captured;
        bind(false);
        VortexTestReplay(context.Get(), 6, 2, 2, -3, 7, reinterpret_cast<std::uintptr_t>(command.data()) + 4);
        check(native_mask::GetDiagnostics().captured == captured, "replayed pointer cannot consume a tag twice");
        native_mask::Annotate(reinterpret_cast<std::uintptr_t>(command.data()), args, 0xff0000ff);
        native_mask::Discard();
        {
            native_mask::Scope selected(0xff0000ff);
            draw({0, 0, .8f, .4f});
        }
        check(native_mask::GetDiagnostics().captured == captured,
              "discard stops stale native scopes and queued captures");
        require(native_mask::Render(device.Get(), context.Get(), target.Get(), desc, nullptr, false, true, status));
        check(!native_mask::Enabled() && status.status == EffectsStatus::DepthUnavailable,
              "missing scene depth disables capture and hidden drawing");
        require(native_mask::Render(device.Get(), context.Get(), target.Get(), desc, dsv.Get(), false, true, status));
        bind(false);
        {
            native_mask::Scope selected(0xff0000ff);
            draw({0, 0, .8f, .4f});
        }
        auto resized = desc;
        resized.Width = 64;
        ComPtr<ID3D11Texture2D> smaller;
        ComPtr<ID3D11RenderTargetView> smallTarget;
        require(device->CreateTexture2D(&resized, nullptr, &smaller));
        require(device->CreateRenderTargetView(smaller.Get(), nullptr, &smallTarget));
        require(
            native_mask::Render(device.Get(), context.Get(), smallTarget.Get(), resized, nullptr, false, true, status));
        check(!status.meshCount && !native_mask::Enabled(),
              "resize discards old geometry and requires matching scene depth");
        context->ClearState();
        const auto refs = [](IUnknown *object) {
            const auto result = object->AddRef();
            object->Release();
            return result;
        };
        check(refs(target.Get()) == 2, "mask retains no backbuffer RTV after capture and unbind");
        require(scene_depth::Stop());
        native_mask::Release();
        native_mask::SetReplaySite(0);
        check(!native_mask::Enabled(), "teardown disarms native capture");
        check(MH_Uninitialize() == MH_OK, "all draw hooks removed cleanly");
    } catch (const std::exception &e) {
        std::printf("EXCEPTION: %s\n", e.what());
        ++failures;
        scene_depth::Stop();
        native_mask::Release();
        MH_Uninitialize();
    }
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
