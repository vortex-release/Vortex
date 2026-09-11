#include "model_preview.hpp"
#include "menu_background.hpp"
#include <DirectXMath.h>
#include <d3d11sdklayers.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <thread>
#include <limits>
using namespace awareness;
using namespace DirectX;
using Microsoft::WRL::ComPtr;
HMODULE OverlayModule() noexcept {
    return GetModuleHandleW(nullptr);
}
int main(int argc, char **argv) {
    int failures{};
    const auto check = [&](bool ok, const char *why) {
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", why);
        }
    };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0,
                                   D3D11_SDK_VERSION, &device, nullptr, &context);
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
        std::puts("D3D debug layer unavailable; using WARP for pixel verification.");
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device,
                               nullptr, &context);
    }
    if (FAILED(hr))
        return 1;
    ModelPreview model;
    check(model.Initialize(nullptr) == E_INVALIDARG, "missing preview device is rejected");
    check(SUCCEEDED(model.Initialize(device.Get())), "embedded skinned character and textures load");
    if (!model.View())
        return 1;
    const auto pixels = [&]() {
        ComPtr<ID3D11Resource> ghostResource;
        model.View()->GetResource(&ghostResource);
        ComPtr<ID3D11Texture2D> texture;
        ghostResource.As(&texture);
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.BindFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        device->CreateTexture2D(&desc, nullptr, &staging);
        context->CopyResource(staging.Get(), texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        std::vector<BYTE> value(std::size_t(desc.Width) * desc.Height * 4);
        for (UINT y = 0; y < desc.Height; ++y)
            std::memcpy(value.data() + std::size_t(y) * desc.Width * 4,
                        static_cast<BYTE *>(mapped.pData) + std::size_t(y) * mapped.RowPitch, desc.Width * 4);
        context->Unmap(staging.Get(), 0);
        return value;
    };
    EffectsConfiguration effects;
    check(model.Render(nullptr, nullptr, 0, false, effects) == E_INVALIDARG, "missing preview context is rejected");
    check(model.Render(context.Get(), nullptr, std::numeric_limits<float>::quiet_NaN(), true, effects) == E_INVALIDARG,
          "invalid frame time cannot poison animation state");
    model.Drag(std::numeric_limits<float>::infinity());
    if (argc == 2) {
        PreviewPose liveSample;
        std::ifstream input(argv[1], std::ios::binary);
        input.read(reinterpret_cast<char *>(&liveSample), sizeof(liveSample));
        if (!input || !liveSample.valid)
            return 1;
        check(SUCCEEDED(model.Render(context.Get(), &liveSample, 0, false, effects)), "live CS2 pose renders offline");
        auto sample = pixels();
        std::size_t visible{};
        for (std::size_t i = 0; i < sample.size(); i += 4) {
            visible += sample[i + 3] != 0;
            std::swap(sample[i], sample[i + 2]);
        }
        check(visible > 10000, "live CS2 pose has visible character geometry");
        BITMAPFILEHEADER file{};
        file.bfType = 0x4D42;
        file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
        file.bfSize = file.bfOffBits + static_cast<DWORD>(sample.size());
        BITMAPINFOHEADER info{};
        info.biSize = sizeof(info);
        info.biWidth = 384;
        info.biHeight = -576;
        info.biPlanes = 1;
        info.biBitCount = 32;
        auto path = std::filesystem::path(argv[1]);
        path.replace_extension(".bmp");
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(&file), sizeof(file));
        output.write(reinterpret_cast<const char *>(&info), sizeof(info));
        output.write(reinterpret_cast<const char *>(sample.data()), static_cast<std::streamsize>(sample.size()));
        check(output.good(), "live model readback saved");
    }
    check(SUCCEEDED(model.Render(context.Get(), nullptr, 0, false, effects)), "fallback model draws");
    const auto rest = pixels();
    std::size_t opaque{}, transparent{};
    for (std::size_t i = 3; i < rest.size(); i += 4) {
        opaque += rest[i] > 0;
        transparent += rest[i] == 0;
    }
    check(opaque > 10000 && transparent > 50000, "preview contains textured geometry on a transparent canvas");
    // Reconstruct an equivalent native world pose from the packaged reference pose.
    const auto resource = FindResourceW(OverlayModule(), MAKEINTRESOURCEW(102), RT_RCDATA);
    const auto *bytes = static_cast<const BYTE *>(LockResource(LoadResource(OverlayModule(), resource)));
    struct Bone {
        int parent;
        XMFLOAT3 t;
        XMFLOAT4 q;
        XMFLOAT3 s;
        XMFLOAT4X4 inverse;
    };
    std::array<Bone, 94> bones{};
    std::memcpy(bones.data(), bytes + 24, sizeof(bones));
    std::array<XMMATRIX, 94> world{};
    std::array<bool, 94> ready{};
    const auto evaluate = [&](auto &&self, int i) -> XMMATRIX {
        if (ready[i])
            return world[i];
        const auto &b = bones[i];
        world[i] = XMMatrixScaling(b.s.x, b.s.y, b.s.z) * XMMatrixRotationQuaternion(XMLoadFloat4(&b.q)) *
                   XMMatrixTranslation(b.t.x, b.t.y, b.t.z);
        if (b.parent >= 0)
            world[i] = world[i] * self(self, b.parent);
        ready[i] = true;
        return world[i];
    };
    const auto toSource = XMMatrixSet(0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1);
    PreviewPose pose;
    pose.valid = true;
    pose.validMask = (1u << PreviewJointCount) - 1;
    pose.entity.origin = {100, 200, 300};
    for (int i = 0; i < 25; ++i) {
        auto native = evaluate(evaluate, i) * toSource;
        XMVECTOR scale, rotation, translation;
        XMMatrixDecompose(&scale, &rotation, &translation, native);
        XMFLOAT3 t;
        XMFLOAT4 q;
        XMStoreFloat3(&t, translation);
        XMStoreFloat4(&q, rotation);
        pose.joints[i] = {
            {100 + t.x / .0254f, 200 + t.y / .0254f, 300 + t.z / .0254f}, XMVectorGetX(scale), {q.x, q.y, q.z, q.w}};
    }
    model.Render(context.Get(), &pose, 0, false, effects);
    const auto equivalent = pixels();
    std::size_t differences{};
    for (std::size_t i = 0; i < rest.size(); ++i)
        differences += std::abs(int(rest[i]) - int(equivalent[i])) > 3;
    check(differences < rest.size() / 100, "native bone units and basis reproduce the reference character pose");
    {
        ComPtr<ID3D11Resource> ghostResource;
        model.View()->GetResource(&ghostResource);
        ComPtr<ID3D11RenderTargetView> target;
        check(SUCCEEDED(device->CreateRenderTargetView(ghostResource.Get(), nullptr, &target)), "ghost target created");
        FrameSnapshot frame;
        frame.localEntityId = 1;
        frame.localTeam = 3;
        frame.cameraOrigin = {250, 200, 340};
        frame.viewport = {0, 0, 384, 576};
        pose.entity.valid = 1;
        pose.entity.id = 2;
        pose.entity.team = 2;
        pose.entity.health = pose.entity.maxHealth = 100;
        pose.entity.mins = {-16, -16, 0};
        pose.entity.maxs = {16, 16, 75};
        const auto basis = XMMatrixTranspose(toSource);
        const auto view = XMMatrixLookAtRH(XMVectorSet(std::sin(.22f) * 3, 1.1f, std::cos(.22f) * 3, 1),
                                           XMVectorSet(0, .95f, 0, 1), XMVectorSet(0, 1, 0, 0));
        XMFLOAT4X4 matrix;
        XMStoreFloat4x4(&matrix, XMMatrixTranspose(XMMatrixTranslation(-100, -200, -300) *
                                                   XMMatrixScaling(.0254f, .0254f, .0254f) * basis * view *
                                                   XMMatrixOrthographicRH(1.6f, 2.4f, .1f, 10)));
        std::memcpy(&frame.viewProjection, &matrix, sizeof(matrix));
        Configuration c;
        c.enabled = 1;
        c.teamFilter = TeamFilter::All;
        c.worldUnitsPerMeter = 39.3700787f;
        combat::Options o;
        o.ghosts = 1;
        o.ghostDuration = .25f;
        o.ghostEnemy = {.2f, .6f, 1, 1};
        auto history = std::make_unique<combat::GhostHistory>();
        history->Begin();
        history->Add(2, pose, 10, 1);
        history->End();
        history->Begin();
        history->Add(2, pose, 10.2, 1);
        history->Add(2, pose, 10.3, 1);
        history->End();
        auto replay = std::make_unique<combat::ReplayFrame>();
        const auto replayAt = [&](double now) -> const combat::ReplayFrame & {
            combat::BuildReplay(*history, now, .25f, *replay);
            return *replay;
        };
        const float clear[4]{};
        context->ClearRenderTargetView(target.Get(), clear);
        check(SUCCEEDED(
                  model.RenderGhosts(context.Get(), target.Get(), frame, frame.viewport, c, o, replayAt(10.35), 10.35)),
              "historical pose draws through production ghost pass");
        const auto stationary = pixels();
        check(std::all_of(stationary.begin(), stationary.end(), [](auto x) { return x == 0; }),
              "stationary player skips the replay pass");
        auto moved = pose;
        moved.entity.origin.y += 15;
        for (auto &joint : moved.joints)
            joint.position.y += 15;
        history->Begin();
        history->Add(2, moved, 10.35, 1);
        history->End();
        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = 384;
        depthDesc.Height = 576;
        depthDesc.MipLevels = depthDesc.ArraySize = depthDesc.SampleDesc.Count = 1;
        depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ComPtr<ID3D11Texture2D> liveDepth;
        ComPtr<ID3D11DepthStencilView> liveDepthView;
        check(SUCCEEDED(device->CreateTexture2D(&depthDesc, nullptr, &liveDepth)) &&
                  SUCCEEDED(device->CreateDepthStencilView(liveDepth.Get(), nullptr, &liveDepthView)),
              "current-player depth fixture created");
        context->ClearRenderTargetView(target.Get(), clear);
        context->ClearDepthStencilView(liveDepthView.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        GhostSurface current{frame.viewProjection, frame.viewport, target.Get(), liveDepthView.Get(), true};
        EffectsConfiguration playerColor;
        playerColor.materialColor = {.7f, .2f, .8f, 1};
        check(SUCCEEDED(model.Render(context.Get(), &moved, 0, false, playerColor, &current)),
              "current player color fixture renders");
        const auto foreground = pixels();
        check(SUCCEEDED(
                  model.RenderGhosts(context.Get(), target.Get(), frame, frame.viewport, c, o, replayAt(10.3), 10.3)),
              "moving historical poses render outside the current player");
        const auto ghost = pixels();
        std::size_t visible{}, overwritten{}, foregroundPixels{};
        for (std::size_t i = 0; i < ghost.size(); i += 4) {
            if (foreground[i + 3]) {
                ++foregroundPixels;
                overwritten += std::memcmp(foreground.data() + i, ghost.data() + i, 4) != 0;
            } else
                visible += ghost[i + 3] > 0;
        }
        check(foregroundPixels > 10000 && overwritten > 100,
              "nearby replay blends without a rectangular current-player cutout");
        check(visible > 100, "moving ghost remains visible outside the player silhouette");
        context->ClearRenderTargetView(target.Get(), clear);
        context->ClearDepthStencilView(liveDepthView.Get(), D3D11_CLEAR_DEPTH, 0, 0);
        model.RenderGhosts(context.Get(), target.Get(), frame, frame.viewport, c, o, replayAt(10.3), 10.3,
                           liveDepthView.Get());
        const auto blocked = pixels();
        check(std::all_of(blocked.begin(), blocked.end(), [](auto x) { return x == 0; }),
              "host scene depth occludes ghosts");
        auto reversedFrame = frame;
        for (int j = 0; j < 4; ++j)
            reversedFrame.viewProjection.m[2][j] = frame.viewProjection.m[3][j] - frame.viewProjection.m[2][j];
        context->ClearDepthStencilView(liveDepthView.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        model.RenderGhosts(context.Get(), target.Get(), reversedFrame, frame.viewport, c, o, replayAt(10.3), 10.3,
                           liveDepthView.Get(), true);
        const auto reversedBlocked = pixels();
        check(std::all_of(reversedBlocked.begin(), reversedBlocked.end(), [](auto x) { return x == 0; }),
              "reversed scene depth also occludes ghosts");
        BITMAPFILEHEADER fh{};
        fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER);
        fh.bfSize = fh.bfOffBits + static_cast<DWORD>(ghost.size());
        BITMAPINFOHEADER ih{};
        ih.biSize = sizeof(ih);
        ih.biWidth = 384;
        ih.biHeight = -576;
        ih.biPlanes = 1;
        ih.biBitCount = 32;
        auto bgra = ghost;
        for (std::size_t i = 0; i < bgra.size(); i += 4)
            std::swap(bgra[i], bgra[i + 2]);
        std::ofstream output("ghost-preview.bmp", std::ios::binary);
        output.write(reinterpret_cast<char *>(&fh), sizeof(fh));
        output.write(reinterpret_cast<char *>(&ih), sizeof(ih));
        output.write(reinterpret_cast<char *>(bgra.data()), bgra.size());
        context->ClearRenderTargetView(target.Get(), clear);
        model.RenderGhosts(context.Get(), target.Get(), frame, frame.viewport, c, o, replayAt(12), 12);
        const auto expired = pixels();
        check(std::all_of(expired.begin(), expired.end(), [](auto x) { return x == 0; }),
              "expired ghosts leave no pixels");
    }
    for (int i = 1; i < 17; ++i)
        pose.joints[i].position.z -= 10;
    model.Render(context.Get(), &pose, 0, false, effects);
    const auto live = pixels();
    check(live != equivalent, "changing live skeletal transforms updates the rendered character");
    model.Render(context.Get(), nullptr, .1f, true, effects);
    const auto animated = pixels();
    check(animated != rest, "fallback animates and rotates between frames");
    effects.materialEnabled = effects.glowEnabled = 1;
    effects.materialColor = {1, 0, 0, 1};
    model.Render(context.Get(), nullptr, 0, false, effects);
    const auto highlighted = pixels();
    std::size_t red{};
    for (std::size_t i = 0; i < highlighted.size(); i += 4)
        red += highlighted[i] > 200 && highlighted[i + 1] < 30;
    check(red > 10000, "highlight color binds to actual skinned model pixels");
    const auto imageResource = FindResourceW(OverlayModule(), MAKEINTRESOURCEW(111), RT_RCDATA);
    const auto imageBytes = static_cast<const BYTE *>(LockResource(LoadResource(OverlayModule(), imageResource)));
    const auto imageLength = SizeofResource(OverlayModule(), imageResource);
    ImagePixels image;
    check(SUCCEEDED(DecodeImage(nullptr, {imageBytes, imageLength}, image)) && image.width && image.height,
          "PNG decoder reads embedded image");
    const BYTE invalid[]{1, 2, 3};
    check(FAILED(DecodeImage(nullptr, invalid, image)) && image.rgba.empty(),
          "invalid image is rejected without stale pixels");
    const auto path = std::filesystem::current_path() / L"preview-\x732B.png";
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(imageBytes), imageLength);
    }
    VisualOptions visual;
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, visual.backgroundPath, sizeof(visual.backgroundPath), nullptr,
                        nullptr);
    {
        MenuBackground background;
        background.Update(device.Get(), visual);
        for (int i = 0; i < 500 && !background.View(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            background.Update(device.Get(), visual);
        }
        check(background.View() != nullptr, "background loads asynchronously from a Unicode path");
        const auto *cached = background.View();
        for (int i = 0; i < 10; ++i)
            background.Update(device.Get(), visual);
        check(background.View() == cached, "unchanged background reuses its texture");
        visual.backgroundPath[0] = 0;
        background.Update(device.Get(), visual);
        check(!background.View(), "removing an image releases the cached texture");
    }
    std::filesystem::remove(path);
    context->ClearState();
    context->Flush();
    ComPtr<ID3D11InfoQueue> queue;
    device.As(&queue);
    if (queue)
        for (UINT64 i = 0; i < queue->GetNumStoredMessages(); ++i) {
            SIZE_T length{};
            queue->GetMessage(i, nullptr, &length);
            std::vector<BYTE> storage(length);
            auto *message = reinterpret_cast<D3D11_MESSAGE *>(storage.data());
            queue->GetMessage(i, message, &length);
            check(message->Severity > D3D11_MESSAGE_SEVERITY_ERROR, message->pDescription);
        }
    std::printf("Model, live retargeting, animation, image loading: %d failures\n", failures);
    return failures ? 1 : 0;
}
