#include "scene_grade.hpp"
#include <array>
#include <cstdio>
#include <d3d11sdklayers.h>
#include <vector>
using Microsoft::WRL::ComPtr;
int main() {
    using namespace awareness;
    unsigned failures{};
    const auto check = [&](bool ok, const char *message) {
        if (!ok) {
            ++failures;
            std::printf("FAIL %s\n", message);
        }
    };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0,
                                D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device,
                               nullptr, &context);
    if (FAILED(hr))
        return 2;
    SceneGrade grade;
    ComPtr<ID3D11Texture2D> texture, staging;
    ComPtr<ID3D11RenderTargetView> target;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 32;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    auto create = [&]() {
        target.Reset();
        texture.Reset();
        staging.Reset();
        check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)) &&
                  SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &target)),
              "color target");
        auto copy = desc;
        copy.BindFlags = 0;
        copy.Usage = D3D11_USAGE_STAGING;
        copy.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check(SUCCEEDED(device->CreateTexture2D(&copy, nullptr, &staging)), "readback target");
    };
    create();
    const float original[]{.8f, .4f, .2f, 1};
    auto pixel = [&](unsigned x = 16, unsigned y = 16) {
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->CopyResource(staging.Get(), texture.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        std::array<unsigned char, 4> result{};
        if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) {
            std::memcpy(result.data(), static_cast<unsigned char *>(map.pData) + y * map.RowPitch + x * 4, 4);
            context->Unmap(staging.Get(), 0);
        }
        return result;
    };
    combat::Options o;
    context->ClearRenderTargetView(target.Get(), original);
    check(grade.Render(device.Get(), context.Get(), texture.Get(), target.Get(), o) == S_FALSE && pixel()[0] == 204,
          "disabled scene controls leave the source unchanged");
    o.contrast = 1;
    o.worldDarkness = 0;
    check(grade.Render(device.Get(), context.Get(), texture.Get(), target.Get(), o) == S_FALSE,
          "neutral scene settings skip the texture copy and draw");
    o.sceneSaturation = 0;
    check(SUCCEEDED(grade.Render(device.Get(), context.Get(), texture.Get(), target.Get(), o)),
          "grade shaders compile and render");
    auto grey = pixel();
    check(grey[0] == grey[1] && grey[1] == grey[2] && grey[0] > 90, "saturation changes actual game-image pixels");
    context->ClearRenderTargetView(target.Get(), original);
    o.sceneSaturation = 1;
    o.worldDarkness = .5f;
    grade.Render(device.Get(), context.Get(), texture.Get(), target.Get(), o);
    check(pixel()[0] >= 101 && pixel()[0] <= 103, "darkness halves the image brightness");
    context->ClearRenderTargetView(target.Get(), original);
    o.worldDarkness = 0;
    o.sceneExposure = -1;
    grade.Render(device.Get(), context.Get(), texture.Get(), target.Get(), o);
    check(pixel()[0] >= 101 && pixel()[0] <= 103, "exposure reaches the shader");
    context->ClearRenderTargetView(target.Get(), original);
    o.sceneExposure = 0;
    o.sceneTintStrength = 1;
    o.sceneTint = {0, 1, 0, 1};
    grade.Render(device.Get(), context.Get(), texture.Get(), target.Get(), o);
    auto tinted = pixel();
    check(tinted[0] == 0 && tinted[1] > 90 && tinted[2] == 0, "tint changes independent channels");
    o.sceneTintStrength = 0;
    o.sceneVignette = .8f;
    context->ClearRenderTargetView(target.Get(), original);
    grade.Render(device.Get(), context.Get(), texture.Get(), target.Get(), o);
    check(pixel(0, 0)[0] < pixel()[0], "vignette darkens corners rather than the center");
    o = {};
    o.contrast = 1;
    o.worldDarkness = 0;
    o.sceneGamma = 1.5f;
    context->ClearRenderTargetView(target.Get(), original);
    grade.Render(device.Get(), context.Get(), texture.Get(), target.Get(), o);
    check(pixel()[1] > 110, "gamma changes midtone pixels");
    o.sceneGamma = 1;
    o.sceneTemperature = 1;
    context->ClearRenderTargetView(target.Get(), original);
    grade.Render(device.Get(), context.Get(), texture.Get(), target.Get(), o);
    check(pixel()[0] > 204 && pixel()[2] < 51, "temperature warms red and cools blue");
    o.sceneTemperature = 0;
    o.sceneVibrance = 1;
    context->ClearRenderTargetView(target.Get(), original);
    grade.Render(device.Get(), context.Get(), texture.Get(), target.Get(), o);
    check(pixel()[0] > 204 && pixel()[2] < 51, "vibrance expands chroma");
    o.sceneVibrance = 0;
    o.sceneShadows = .2f;
    const float dark[]{.15f, .15f, .15f, 1};
    context->ClearRenderTargetView(target.Get(), dark);
    grade.Render(device.Get(), context.Get(), texture.Get(), target.Get(), o);
    check(pixel()[0] > 65, "shadow lift changes dark pixels");
    o.sceneShadows = 0;
    o.sceneHighlights = -.2f;
    const float bright[]{.9f, .9f, .9f, 1};
    context->ClearRenderTargetView(target.Get(), bright);
    grade.Render(device.Get(), context.Get(), texture.Get(), target.Get(), o);
    check(pixel()[0] < 210, "highlight recovery changes bright pixels");
    context->ClearState();
    desc.Width = 48;
    create();
    context->ClearRenderTargetView(target.Get(), original);
    check(SUCCEEDED(grade.Render(device.Get(), context.Get(), texture.Get(), target.Get(), o)) && pixel()[3] == 255,
          "cached scene resources recreate safely on resize");
    context->ClearState();
    context->Flush();
    ComPtr<ID3D11InfoQueue> queue;
    device.As(&queue);
    if (queue)
        for (UINT64 i = 0; i < queue->GetNumStoredMessages(); ++i) {
            SIZE_T size{};
            queue->GetMessage(i, nullptr, &size);
            std::vector<unsigned char> bytes(size);
            auto *m = reinterpret_cast<D3D11_MESSAGE *>(bytes.data());
            queue->GetMessage(i, m, &size);
            check(m->Severity > D3D11_MESSAGE_SEVERITY_ERROR, m->pDescription);
        }
    // Keep the renderer alive across a replacement device with identical target size.
    // Size-only caching would otherwise reuse every old shader and texture.
    ComPtr<ID3D11Device> replacement;
    ComPtr<ID3D11DeviceContext> replacementContext;
    check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                      &replacement, nullptr, &replacementContext)),
          "replacement device");
    if (replacement) {
        device = replacement;
        context = replacementContext;
        create();
        o = {};
        o.contrast = 1;
        o.worldDarkness = .5f;
        context->ClearRenderTargetView(target.Get(), original);
        check(SUCCEEDED(grade.Render(device.Get(), context.Get(), texture.Get(), target.Get(), o)) &&
                  pixel()[0] >= 101 && pixel()[0] <= 103,
              "device replacement rebuilds resources and uploads constants at unchanged dimensions");
        context->ClearState();
    }
    std::printf("Scene grading: %u failures\n", failures);
    return failures ? 1 : 0;
}
