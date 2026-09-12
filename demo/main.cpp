#include "../app/app_paths.hpp"
#include <awareness/OverlayApi.hpp>
#include <awareness/Trajectories.hpp>
#include <awareness/TrackingApi.hpp>
#include <awareness/EffectsApi.hpp>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <dxgi.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "camera_tracking.hpp"
#include "model_geometry.hpp"
#include "trajectory_draw.hpp"
#include "combat_draw.hpp"
using Microsoft::WRL::ComPtr;
using namespace awareness;
using namespace DirectX;
namespace {
bool healthKey{}, teamKey{};
std::uint32_t demoTrackingKey{VK_RBUTTON}, demoWheelKey{};
ULONGLONG demoWheelUntil{};
std::ofstream logFile;
void Require(bool condition, const char *what) {
    if (!condition)
        throw std::runtime_error(what);
}
void Check(HRESULT hr, const char *what) {
    if (FAILED(hr)) {
        char text[256]{};
        std::snprintf(text, sizeof(text), "%s (HRESULT 0x%08lX)", what, static_cast<unsigned long>(hr));
        throw std::runtime_error(text);
    }
}
void Pass(const char *message) {
    logFile << "PASS: " << message << std::endl;
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM key, LPARAM value) {
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
        demoWheelKey = binding::Button(message, key, value);
        demoWheelUntil = GetTickCount64() + 120;
    }
    if (message == WM_KILLFOCUS)
        demoWheelUntil = 0;
    if (message == WM_KEYDOWN && !(value & (1LL << 30))) {
        if (key == VK_ESCAPE && demoTrackingKey != VK_ESCAPE)
            DestroyWindow(window);
        if (key == 'H')
            healthKey = true;
        if (key == 'T')
            teamKey = true;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, key, value);
}
struct Plugin {
    HMODULE module{};
    decltype(&AwarenessInitialize) initialize{};
    decltype(&AwarenessSubmitFrame) submit{};
    decltype(&AwarenessSetConfiguration) configure{};
    decltype(&AwarenessGetConfiguration) configuration{};
    decltype(&AwarenessGetStatistics) statistics{};
    decltype(&AwarenessShutdown) shutdown{};
    decltype(&AwarenessStartAutomatic) automatic{};
    decltype(&AwarenessSetMenuVisible) menu{};
    decltype(&AwarenessSetTrackingConfiguration) setTracking{};
    decltype(&AwarenessGetTrackingConfiguration) getTracking{};
    decltype(&AwarenessSubmitCameraInput) cameraInput{};
    decltype(&AwarenessGetTrackingState) trackingState{};
    decltype(&AwarenessSetEffectsConfiguration) setEffects{};
    decltype(&AwarenessGetEffectsConfiguration) getEffects{};
    decltype(&AwarenessSubmitEffectsInput) effectsInput{};
    decltype(&AwarenessGetEffectsState) effectsState{};
    void Models(const FrameSnapshot &frame, ID3D11DepthStencilView *depth = nullptr, float seconds = 0) {
        const auto model = awareness::demo::MakeModelGeometry(frame, seconds);
        EffectsInput input;
        input.vertices = model.vertices.data();
        input.vertexCount = static_cast<std::uint32_t>(model.vertices.size());
        input.meshes = model.meshes.data();
        input.meshCount = static_cast<std::uint32_t>(model.meshes.size());
        input.sceneDepth = depth;
        Check(effectsInput(&input), "submit simulated model triangles");
    }
    explicit Plugin(const std::filesystem::path &directory) {
        module = LoadLibraryExW((directory / L"EntityAwarenessOverlay.dll").c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        Require(module != nullptr, "LoadLibraryExW");
        initialize = reinterpret_cast<decltype(initialize)>(GetProcAddress(module, "AwarenessInitialize"));
        submit = reinterpret_cast<decltype(submit)>(GetProcAddress(module, "AwarenessSubmitFrame"));
        configure = reinterpret_cast<decltype(configure)>(GetProcAddress(module, "AwarenessSetConfiguration"));
        configuration = reinterpret_cast<decltype(configuration)>(GetProcAddress(module, "AwarenessGetConfiguration"));
        statistics = reinterpret_cast<decltype(statistics)>(GetProcAddress(module, "AwarenessGetStatistics"));
        shutdown = reinterpret_cast<decltype(shutdown)>(GetProcAddress(module, "AwarenessShutdown"));
        automatic = reinterpret_cast<decltype(automatic)>(GetProcAddress(module, "AwarenessStartAutomatic"));
        menu = reinterpret_cast<decltype(menu)>(GetProcAddress(module, "AwarenessSetMenuVisible"));
        setTracking =
            reinterpret_cast<decltype(setTracking)>(GetProcAddress(module, "AwarenessSetTrackingConfiguration"));
        getTracking =
            reinterpret_cast<decltype(getTracking)>(GetProcAddress(module, "AwarenessGetTrackingConfiguration"));
        cameraInput = reinterpret_cast<decltype(cameraInput)>(GetProcAddress(module, "AwarenessSubmitCameraInput"));
        trackingState = reinterpret_cast<decltype(trackingState)>(GetProcAddress(module, "AwarenessGetTrackingState"));
        setEffects = reinterpret_cast<decltype(setEffects)>(GetProcAddress(module, "AwarenessSetEffectsConfiguration"));
        getEffects = reinterpret_cast<decltype(getEffects)>(GetProcAddress(module, "AwarenessGetEffectsConfiguration"));
        effectsInput = reinterpret_cast<decltype(effectsInput)>(GetProcAddress(module, "AwarenessSubmitEffectsInput"));
        effectsState = reinterpret_cast<decltype(effectsState)>(GetProcAddress(module, "AwarenessGetEffectsState"));
        Require(initialize && submit && configure && configuration && statistics && shutdown && automatic && menu,
                "DLL export lookup");
        Require(setTracking && getTracking && cameraInput && trackingState, "DLL camera export lookup");
        Require(setEffects && getEffects && effectsInput && effectsState, "DLL effects export lookup");
    }
    ~Plugin() {
        // A failed shutdown leaves the module mapped. Never unload live callbacks.
        if (module && shutdown && SUCCEEDED(shutdown()))
            FreeLibrary(module);
    }
    void Unload() {
        Check(shutdown(), "AwarenessShutdown");
        Require(FreeLibrary(module) != FALSE, "FreeLibrary");
        module = nullptr;
    }
};
struct Graphics {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> chain;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> depth;
    ComPtr<ID3D11InfoQueue> infoQueue;
    UINT width{1280}, height{800};
    const float background[4]{.025f, .035f, .065f, 1.f};
    void Initialize(HWND window, bool smoke) {
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = width;
        desc.BufferDesc.Height = height;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 1;
        desc.OutputWindow = window;
        desc.Windowed = TRUE;
        // Bitblt sequential retains buffer contents after Present for smoke readback.
        desc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
        const auto driver = smoke ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE;
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_DEBUG, levels, 1,
                                                   D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context);
        if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING)
            hr = D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &desc, &chain,
                                               &device, nullptr, &context);
        if (FAILED(hr) && !smoke)
            hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1, D3D11_SDK_VERSION,
                                               &desc, &chain, &device, nullptr, &context);
        Check(hr, "D3D11CreateDeviceAndSwapChain");
        device.As(&infoQueue);
        CreateTargets();
    }
    void CreateTargets() {
        ComPtr<ID3D11Texture2D> buffer;
        Check(chain->GetBuffer(0, IID_PPV_ARGS(&buffer)), "GetBuffer");
        Check(device->CreateRenderTargetView(buffer.Get(), nullptr, &target), "CreateRenderTargetView");
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ComPtr<ID3D11Texture2D> texture;
        Check(device->CreateTexture2D(&desc, nullptr, &texture), "host depth texture");
        Check(device->CreateDepthStencilView(texture.Get(), nullptr, &depth), "host depth view");
    }
    void Resize(UINT w, UINT h) {
        context->ClearState();
        target.Reset();
        depth.Reset();
        Check(chain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
        width = w;
        height = h;
        CreateTargets();
    }
    void Clear() {
        auto *view = target.Get();
        context->OMSetRenderTargets(1, &view, depth.Get());
        context->ClearRenderTargetView(target.Get(), background);
        context->ClearDepthStencilView(depth.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, .35f, 7);
        D3D11_VIEWPORT viewport{11.f, 13.f, 400.f, 300.f, .1f, .9f};
        context->RSSetViewports(1, &viewport); // Deliberate sentinel state, not the overlay viewport.
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        context->OMSetBlendState(nullptr, nullptr, 0x12345678u);
    }
    void VerifyState() {
        ComPtr<ID3D11RenderTargetView> view;
        ComPtr<ID3D11DepthStencilView> dsv;
        context->OMGetRenderTargets(1, &view, &dsv);
        Require(view.Get() == target.Get() && dsv.Get() == depth.Get(), "host target and depth bindings restored");
        D3D11_VIEWPORT v{};
        UINT count = 1;
        context->RSGetViewports(&count, &v);
        Require(count == 1 && v.TopLeftX == 11.f && v.TopLeftY == 13.f && v.Width == 400.f && v.Height == 300.f &&
                    v.MinDepth == .1f && v.MaxDepth == .9f,
                "host viewport restored");
        D3D11_PRIMITIVE_TOPOLOGY topology{};
        context->IAGetPrimitiveTopology(&topology);
        Require(topology == D3D11_PRIMITIVE_TOPOLOGY_LINELIST, "host topology restored");
        ComPtr<ID3D11BlendState> blend;
        float factors[4]{};
        UINT mask{};
        context->OMGetBlendState(&blend, factors, &mask);
        Require(!blend && mask == 0x12345678u, "host sample mask restored");
    }
    std::vector<unsigned char> ReadPixels() {
        ComPtr<ID3D11Texture2D> buffer, staging;
        Check(chain->GetBuffer(0, IID_PPV_ARGS(&buffer)), "readback GetBuffer");
        D3D11_TEXTURE2D_DESC desc{};
        buffer->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Check(device->CreateTexture2D(&desc, nullptr, &staging), "readback staging texture");
        context->CopyResource(staging.Get(), buffer.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "readback Map");
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
        for (UINT y = 0; y < height; ++y)
            std::memcpy(pixels.data() + static_cast<std::size_t>(y) * width * 4,
                        static_cast<unsigned char *>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                        width * 4);
        context->Unmap(staging.Get(), 0);
        return pixels;
    }
    void VerifyDepth() {
        ComPtr<ID3D11Resource> resource;
        depth->GetResource(&resource);
        ComPtr<ID3D11Texture2D> texture, staging;
        Check(resource.As(&texture), "host depth texture interface");
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Check(device->CreateTexture2D(&desc, nullptr, &staging), "depth readback texture");
        context->CopyResource(staging.Get(), texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "depth readback Map");
        const auto value = *static_cast<const std::uint32_t *>(mapped.pData);
        context->Unmap(staging.Get(), 0);
        const float depthValue = static_cast<float>(value & 0xFFFFFFu) / 16777215.f;
        Require(std::abs(depthValue - .35f) < .001f && (value >> 24) == 7u, "host depth/stencil contents preserved");
    }
    void VerifyDebugLayer() {
        if (!infoQueue) {
            logFile << "D3D11 debug layer unavailable; GPU readback checks still ran.\n";
            return;
        }
        const auto count = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 i = 0; i < count; ++i) {
            SIZE_T length{};
            infoQueue->GetMessage(i, nullptr, &length);
            std::vector<unsigned char> storage(length);
            auto *message = reinterpret_cast<D3D11_MESSAGE *>(storage.data());
            if (SUCCEEDED(infoQueue->GetMessage(i, message, &length)) &&
                message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) {
                logFile << message->pDescription << std::endl;
                Require(message->Severity > D3D11_MESSAGE_SEVERITY_ERROR, "D3D11 debug layer error");
            }
        }
        Pass("D3D11 debug layer has no errors");
    }
};
Configuration MakeConfiguration() {
    Configuration c;
    wchar_t windowsDirectory[MAX_PATH]{};
    if (GetWindowsDirectoryW(windowsDirectory, MAX_PATH)) {
        const auto font = std::filesystem::path(windowsDirectory) / L"Fonts" / L"segoeui.ttf";
        if (std::filesystem::exists(font))
            WideCharToMultiByte(CP_UTF8, 0, font.c_str(), -1, c.fontPath, sizeof(c.fontPath), nullptr, nullptr);
    }
    return c;
}
FrameSnapshot MakeFrame(float seconds, UINT width, UINT height) {
    FrameSnapshot f;
    f.localTeam = 1;
    f.cameraOrigin = {0.f, 2.f, -8.f};
    const auto view = XMMatrixLookAtLH(XMVectorSet(0, 2, -8, 1), XMVectorSet(0, 1, 12, 1), XMVectorSet(0, 1, 0, 0));
    const auto projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, static_cast<float>(width) / height, .1f, 160.f);
    XMFLOAT4X4 transposed;
    XMStoreFloat4x4(&transposed, XMMatrixTranspose(view * projection));
    std::memcpy(f.viewProjection.m, &transposed, sizeof(transposed));
    f.entityCount = 8;
    const char *names[]{"Scout", "Defender", "Medic", "Flanker", "Distant AI", "Dormant", "Eliminated", "Self"};
    const Vector3 positions[]{{-4, 0, 9},  {0, 0, 13}, {4, 0, 10}, {-12, 0, 22},
                              {12, 0, 70}, {2, 0, 4},  {-2, 0, 4}, {0, 0, 3}};
    const float health[]{90, 48, 20, 70, 85, 100, 0, 100};
    for (std::uint32_t i = 0; i < f.entityCount; ++i) {
        auto &e = f.entities[i];
        e.id = i + 1;
        e.valid = 1;
        e.team = (i % 2 == 0) ? 1 : 2;
        e.health = health[i];
        e.maxHealth = 100.f;
        e.origin = positions[i];
        e.origin.x += std::sin(seconds * .6f + static_cast<float>(i)) * .4f;
        e.mins = {-.4f, 0.f, -.4f};
        e.maxs = {.4f, 1.85f, .4f};
        std::snprintf(e.name, sizeof(e.name), "%s", names[i]);
        constexpr std::uint32_t weapons[]{7, 16, 9, 61, 40, 4, 7, 16};
        f.weaponDefinitionIndices[i] = weapons[i];
        // Synthetic rig for the demo host; real hosts submit animated world-space nodes.
        constexpr float nodeHeights[]{1.72f, 1.5f, 1.25f, .85f};
        for (int node = 0; node < 4; ++node) {
            const int bone = static_cast<int>(TargetBones[node]);
            f.bones[i].positions[bone] = e.origin + Vector3{0, nodeHeights[node], 0};
            f.bones[i].validMask |= 1u << bone;
        }
        if (i == 1 && (static_cast<int>(seconds) / 4) % 2)
            f.weaponDefinitionIndices[i] = 4;
    }
    f.entities[5].dormant = 1;
    f.localEntityId = 8;
    return f;
}
void SaveBitmap(const std::filesystem::path &path, std::vector<unsigned char> pixels, UINT width, UINT height) {
    for (std::size_t i = 0; i < pixels.size(); i += 4)
        std::swap(pixels[i], pixels[i + 2]);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = static_cast<LONG>(width);
    info.biHeight = -static_cast<LONG>(height);
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(&file), sizeof(file));
    output.write(reinterpret_cast<const char *>(&info), sizeof(info));
    output.write(reinterpret_cast<const char *>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    Require(output.good(), "write smoke-test bitmap");
}
struct ProfileBackup {
    std::filesystem::path path;
    bool existed{};
    std::vector<char> bytes;
    explicit ProfileBackup(std::filesystem::path p) : path(std::move(p)), existed(std::filesystem::exists(path)) {
        if (existed) {
            std::ifstream input(path, std::ios::binary);
            Require(input.good(), "read original profile backup");
            bytes.assign(std::istreambuf_iterator<char>(input), {});
        }
    }
    ~ProfileBackup() {
        if (existed) {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        } else {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    }
};
void Smoke(Graphics &g, const std::filesystem::path &directory) {
    // Exercise the optional decorative ImGui stroke in an isolated WARP scene.
    // Depth-aware production trajectories have their own GPU fixture.
    {
        struct DrawScope {
            ImGuiContext *previous{ImGui::GetCurrentContext()};
            ImGuiContext *context{ImGui::CreateContext()};
            bool backend{};
            ~DrawScope() {
                if (backend)
                    ImGui_ImplDX11_Shutdown();
                ImGui::DestroyContext(context);
                ImGui::SetCurrentContext(previous);
            }
        } scope;
        ImGui::SetCurrentContext(scope.context);
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = {static_cast<float>(g.width), static_cast<float>(g.height)};
        io.DeltaTime = 1.f / 60;
        io.Fonts->AddFontDefault();
        Require(scope.backend = ImGui_ImplDX11_Init(g.device.Get(), g.context.Get()), "stroke fixture backend");
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        auto *draw = ImGui::GetBackgroundDrawList();
        flight::PathStyle style;
        // This fixture intentionally checks optional glow, independent of the
        // quieter no-glow defaults used by new profiles.
        style.trailGlow = style.shotGlow = 1;
        style.trailStrength = 2.f;
        style.shotStrength = 2.4f;
        style.trailWidth = 2.4f;
        style.shotWidth = 2.2f;
        style.previewWidth = 1.6f;
        style.shotStart = {1.f, .75f, .12f, 1.f};
        style.shotEnd = style.shotCore = {1.f, 1.f, 1.f, 1.f};
        draw->AddText({100, 95}, IM_COL32_WHITE, "Held preview");
        draw->AddText({100, 245}, IM_COL32_WHITE, "Flight trail");
        draw->AddText({100, 445}, IM_COL32_WHITE, "Bullet tracer");
        flight::ScreenPolyline held(*draw, style.previewHE, 1, style.previewWidth, style.previewGlow != 0,
                                    style.previewStrength);
        flight::ScreenPolyline flying(*draw, style.trailHE, 1, style.trailWidth, style.trailGlow != 0,
                                      style.trailStrength);
        for (int i = 0; i < 48; ++i) {
            const auto point = [](int j, float y) {
                const float t = j / 48.f;
                return ImVec2{100 + 800 * t, y - 90 * std::sin(t * 3.14159265f)};
            };
            held.Add(point(i, 230), point(i + 1, 230));
            flying.Add(point(i, 380), point(i + 1, 380));
        }
        held.Flush();
        flying.Flush();
        flight::Tracers fixtureShots;
        fixtureShots.Add({{200.f / g.width - 1, 1 - 1000.f / g.height, .5f},
                          {1800.f / g.width - 1, 1 - 1000.f / g.height, .5f},
                          1,
                          2,
                          1});
        flight::Draw(*draw, {}, {}, fixtureShots, false, false, true, flight::Shots::All, 1, 2, Matrix4x4::Identity(),
                     {0, 0, static_cast<float>(g.width), static_cast<float>(g.height)}, 1, 1, style);
        ImGui::Render();
        g.Clear();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const auto pixels = g.ReadPixels();
        SaveBitmap(directory / L"paths-glow-preview.bmp", pixels, g.width, g.height);
        const auto red = [&](unsigned x, unsigned y) { return pixels[(y * g.width + x) * 4]; };
        Require(red(500, 500) > 220 && red(500, 504) > 50 && red(500, 519) < 15,
                "bullet glow has a bright core, soft outer light and bounded extent");
        Require(red(500, 140) > 100 && red(500, 147) < 15 && red(500, 297) > 15,
                "held preview stays sharp while actual flight has an outer glow");
        g.VerifyState();
        g.VerifyDepth();
        Pass("optional decorative stroke GPU pixels and glow comparison image");
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        draw = ImGui::GetBackgroundDrawList();
        combat::Area fire{1, combat::AreaType::Fire, {0, 0, 0}, 1.5f, 0};
        fire.boundaryCount = 48;
        for (unsigned i = 0; i < 48; ++i) {
            const float a = i * 6.2831853f / 48, radius = 1.5f + .15f * std::cos(3 * a);
            fire.boundary[i] = {std::cos(a) * radius, std::sin(a) * radius, 0};
        }
        XMFLOAT4X4 projected;
        XMStoreFloat4x4(
            &projected,
            XMMatrixTranspose(XMMatrixLookAtLH(XMVectorSet(0, -4, 2.6f, 1), XMVectorZero(), XMVectorSet(0, 0, 1, 0)) *
                              XMMatrixPerspectiveFovLH(1.1f, static_cast<float>(g.width) / g.height, .05f, 100)));
        Matrix4x4 areaMatrix;
        std::memcpy(&areaMatrix, &projected, sizeof(projected));
        combat::AreaShape(*draw, fire, {.3f, .25f, 1, .3f}, areaMatrix,
                          {0, 0, static_cast<float>(g.width), static_cast<float>(g.height)}, 1);
        ImGui::Render();
        g.Clear();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const auto splash = g.ReadPixels();
        SaveBitmap(directory / L"fire-footprint.bmp", splash, g.width, g.height);
        std::size_t lit{};
        for (std::size_t i = 0; i < splash.size(); i += 4)
            lit += splash[i + 2] > 50;
        Require(lit > 10000, "fire footprint has a visible translucent fill and luminous boundary");
        Pass("projected utility footprint GPU pixels");
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        draw = ImGui::GetBackgroundDrawList();
        combat::Options markers;
        markers.hitMarker = 1;
        markers.markerSize = 8;
        const Viewport markerViewport{0, 0, static_cast<float>(g.width), static_cast<float>(g.height)};
        const std::array<float, 3> ages{.1f, markers.markerHold + markers.markerDuration * .5f, 1.f};
        const char *captions[]{"Hold", "Fade", "Finished"};
        for (unsigned i = 0; i < 3; ++i) {
            const float x = 200.f + 300.f * i, y = 300;
            draw->AddRectFilled({x - 105, y - 80}, {x + 105, y + 80}, IM_COL32(65, 66, 73, 255), 8);
            draw->AddText({x - 20, y + 105}, IM_COL32_WHITE, captions[i]);
            const combat::Hit hit{1, 2, {2 * x / g.width - 1, 1 - 2 * y / g.height, .5f}, 30, 10, false};
            combat::DrawHitMarker(*draw, hit, Matrix4x4::Identity(), markerViewport, markers, 1, 10 + ages[i]);
        }
        ImGui::Render();
        g.Clear();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const auto hitPixels = g.ReadPixels();
        SaveBitmap(directory / L"hitmarkers-preview.bmp", hitPixels, g.width, g.height);
        const auto markerPeak = [&](unsigned cx) {
            unsigned peak{};
            for (unsigned y = 288; y < 312; ++y)
                for (unsigned x = cx - 12; x < cx + 12; ++x)
                    peak = std::max(peak, static_cast<unsigned>(hitPixels[(y * g.width + x) * 4]));
            return peak;
        };
        Require(markerPeak(200) > 220 && markerPeak(500) > 90 && markerPeak(500) < markerPeak(200) &&
                    markerPeak(800) < 80,
                "world marker GPU pixels hold, fade and disappear");
        Pass("world hitmarker fade GPU pixels");
    }
    struct SteamFixtureScope {
        HMODULE module{};
        ~SteamFixtureScope() {
            if (module)
                FreeLibrary(module);
        }
    } steamFixture{LoadLibraryExW((directory / L"steam_api64.dll").c_str(), nullptr,
                                  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)};
    SetEnvironmentVariableW(L"VORTEX_UI_TEST", L"1");
    ProfileBackup profile(vortex::DataDirectory() / L"OverlaySettings.ini");
    const auto settleMenu = [&] {
        for (int i = 0; i < 4; ++i) {
            Sleep(50);
            g.Clear();
            Check(g.chain->Present(0, 0), "menu animation frame");
        }
    };
    const auto setMenu = [&](Plugin &plugin, BOOL shown) {
        const auto hr = plugin.menu(shown);
        if (SUCCEEDED(hr))
            settleMenu();
        return hr;
    };
    DXGI_SWAP_CHAIN_DESC desc{};
    Check(g.chain->GetDesc(&desc), "get test window");
    const auto window = desc.OutputWindow;
    const auto originalProcedure = GetWindowLongPtrW(window, GWLP_WNDPROC);
    for (int cycle = 0; cycle < 2; ++cycle) {
        Plugin p(directory);
        auto c = MakeConfiguration();
        Check(cycle ? p.automatic(window, &c) : p.initialize(g.chain.Get(), &c), "start overlay");
        Check(p.menu(FALSE), "initially hide menu");
        Pass(cycle ? "automatic capture armed without a swap-chain argument" : "manual Present trampoline installed");
        auto f = MakeFrame(0, g.width, g.height);
        auto invalid = f;
        invalid.entityCount = 65;
        Require(p.submit(&invalid) == E_INVALIDARG, "reject oversized entity list");
        auto badConfig = c;
        badConfig.maxDistanceMeters = c.fadeStartMeters;
        Require(p.configure(&badConfig) == E_INVALIDARG, "reject invalid fade range");
        Check(p.submit(&f), "AwarenessSubmitFrame");
        Check(g.chain->Present(0, DXGI_PRESENT_TEST), "test Present");
        Statistics stats;
        Check(p.statistics(&stats), "statistics");
        Require(stats.renderedFrames == 0, "DXGI_PRESENT_TEST must skip rendering");
        g.Clear();
        Check(g.chain->Present(0, 0), "hooked Present");
        Check(p.statistics(&stats), "statistics");
        Check(stats.lastRenderResult, "overlay render result");
        logFile << "Initial frame: presents=" << stats.presentCalls << " renders=" << stats.renderedFrames
                << " entities=" << stats.drawnEntities << " result=" << stats.lastRenderResult << std::endl;
        Require(stats.renderedFrames == 1 && stats.drawnEntities == 5, "five valid entities drawn");
        g.VerifyState();
        g.VerifyDepth();
        const auto pixels = g.ReadPixels();
        std::size_t changed = 0;
        for (std::size_t i = 0; i < pixels.size(); i += 4)
            if (pixels[i] > 30 || pixels[i + 1] > 30 || pixels[i + 2] > 40)
                ++changed;
        Require(changed > 500, "overlay generated visible pixels");
        SaveBitmap(directory / L"observer-preview.bmp", pixels, g.width, g.height);
        Pass("projection, filtering, overlay pixels, host state and depth preservation");
        g.Resize(1000, 640);
        c.healthBar = HealthBar::Horizontal;
        c.fontPixels = 19.f;
        Check(p.configure(&c), "change bar orientation and font size");
        f = MakeFrame(1, g.width, g.height);
        Check(p.submit(&f), "resized frame");
        g.Clear();
        Check(g.chain->Present(0, 0), "resized Present");
        Check(p.statistics(&stats), "statistics");
        Require(stats.renderedFrames == 2 && stats.drawnEntities == 5, "render after resize and font rebuild");
        g.VerifyState();
        Pass("ResizeBuffers and runtime font/bar changes");
        c.enabled = 0;
        Check(p.configure(&c), "disable overlay");
        g.Clear();
        Check(g.chain->Present(0, 0), "disabled Present");
        Check(p.statistics(&stats), "statistics");
        Require(stats.renderedFrames == 2 && stats.drawnEntities == 0, "disabled overlay skips drawing");
        c.enabled = 1;
        c.staleFrameMilliseconds = 1;
        Check(p.configure(&c), "configure stale timeout");
        Check(p.submit(&f), "frame before stale wait");
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        g.Clear();
        Check(g.chain->Present(0, 0), "stale Present");
        Check(p.statistics(&stats), "statistics");
        Require(stats.renderedFrames == 2, "stale snapshot hidden");
        c.staleFrameMilliseconds = 1000;
        c.teamFilter = TeamFilter::TeammatesOnly;
        Check(p.configure(&c), "team filter");
        Check(p.submit(&f), "fresh frame");
        g.Clear();
        Check(g.chain->Present(0, 0), "teammate Present");
        Check(p.statistics(&stats), "statistics");
        Require(stats.drawnEntities == 3, "team filter keeps three teammates");
        Pass("disable, stale-frame suppression and team filtering");
        const auto withoutMenu = g.ReadPixels();
        SendMessageW(window, WM_KEYDOWN, VK_INSERT, 0);
        SendMessageW(window, WM_KEYUP, VK_INSERT, 0);
        settleMenu();
        for (int settle = 0; settle < 3; ++settle) {
            g.Clear();
            Check(g.chain->Present(0, 0), "menu Present");
        }
        const auto withMenu = g.ReadPixels();
        std::size_t menuPixels{};
        for (std::size_t i = 0; i < withMenu.size(); i += 4)
            if (withMenu[i] != withoutMenu[i] || withMenu[i + 1] != withoutMenu[i + 1] ||
                withMenu[i + 2] != withoutMenu[i + 2])
                ++menuPixels;
        Require(menuPixels > 10000, "Insert displays GUI");
        SaveBitmap(directory / L"observer-gui.bmp", withMenu, g.width, g.height);
        RECT guiClient{};
        GetClientRect(window, &guiClient);
        const auto mouseAt = [&](int x, int y, UINT message) {
            const auto position = MAKELPARAM(x * guiClient.right / static_cast<int>(g.width),
                                             y * guiClient.bottom / static_cast<int>(g.height));
            SendMessageW(window, WM_MOUSEMOVE, 0, position);
            if (message)
                SendMessageW(window, message, message == WM_LBUTTONDOWN ? MK_LBUTTON : 0, position);
            g.Clear();
            Check(g.chain->Present(0, 0), "mouse GUI Present");
        };
        const auto click = [&](int x, int y) {
            mouseAt(x, y, 0);
            mouseAt(x, y, WM_LBUTTONDOWN);
            mouseAt(x, y, WM_LBUTTONUP);
            mouseAt(x, y, 0);
        };
        using FindItem = BOOL(__cdecl *)(const char *, unsigned, float *);
        const auto findItem = reinterpret_cast<FindItem>(
            GetProcAddress(GetModuleHandleW(L"EntityAwarenessOverlay.dll"), "AwarenessGetMenuItemRect"));
        Require(findItem != nullptr, "menu geometry probe available");
        using ProfileBusy = BOOL(__cdecl *)();
        const auto profileBusy = reinterpret_cast<ProfileBusy>(
            GetProcAddress(GetModuleHandleW(L"EntityAwarenessOverlay.dll"), "AwarenessProfileIoBusy"));
        Require(profileBusy != nullptr, "profile I/O probe available");
        const auto settleProfile = [&] {
            const auto deadline = GetTickCount64() + 3000;
            while (profileBusy()) {
                Require(GetTickCount64() < deadline, "profile operation finishes without blocking Present");
                Sleep(1);
                g.Clear();
                Check(g.chain->Present(0, 0), "profile operation Present");
            }
        };
        const auto clickItem = [&](const char *label, unsigned occurrence = 0) {
            settleProfile();
            float rect[4]{};
            for (int attempt = 0; attempt < 25; ++attempt) {
                const auto found = findItem(label, occurrence, rect);
                if (found == 1) {
                    click(static_cast<int>((rect[0] + rect[2]) * .5f), static_cast<int>((rect[1] + rect[3]) * .5f));
                    settleProfile();
                    return;
                }
                if (!found)
                    break;
                mouseAt(500, 330, 0);
                const int direction = rect[1] < 160 ? 1 : -1;
                SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(direction * WHEEL_DELTA)), 0);
                g.Clear();
                Check(g.chain->Present(0, 0), "scroll to menu control");
            }
            SaveBitmap(directory / L"missing-menu-item.bmp", g.ReadPixels(), g.width, g.height);
            throw std::runtime_error(std::string("Menu item not visible: ") + label);
        };
        clickItem("Players");
        clickItem("General");
        clickItem("Enabled");
        Check(p.statistics(&stats), "statistics after checkbox");
        Require(stats.enabled == 0, "mouse checkbox disables overlay");
        Configuration fromGui;
        Check(p.configuration(&fromGui), "retrieve configuration changed by GUI");
        Require(fromGui.enabled == 0 && fromGui.teamFilter == TeamFilter::TeammatesOnly && fromGui.fontPixels == 19.f,
                "configuration getter preserves GUI changes and host fields");
        clickItem("Enabled");
        Check(p.statistics(&stats), "statistics after checkbox");
        Require(stats.enabled == 1, "mouse checkbox enables overlay");
        Check(p.configuration(&fromGui), "refresh configuration before host edit");
        fromGui.boxThickness = 2.5f;
        Check(p.configure(&fromGui), "host edits retrieved configuration");
        g.Clear();
        Check(g.chain->Present(0, 0), "GUI after host edit");
        Check(p.configuration(&fromGui), "read configuration after host edit");
        Require(fromGui.enabled == 1 && fromGui.boxThickness == 2.5f, "GUI retains a host configuration update");
        Check(setMenu(p, FALSE), "hide menu for style baseline");
        Check(p.submit(&f), "refresh style baseline");
        g.Clear();
        Check(g.chain->Present(0, 0), "style baseline Present");
        const auto beforeStyle = g.ReadPixels();
        Check(setMenu(p, TRUE), "show style controls");
        g.Clear();
        Check(g.chain->Present(0, 0), "style controls Present");
        clickItem("Models");
        clickItem("Enabled"); // Combined material toggle.
        EffectsConfiguration effectConfig;
        Check(p.getEffects(&effectConfig), "read Appearance toggles");
        Require(effectConfig.materialEnabled && effectConfig.glowEnabled,
                "one Appearance toggle enables the filled silhouette and its edge");
        SaveBitmap(directory / L"observer-appearance.bmp", g.ReadPixels(), g.width, g.height);
        const auto beforeFont = g.ReadPixels();
        Configuration alternateFont;
        Check(p.configuration(&alternateFont), "retrieve HUD font configuration");
        wchar_t fontWindows[MAX_PATH]{}, fontFile[MAX_PATH]{};
        GetWindowsDirectoryW(fontWindows, MAX_PATH);
        swprintf_s(fontFile, L"%s\\Fonts\\consola.ttf", fontWindows);
        WideCharToMultiByte(CP_UTF8, 0, fontFile, -1, alternateFont.fontPath, sizeof(alternateFont.fontPath), nullptr,
                            nullptr);
        Check(p.configure(&alternateFont), "change HUD font while Appearance remains open");
        g.Clear();
        Check(g.chain->Present(0, 0), "HUD font change Present");
        const auto afterFont = g.ReadPixels();
        std::size_t menuFontDifferences{};
        for (unsigned y = 171; y < 216; ++y)
            for (unsigned x = 60; x < 210; ++x) {
                const auto i = (y * g.width + x) * 4;
                if (beforeFont[i] != afterFont[i] || beforeFont[i + 1] != afterFont[i + 1] ||
                    beforeFont[i + 2] != afterFont[i + 2])
                    ++menuFontDifferences;
            }
        Require(menuFontDifferences == 0, "HUD font changes preserve the menu font, selected tab and layout");
        alternateFont.fontPath[0] = '\0';
        Check(p.configure(&alternateFont), "restore Segoe HUD font");
        Check(setMenu(p, FALSE), "hide menu to verify appearance changes");
        Check(p.submit(&f), "refresh styled frame");
        g.Clear();
        p.Models(f);
        Check(g.chain->Present(0, 0), "styled Present");
        const auto afterStyle = g.ReadPixels();
        std::size_t stylePixels{};
        for (std::size_t i = 0; i < afterStyle.size(); i += 4)
            if (afterStyle[i] != beforeStyle[i] || afterStyle[i + 1] != beforeStyle[i + 1] ||
                afterStyle[i + 2] != beforeStyle[i + 2])
                ++stylePixels;
        Require(stylePixels > 100, "appearance controls change rendered entity pixels");
        SaveBitmap(directory / L"observer-effects.bmp", afterStyle, g.width, g.height);
        g.VerifyState();
        g.VerifyDepth();
        EffectsState effectState;
        Check(p.effectsState(&effectState), "read effect renderer state");
        Require(effectState.status == EffectsStatus::Ready && effectState.boundsCount == 0 &&
                    effectState.meshCount == 3,
                "DLL renders filtered character models without bounds");
        g.Clear();
        Check(g.chain->Present(0, 0), "missing model Present");
        Check(p.effectsState(&effectState), "missing model status");
        Require(effectState.status == EffectsStatus::NoGeometry && !effectState.boundsCount,
                "missing model data never falls back to glowing bounds");
        Pass("single Glow / chams toggle draws model silhouettes and never substitutes bounding boxes");
        effectConfig.visibility = EffectVisibility::OccludedOnly;
        Check(p.setEffects(&effectConfig), "select occluded only");
        g.Clear();
        p.Models(f);
        Check(g.chain->Present(0, 0), "no-depth Present");
        Check(p.effectsState(&effectState), "no-depth effect status");
        Require(effectState.status == EffectsStatus::DepthUnavailable, "DLL reports missing scene depth");
        g.Clear();
        p.Models(f, g.depth.Get());
        Check(g.chain->Present(0, 0), "supplied-depth Present");
        Check(p.effectsState(&effectState), "supplied-depth status");
        Require(effectState.status == EffectsStatus::Ready && effectState.depthAvailable,
                "matching host depth enables occlusion pass");
        g.VerifyState();
        g.VerifyDepth();
        g.Clear();
        p.Models(f);
        Check(g.chain->Present(0, 0), "expire scene depth");
        Check(p.effectsState(&effectState), "expired-depth status");
        Require(effectState.status == EffectsStatus::DepthUnavailable, "scene depth expires after one Present");
        effectConfig.visibility = EffectVisibility::TwoColor;
        effectConfig.materialColor = {1, 1, 1, .85f};
        effectConfig.glowColor = {.65f, .15f, .95f, .85f};
        Check(p.setEffects(&effectConfig), "enable two-color model fill");
        EffectsConfiguration splitSettings;
        Check(p.getEffects(&splitSettings), "read two-color controls");
        Require(splitSettings.materialColor.g == 1 && splitSettings.glowColor.g == .15f,
                "two-color settings retain independent visible/hidden colors");
        g.Clear();
        p.Models(f, g.depth.Get());
        Check(g.chain->Present(0, 0), "two-color DLL Present");
        Check(p.effectsState(&effectState), "two-color DLL state");
        Require(effectState.status == EffectsStatus::Ready && effectState.depthAvailable,
                "two-color renderer is wired into the DLL Present path");
        g.VerifyState();
        g.VerifyDepth();
        SaveBitmap(directory / L"observer-two-color.bmp", g.ReadPixels(), g.width, g.height);
        Pass("independent visible/hidden colors, two-color DLL rendering and host state/depth preservation");
        effectConfig.visibility = EffectVisibility::AlwaysVisible;
        Check(p.setEffects(&effectConfig), "restore visibility");
        Check(setMenu(p, TRUE), "restore menu after style check");
        g.Clear();
        Check(g.chain->Present(0, 0), "restored menu Present");
        if (cycle == 0) {
            clickItem("Fill"); // Actual Fill combo.
            for (WPARAM key :
                 {static_cast<WPARAM>(VK_DOWN), static_cast<WPARAM>(VK_DOWN), static_cast<WPARAM>(VK_RETURN)}) {
                SendMessageW(window, WM_KEYDOWN, key, 0);
                g.Clear();
                Check(g.chain->Present(0, 0), "fill menu input");
                SendMessageW(window, WM_KEYUP, key, 0);
                g.Clear();
                Check(g.chain->Present(0, 0), "fill menu release");
            }
            Check(p.getEffects(&splitSettings), "read fill menu selection");
            for (int settle = 0; settle < 3; ++settle) {
                g.Clear();
                Check(g.chain->Present(0, 0), "settle fill panel height");
            }
            SaveBitmap(directory / L"two-color-controls.bmp", g.ReadPixels(), g.width, g.height);
            Require(splitSettings.visibility == EffectVisibility::TwoColor && splitSettings.glowColor.b == .95f,
                    "Fill combo selects two colors and initializes hidden purple");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Effects", L"visibility", 99, profile.path.c_str()) == 2,
                    "two-color mode saves from the actual GUI");
            Check(p.setEffects(&effectConfig), "restore single-color smoke settings");
            g.Clear();
            Check(g.chain->Present(0, 0), "restore fill menu");
            Pass("actual two-color Fill combo and profile serialization");
        }
        clickItem("Colors");
        SaveBitmap(directory / L"observer-colors.bmp", g.ReadPixels(), g.width, g.height);
        if (cycle == 0) {
            clickItem("Awareness");
            clickItem("Enabled");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"awarenessEnabled", 99, profile.path.c_str()) == 0,
                    "awareness toggle saves");
            clickItem("Enabled");
            clickItem("Load");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"awarenessEnabled", 99, profile.path.c_str()) == 0,
                    "GUI load restores awareness");
            clickItem("Enabled");
            clickItem("Save");
            clickItem("Trajectories");
            for (const char *label : {"Throw preview", "Flight trails", "Bullet tracers"})
                clickItem(label);
            clickItem("Save");
            for (const wchar_t *key : {L"grenadePrediction", L"grenadeTrails", L"bulletTracers"})
                Require(GetPrivateProfileIntW(L"Visual", key, 0, profile.path.c_str()) == 1,
                        "trajectory switches persist");
            clickItem("Bullets");
            SaveBitmap(directory / L"vortex-tracers.bmp", g.ReadPixels(), g.width, g.height);
            const auto initialShotGlow = GetPrivateProfileIntW(L"Visual", L"paths.shotGlow", 99, profile.path.c_str());
            const auto initialTrailGlow = GetPrivateProfileIntW(L"Visual", L"paths.trailGlow", 99, profile.path.c_str());
            const auto initialPreviewGlow = GetPrivateProfileIntW(L"Visual", L"paths.previewGlow", 99, profile.path.c_str());
            Require(initialShotGlow <= 1, "saved bullet glow is a valid toggle");
            clickItem("Glow");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"paths.shotGlow", 99, profile.path.c_str()) == 1 - initialShotGlow &&
                        GetPrivateProfileIntW(L"Visual", L"paths.trailGlow", 99, profile.path.c_str()) == initialTrailGlow &&
                        GetPrivateProfileIntW(L"Visual", L"paths.previewGlow", 99, profile.path.c_str()) == initialPreviewGlow,
                    "bullet glow toggles independently of utility glow");
            clickItem("Glow");
            clickItem("Assists");
            clickItem("Assisted Shoot");
            SaveBitmap(directory / L"vortex-assisted-shoot.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Enable Assisted Shoot");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"assists.shoot", 0, profile.path.c_str()) == 1,
                    "Assisted Shoot saves without native input in preview");
            clickItem("Enable Assisted Shoot");
            clickItem("Movement");
            SaveBitmap(directory / L"vortex-jumper.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Enable Jumper");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"assists.jumper", 0, profile.path.c_str()) == 1,
                    "Jumper saves independently");
            clickItem("Enable Jumper");
            SaveBitmap(directory / L"vortex-strafer.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Enable Strafer");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"assists.strafer", 0, profile.path.c_str()) == 1 &&
                        GetPrivateProfileIntW(L"Visual", L"assists.jumper", 99, profile.path.c_str()) == 0,
                    "Movement tab saves Strafer independently of Jumper");
            clickItem("Enable Strafer");
            clickItem("Server movement tuning");
            SaveBitmap(directory / L"vortex-strafer-tuning.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Camera");
            clickItem("Camera FOV");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"cameraFovEnabled", 0, profile.path.c_str()) == 1,
                    "camera control saves in aim page");
            clickItem("World");
            clickItem("Footsteps");
            SaveBitmap(directory / L"vortex-footsteps.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Show footsteps");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"worldVisuals.footsteps", 99, profile.path.c_str()) == 0,
                    "footstep switch saves");
            clickItem("Show footsteps");
            clickItem("Dropped weapons");
            SaveBitmap(directory / L"vortex-dropped.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Show dropped weapons");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"worldVisuals.dropped", 99, profile.path.c_str()) == 0,
                    "dropped switch saves");
            clickItem("Show dropped weapons");
            clickItem("Scene");
            SaveBitmap(directory / L"vortex-world.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Utility areas");
            SaveBitmap(directory / L"vortex-utility.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Filled footprints");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"combat.areaFill", 99, profile.path.c_str()) == 0,
                    "area fill control persists");
            clickItem("Filled footprints");
            clickItem("Player replay");
            clickItem("Show replay");
            SaveBitmap(directory / L"vortex-replay.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"combat.ghosts", 99, profile.path.c_str()) == 1,
                    "motion module saves");
            clickItem("Show replay");
            clickItem("Feedback");
            SaveBitmap(directory / L"vortex-hit-feedback.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Hitmarker");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"combat.hitMarker", 99, profile.path.c_str()) == 1,
                    "feedback module saves");
            clickItem("Hitmarker");
            clickItem("Settings");
            clickItem("Interface");
            SaveBitmap(directory / L"vortex-interface.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Session");
            clickItem("Keep Windows awake");
            clickItem("Auto-accept matches");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"keepAwake", 0, profile.path.c_str()) == 1 &&
                        GetPrivateProfileIntW(L"Visual", L"autoAccept", 0, profile.path.c_str()) == 1,
                    "session preferences save without activating game APIs in the demo");
            SaveBitmap(directory / L"vortex-session.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Keep Windows awake");
            clickItem("Auto-accept matches");
            clickItem("Diagnostics");
            SaveBitmap(directory / L"vortex-diagnostics.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Overview");
            SaveBitmap(directory / L"vortex-presets.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Apply profile");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"backgroundEnabled", 99, profile.path.c_str()) == 0,
                    "preset removes background");
            Require(GetPrivateProfileIntW(L"Visual", L"combat.recoil", 99, profile.path.c_str()) == 1,
                    "preset configures recoil");
            clickItem("Players");
            SaveBitmap(directory / L"vortex-signature.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Overview");
            clickItem("Undo last preset");
            Check(p.configure(&fromGui), "restore host settings after preset undo");
            clickItem("Save");
            Pass("reorganized module controls, save/load, full preset and undo");
            clickItem("Save");
            clickItem("Players");
            clickItem("Models");
            for (const auto *look : {"Silhouette", "Glass", "Neon"}) {
                clickItem(look);
                SaveBitmap(directory / (std::wstring(L"vortex-look-") + vortex::Wide(look) + L".bmp"), g.ReadPixels(),
                           g.width, g.height);
            }
            clickItem("Load");
            clickItem("Style");
            clickItem("Gradient fill");
            SaveBitmap(directory / L"vortex-player-style.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Load");
            clickItem("World");
            clickItem("Sky");
            clickItem("Midnight");
            SaveBitmap(directory / L"vortex-sky.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"sky.enabled", 0, profile.path.c_str()) == 1,
                    "sky preference saves without touching any native game in preview");
            clickItem("Original sky");
            clickItem("Save");
            clickItem("Scene");
            clickItem("Crisp");
            clickItem("Finishing");
            SaveBitmap(directory / L"vortex-scene-finishing.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Load");
            Configuration namedBaseline;
            Check(p.configuration(&namedBaseline), "named profile baseline");
            clickItem("Overview");
            clickItem("My profiles");
            clickItem("Profile name");
            const auto named = std::wstring(L"Review ") + std::to_wstring(GetCurrentProcessId());
            for (wchar_t letter : named)
                SendMessageW(window, WM_CHAR, letter, 0);
            g.Clear();
            Check(g.chain->Present(0, 0), "type profile name");
            clickItem("Create profile");
            const auto namedPath = vortex::DataDirectory() / L"profiles" / (named + L".ini");
            Require(GetPrivateProfileIntW(L"Overlay", L"enabled", 99, namedPath.c_str()) == namedBaseline.enabled,
                    "named profile created through actual UI");
            clickItem("Players");
            clickItem("General");
            clickItem("Enabled");
            clickItem("Overview");
            clickItem("My profiles");
            clickItem("Load profile");
            Check(p.configuration(&fromGui), "named profile loaded through UI");
            Require(fromGui.enabled == namedBaseline.enabled, "named snapshot restores settings independently");
            SaveBitmap(directory / L"vortex-profiles.bmp", g.ReadPixels(), g.width, g.height);
            Pass("named profile creation and loading through the real menu");
        }
        clickItem("Players");
        Pass("GUI checkbox, appearance pixels, tab navigation and host configuration getter");
        SendMessageW(window, WM_KEYDOWN, VK_HOME, 0);
        SendMessageW(window, WM_KEYUP, VK_HOME, 0);
        g.Clear();
        Check(g.chain->Present(0, 0), "Home disables entities");
        Check(p.statistics(&stats), "statistics");
        Require(stats.enabled == 0 && stats.drawnEntities == 0, "Home toggles entity overlay with menu open");
        SendMessageW(window, WM_KEYDOWN, VK_HOME, 1LL << 30);
        g.Clear();
        Check(g.chain->Present(0, 0), "repeated Home");
        Check(p.statistics(&stats), "statistics");
        Require(stats.enabled == 0, "held hotkey does not repeatedly toggle");
        SendMessageW(window, WM_KEYDOWN, VK_INSERT, 0);
        SendMessageW(window, WM_KEYUP, VK_INSERT, 0);
        settleMenu();
        g.Clear();
        Check(g.chain->Present(0, 0), "close menu while disabled");
        const auto hidden = g.ReadPixels();
        std::size_t bright{};
        for (std::size_t i = 0; i < hidden.size(); i += 4)
            if (hidden[i] > 30 || hidden[i + 1] > 30 || hidden[i + 2] > 40)
                ++bright;
        Require(bright == 0, "Insert hides GUI while entity overlay is disabled");
        SendMessageW(window, WM_KEYDOWN, VK_HOME, 0);
        SendMessageW(window, WM_KEYUP, VK_HOME, 0);
        Check(p.submit(&f), "refresh after GUI tests");
        g.Clear();
        Check(g.chain->Present(0, 0), "Home enables without menu");
        Check(p.statistics(&stats), "statistics");
        Require(stats.enabled == 1 && stats.drawnEntities == 3, "Home works with menu closed");
        g.VerifyState();
        g.VerifyDepth();
        Pass("Insert menu, Home toggle, autorepeat suppression, hidden GUI and restored host state");
        const auto withWeapons = g.ReadPixels();
        auto noWeapons = f;
        std::fill(std::begin(noWeapons.weaponDefinitionIndices), std::end(noWeapons.weaponDefinitionIndices), 0u);
        Check(p.submit(&noWeapons), "submit frame without active firearms");
        g.Clear();
        Check(g.chain->Present(0, 0), "weapon removal Present");
        const auto withoutWeapons = g.ReadPixels();
        std::size_t weaponPixels{};
        for (std::size_t i = 0; i < withWeapons.size(); i += 4)
            if (withWeapons[i] != withoutWeapons[i] || withWeapons[i + 1] != withoutWeapons[i + 1] ||
                withWeapons[i + 2] != withoutWeapons[i + 2])
                ++weaponPixels;
        Require(weaponPixels > 60, "clearing active firearms removes their silhouettes on the next Present");
        f.weaponDefinitionIndices[0] = 9;
        Check(p.submit(&f), "switch active firearm");
        g.Clear();
        Check(g.chain->Present(0, 0), "weapon switch Present");
        const auto switched = g.ReadPixels();
        std::size_t changedWeapon{};
        for (std::size_t i = 0; i < switched.size(); i += 4)
            if (switched[i] != withWeapons[i] || switched[i + 1] != withWeapons[i + 1] ||
                switched[i + 2] != withWeapons[i + 2])
                ++changedWeapon;
        Require(changedWeapon > 20, "switching firearm updates the silhouette on the next Present");
        Pass("active weapon icons appear, clear and switch on consecutive Presents");
        TrackingConfiguration tracking;
        tracking.enabled = 1;
        tracking.fovDegrees = 45;
        Check(p.setTracking(&tracking), "enable DLL camera tracking");
        auto invalidTracking = tracking;
        invalidTracking.fovDegrees = 200;
        Require(p.setTracking(&invalidTracking) == E_INVALIDARG, "DLL rejects invalid camera FOV");
        Check(setMenu(p, TRUE), "show camera controls");
        g.Clear();
        Check(g.chain->Present(0, 0), "camera controls Present");
        clickItem("Assists");
        clickItem("Tracking");
        SaveBitmap(directory / L"tracking-layout.bmp", g.ReadPixels(), g.width, g.height);
        if (cycle == 0) {
            clickItem("Rifles");
            clickItem("Copy Default");
            clickItem("Enable this profile");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"TrackingGroup.3", L"custom", 0, profile.path.c_str()) == 1 &&
                        GetPrivateProfileIntW(L"TrackingGroup.3", L"enabled", 1, profile.path.c_str()) == 0,
                    "weapon group switches persist");
            SaveBitmap(directory / L"vortex-weapon-profiles.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Customize this profile");
            clickItem("Default");
            clickItem("Latency");
            clickItem("Motion prediction");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"trackingProfiles.compensation", 0, profile.path.c_str()) == 1,
                    "prediction switch saves");
            SaveBitmap(directory / L"vortex-latency.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Motion prediction");
            clickItem("Tracking");
            clickItem("Set bind");
            SendMessageW(window, WM_KEYDOWN, VK_F7, 0);
            SendMessageW(window, WM_KEYUP, VK_F7, 0);
            g.Clear();
            Check(g.chain->Present(0, 0), "keyboard binding frame");
            TrackingConfiguration bound;
            Check(p.getTracking(&bound), "read captured key");
            Require(bound.hotkey == VK_F7, "GUI captures keyboard bind");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"CameraTracking", L"hotkey", 0, profile.path.c_str()) == VK_F7,
                    "captured key persists");
            clickItem("Set bind");
            SendMessageW(window, WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON1, XBUTTON1), 0);
            SendMessageW(window, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), 0);
            g.Clear();
            Check(g.chain->Present(0, 0), "mouse binding frame");
            Check(p.getTracking(&bound), "read mouse bind");
            Require(bound.hotkey == VK_XBUTTON1, "GUI captures mouse side button");
            clickItem("Load");
            Check(p.getTracking(&bound), "read restored bind");
            Require(bound.hotkey == VK_F7, "GUI load restores binding");
            clickItem("Recoil");
            SaveBitmap(directory / L"vortex-recoil.bmp", g.ReadPixels(), g.width, g.height);
            clickItem("Enabled");
            clickItem("Save");
            Require(GetPrivateProfileIntW(L"Visual", L"combat.recoil", 99, profile.path.c_str()) == 1,
                    "recoil module can be enabled");
            clickItem("Enabled");
            clickItem("Tracking");
        }
        Check(p.setTracking(&tracking), "restore camera test settings");
        SaveBitmap(directory / L"observer-camera.bmp", g.ReadPixels(), g.width, g.height);
        Check(setMenu(p, FALSE), "close camera menu");
        tracking.fovDegrees = 15;
        Check(p.setTracking(&tracking), "set visible angular boundary");
        c.enabled = 0;
        Check(p.configure(&c), "isolate tracking ring");
        Check(p.submit(&f), "fresh ring frame");
        effectConfig.fovCircle = 0;
        Check(p.setEffects(&effectConfig), "hide ring");
        g.Clear();
        Check(g.chain->Present(0, 0), "hidden ring Present");
        const auto noRing = g.ReadPixels();
        effectConfig.fovCircle = 1;
        Check(p.setEffects(&effectConfig), "show ring");
        g.Clear();
        Check(g.chain->Present(0, 0), "visible ring Present");
        const auto ringPixels = g.ReadPixels();
        std::size_t ringChanges{};
        for (std::size_t i = 0; i < ringPixels.size(); i += 4)
            if (ringPixels[i] != noRing[i] || ringPixels[i + 1] != noRing[i + 1] || ringPixels[i + 2] != noRing[i + 2])
                ++ringChanges;
        Require(ringChanges > 500, "FOV toggle draws a visible boundary independently of entity HUD");
        SaveBitmap(directory / L"observer-fov.bmp", ringPixels, g.width, g.height);
        Pass("projection-correct FOV circle toggles independently of HUD drawing");
        tracking.fovDegrees = 45;
        Check(p.setTracking(&tracking), "restore camera FOV");
        c.enabled = 1;
        Check(p.configure(&c), "restore player HUD");
        camera::Angles view{-3, 20};
        TrackingState trackingResult;
        for (int step = 0; step < 36; ++step) {
            auto cameraFrame = MakeFrame(0, g.width, g.height);
            awareness::demo::ApplyCamera(cameraFrame, view,
                                         {0, 0, static_cast<float>(g.width), static_cast<float>(g.height)});
            Check(p.submit(&cameraFrame), "submit DLL camera scene");
            CameraInput input;
            input.angles = view;
            input.activationHeld = step < 30;
            Check(p.cameraInput(&input), "submit host camera/hold input");
            const auto before = view;
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            g.Clear();
            Check(g.chain->Present(0, 0), "DLL tracking Present");
            Check(p.trackingState(&trackingResult), "read DLL tracking output");
            if (step < 30) {
                Require(trackingResult.active && trackingResult.targetId == 2, "DLL tracks the live opposing target");
                view = trackingResult.angles;
            } else
                Require(!trackingResult.active && view.yaw == before.yaw,
                        "DLL stops producing camera movement on release");
        }
        Require(std::abs(view.yaw - 20.f) > 10.f, "DLL smoothing changes host camera view");
        Check(p.submit(&f), "new frame without paired camera input");
        g.Clear();
        Check(g.chain->Present(0, 0), "unpaired camera Present");
        Check(p.trackingState(&trackingResult), "unpaired camera state");
        Require(!trackingResult.active && trackingResult.status == TrackingStatus::WaitingForCamera,
                "old camera input cannot drive a new snapshot");
        Check(setMenu(p, TRUE), "open menu pauses DLL camera");
        CameraInput heldCamera;
        heldCamera.activationHeld = 1;
        heldCamera.angles = view;
        Check(p.cameraInput(&heldCamera), "paired held input with menu open");
        g.Clear();
        Check(g.chain->Present(0, 0), "menu-paused camera Present");
        Check(p.trackingState(&trackingResult), "menu pause state");
        Require(!trackingResult.active && trackingResult.status == TrackingStatus::MenuOpen,
                "DLL menu pauses tracking");
        tracking.enabled = 0;
        Check(p.setTracking(&tracking), "disable DLL tracking");
        Check(setMenu(p, FALSE), "hide tracking menu before unload");
        Pass("DLL-owned camera tab, exports, target filtering, smoothing, release and stale-input/menu gates");
        // Exercise a real ResizeBuffers with the material/outline resources initialized.
        g.Resize(1280, 900);
        f = MakeFrame(0, g.width, g.height);
        Check(p.submit(&f), "effect frame after resize");
        effectConfig.materialEnabled = effectConfig.glowEnabled = 1;
        effectConfig.geometry = EffectGeometry::MeshOnly;
        Check(p.setEffects(&effectConfig), "select exact submitted meshes");
        const auto &subject = f.entities[0];
        const Vector3 triangle[]{subject.origin + Vector3{-.7f, 0, 0}, subject.origin + Vector3{0, 1.8f, 0},
                                 subject.origin + Vector3{.7f, 0, 0}};
        const EffectMesh mesh{subject.id, 0, 3};
        EffectsInput geometryInput;
        geometryInput.vertices = triangle;
        geometryInput.vertexCount = 3;
        geometryInput.meshes = &mesh;
        geometryInput.meshCount = 1;
        g.Clear();
        Check(p.effectsInput(&geometryInput), "submit exact world-space mesh to DLL");
        Check(g.chain->Present(0, 0), "mesh Present after resize");
        Check(p.effectsState(&effectState), "exact geometry status");
        Require(effectState.status == EffectsStatus::Ready && effectState.meshCount == 1 && !effectState.boundsCount,
                "DLL uses exact meshes without fabricating bounds");
        g.VerifyState();
        g.VerifyDepth();
        g.Clear();
        Check(g.chain->Present(0, 0), "expire exact mesh");
        Check(p.effectsState(&effectState), "expired geometry status");
        Require(effectState.status == EffectsStatus::NoGeometry, "exact meshes expire after one Present");
        effectConfig.geometry = EffectGeometry::BoundsFallback;
        Check(p.setEffects(&effectConfig), "restore bounds preview");
        Pass("mesh/depth submission, one-frame expiry, GPU resource resize and host-state preservation");
        if (cycle == 1) {
            // Capture the expanded dashboard using the same real Win32 pointer input as its users.
            Check(setMenu(p, TRUE), "open expanded dashboard");
            g.Clear();
            Check(g.chain->Present(0, 0), "dashboard resize baseline");
            mouseAt(972, 620, WM_LBUTTONDOWN);
            mouseAt(1190, 870, 0);
            mouseAt(1190, 870, WM_LBUTTONUP);
            mouseAt(1100, 60, 0);
            for (int settle = 0; settle < 4; ++settle) {
                g.Clear();
                Check(g.chain->Present(0, 0), "settle dashboard layout");
            }
            SaveBitmap(directory / L"observer-dashboard-camera.bmp", g.ReadPixels(), g.width, g.height);
            click(226, 94);
            SaveBitmap(directory / L"observer-dashboard-appearance.bmp", g.ReadPixels(), g.width, g.height);
            click(90, 123);
            SaveBitmap(directory / L"observer-dashboard.bmp", g.ReadPixels(), g.width, g.height);
            Check(setMenu(p, FALSE), "close expanded dashboard");
        }
        HRESULT wrongThread = S_OK;
        std::thread other([&] { wrongThread = p.shutdown(); });
        other.join();
        Require(wrongThread == RPC_E_WRONG_THREAD, "wrong-thread shutdown rejected");
        p.Unload();
        Require(GetWindowLongPtrW(window, GWLP_WNDPROC) == originalProcedure, "host window procedure restored");
        g.Clear();
        Check(g.chain->Present(0, 0), "Present after DLL unload");
        g.Resize(1280, 800);
        Pass("quiescent shutdown, FreeLibrary, continued Present and resize");
    }
    g.VerifyDebugLayer();
    logFile << "All smoke checks passed in two complete DLL load/unload cycles.\n";
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int show) {
    const std::wstring arguments(command);
    const bool cameraDemo = arguments.find(L"--camera") != std::wstring::npos;
    const bool cameraSmoke = arguments.find(L"--camera-smoke") != std::wstring::npos;
    const bool smoke = cameraSmoke || arguments.find(L"--smoke") != std::wstring::npos;
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    const auto directory = std::filesystem::path(executable).parent_path();
    logFile.open(vortex::LogDirectory() / (cameraSmoke ? L"camera-smoke-test.log"
                                           : smoke     ? L"smoke-test.log"
                                                       : L"demo.log"));
    HWND window{};
    try {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = L"AwarenessObserverDemo";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        Require(RegisterClassExW(&wc) != 0, "RegisterClassExW");
        RECT rect{0, 0, 1280, 800};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        window = CreateWindowExW(0, wc.lpszClassName,
                                 L"Entity Awareness | Insert: menu | Home: overlay | H: bars | T: teams | Esc: exit",
                                 WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                                 rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
        Require(window != nullptr, "CreateWindowExW");
        Graphics g;
        g.Initialize(window, smoke);
        if (cameraDemo) {
            SetWindowTextW(window, L"3D camera tracking | Hold right mouse over the scene");
            if (!smoke)
                ShowWindow(window, show);
            awareness::demo::RunCameraTracking(g, window, smoke, MakeFrame);
            if (smoke) {
                const auto pixels = g.ReadPixels();
                std::size_t visible{};
                for (std::size_t i = 0; i < pixels.size(); i += 4)
                    if (pixels[i] > 90 || pixels[i + 1] > 90 || pixels[i + 2] > 90)
                        ++visible;
                Require(visible > 1000, "camera GUI and scene produced visible pixels");
                SaveBitmap(directory / L"camera-tracking.bmp", pixels, g.width, g.height);
                Pass("camera tracking: opposing target selection, hold/release, angle smoothing, GUI and GPU readback");
            }
        } else if (smoke)
            Smoke(g, directory);
        else {
            Plugin p(directory);
            auto c = MakeConfiguration();
            Check(p.initialize(g.chain.Get(), nullptr), "AwarenessInitialize");
            ShowWindow(window, show);
            const auto started = std::chrono::steady_clock::now();
            camera::Angles hostView{-2.862405f, 0.f};
            bool running = true;
            while (running) {
                MSG msg{};
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT)
                        running = false;
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                if (!running)
                    break;
                if (IsIconic(window)) {
                    Sleep(25);
                    continue;
                }
                RECT client{};
                GetClientRect(window, &client);
                const UINT w = static_cast<UINT>(client.right), h = static_cast<UINT>(client.bottom);
                if (!w || !h)
                    continue;
                if (w != g.width || h != g.height)
                    g.Resize(w, h);
                if (healthKey || teamKey) {
                    // Preserve every setting changed in the DLL-owned GUI.
                    Check(p.configuration(&c), "read current GUI configuration");
                    if (healthKey)
                        c.healthBar = c.healthBar == HealthBar::Vertical ? HealthBar::Horizontal : HealthBar::Vertical;
                    if (teamKey)
                        c.teamFilter = static_cast<TeamFilter>((static_cast<unsigned>(c.teamFilter) + 1) % 3);
                    healthKey = teamKey = false;
                    Check(p.configure(&c), "update configuration");
                }
                const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - started).count();
                auto f = MakeFrame(seconds, g.width, g.height);
                awareness::demo::ApplyCamera(f, hostView,
                                             {0, 0, static_cast<float>(g.width), static_cast<float>(g.height)});
                Check(p.submit(&f), "submit simulation snapshot");
                TrackingConfiguration tracking;
                Check(p.getTracking(&tracking), "read camera controls");
                demoTrackingKey = tracking.hotkey;
                CameraInput input;
                input.angles = hostView;
                input.activationHeld =
                    GetForegroundWindow() == window &&
                    (tracking.hotkey < 256 ? (GetAsyncKeyState(tracking.hotkey) & 0x8000) != 0
                                           : demoWheelKey == tracking.hotkey && GetTickCount64() < demoWheelUntil);
                Check(p.cameraInput(&input), "submit demo camera");
                g.Clear();
                EffectsConfiguration activeEffects;
                Check(p.getEffects(&activeEffects), "read model highlight controls");
                if (activeEffects.materialEnabled || activeEffects.glowEnabled)
                    p.Models(f, g.depth.Get(), seconds);
                Check(g.chain->Present(1, 0), "Present");
                TrackingState state;
                Check(p.trackingState(&state), "retrieve DLL camera output");
                if (state.active)
                    hostView = state.angles;
            }
            p.Unload();
        }
        g.context->ClearState();
        if (IsWindow(window))
            DestroyWindow(window);
        UnregisterClassW(L"AwarenessObserverDemo", instance);
        return 0;
    } catch (const std::exception &e) {
        logFile << "FAIL: " << e.what() << std::endl;
        if (!smoke)
            MessageBoxA(window, e.what(), "Observer demo error", MB_OK | MB_ICONERROR);
        if (IsWindow(window))
            DestroyWindow(window);
        return 1;
    }
}
