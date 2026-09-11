#pragma once
#include <awareness/CameraTrackingImGui.hpp>
#include <awareness/OverlayApi.hpp>
#include "window_bridge.hpp"
#include <imgui_impl_dx11.h>
#include <DirectXMath.h>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace awareness::demo {
inline void CameraCheck(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
// Only the demo owns this context; it never shares ImGui state or camera pointers with the DLL.
struct CameraGuiSession {
    ImGuiContext *previous{ImGui::GetCurrentContext()};
    ImGuiContext *context{ImGui::CreateContext()};
    WindowBridge input;
    bool rendererStarted{};
    ~CameraGuiSession() {
        ImGui::SetCurrentContext(context);
        input.Detach();
        if (rendererStarted)
            ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext(context);
        ImGui::SetCurrentContext(previous);
    }
    void Initialize(HWND window, ID3D11Device *device, ID3D11DeviceContext *deviceContext) {
        ImGui::SetCurrentContext(context);
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.BackendPlatformName = "ObserverDemo_WindowBridge";
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        wchar_t windows[MAX_PATH]{};
        if (GetWindowsDirectoryW(windows, MAX_PATH)) {
            const auto path = std::filesystem::path(windows) / L"Fonts" / L"segoeui.ttf";
            const auto utf8 = path.u8string();
            if (std::filesystem::exists(path))
                io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char *>(utf8.c_str()), 16.f);
        }
        ImGui::StyleColorsDark();
        auto &style = ImGui::GetStyle();
        style.WindowRounding = 10.f;
        style.FrameRounding = 5.f;
        style.WindowPadding = {16, 16};
        style.ItemSpacing = {10, 10};
        rendererStarted = ImGui_ImplDX11_Init(device, deviceContext);
        CameraCheck(rendererStarted, "camera demo DX11 backend initialization");
        CameraCheck(SUCCEEDED(input.Attach(window)), "camera demo window input attachment");
    }
};
inline void ApplyCamera(FrameSnapshot &frame, camera::Angles angles, Viewport viewport) {
    using namespace DirectX;
    const auto forward = camera::Forward(angles);
    const auto origin = frame.cameraOrigin;
    const auto view = XMMatrixLookToLH(XMVectorSet(origin.x, origin.y, origin.z, 1.f),
                                       XMVectorSet(forward.x, forward.y, forward.z, 0.f), XMVectorSet(0, 1, 0, 0));
    const auto projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, viewport.width / viewport.height, .1f, 160.f);
    XMFLOAT4X4 matrix;
    XMStoreFloat4x4(&matrix, XMMatrixTranspose(view * projection));
    std::memcpy(frame.viewProjection.m, &matrix, sizeof(matrix));
    frame.viewport = viewport;
}
inline void DrawCameraScene(const FrameSnapshot &frame, const std::optional<camera::Selection> &selected) {
    auto *draw = ImGui::GetBackgroundDrawList();
    const auto v = frame.viewport;
    draw->PushClipRect({v.x, v.y}, {v.x + v.width, v.y + v.height}, true);
    draw->AddRectFilled({v.x, v.y}, {v.x + v.width, v.y + v.height}, IM_COL32(16, 24, 38, 255), 8.f);
    const auto line = [&](Vector3 a, Vector3 b) {
        Vector2 start, end;
        if (WorldToScreen(a, frame.viewProjection, v, start) && WorldToScreen(b, frame.viewProjection, v, end))
            draw->AddLine({start.x, start.y}, {end.x, end.y}, IM_COL32(34, 48, 68, 255));
    };
    for (int x = -20; x <= 20; x += 4)
        line({static_cast<float>(x), 0, 0}, {static_cast<float>(x), 0, 80});
    for (int z = 0; z <= 80; z += 4)
        line({-20, 0, static_cast<float>(z)}, {20, 0, static_cast<float>(z)});
    for (std::uint32_t i = 0; i < frame.entityCount; ++i) {
        const auto &entity = frame.entities[i];
        if (!entity.valid || entity.id == frame.localEntityId)
            continue;
        ScreenBox box;
        if (!CalculateBoundingBox(entity.origin, entity.mins, entity.maxs, frame.viewProjection, v, box))
            continue;
        const bool inactive = entity.health <= 0 || entity.dormant;
        const bool tracked = selected && selected->id == entity.id;
        const ImU32 color = inactive                         ? IM_COL32(116, 126, 141, 180)
                            : tracked                        ? IM_COL32(255, 207, 93, 255)
                            : entity.team == frame.localTeam ? IM_COL32(104, 183, 255, 255)
                                                             : IM_COL32(245, 120, 112, 255);
        draw->AddRect({box.min.x, box.min.y}, {box.max.x, box.max.y}, color, 3.f, tracked ? 2.5f : 1.5f, 0);
        draw->AddText({box.min.x, box.min.y - 21.f}, color, entity.name);
    }
    const ImVec2 center{v.x + v.width * .5f, v.y + v.height * .5f};
    draw->AddLine({center.x - 8, center.y}, {center.x + 8, center.y}, IM_COL32(225, 233, 245, 255));
    draw->AddLine({center.x, center.y - 8}, {center.x, center.y + 8}, IM_COL32(225, 233, 245, 255));
    draw->AddText({v.x + 16, v.y + 16}, IM_COL32(180, 195, 214, 255), "3D camera preview / hold right mouse");
    draw->PopClipRect();
}
// Graphics is the existing demo's D3D11 host; makeFrame supplies its simulated entity array.
template <class Graphics, class FrameFactory>
void RunCameraTracking(Graphics &graphics, HWND window, bool smoke, FrameFactory makeFrame) {
    CameraGuiSession gui;
    gui.Initialize(window, graphics.device.Get(), graphics.context.Get());
    camera::Settings settings;
    camera::Angles angles{-3.f, 20.f};
    const auto started = std::chrono::steady_clock::now();
    auto previousTime = started;
    std::uint32_t frameNumber{};
    bool moved{}, released{}, selectedEnemy{};
    for (;;) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT)
                return;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        const auto now = std::chrono::steady_clock::now();
        const float delta =
            smoke ? 1.f / 60.f : std::clamp(std::chrono::duration<float>(now - previousTime).count(), .0001f, .1f);
        previousTime = now;
        if (IsIconic(window)) {
            Sleep(25);
            continue;
        }
        RECT client{};
        GetClientRect(window, &client);
        if (client.right <= 0 || client.bottom <= 0)
            return;
        const auto width = static_cast<UINT>(client.right), height = static_cast<UINT>(client.bottom);
        if (width != graphics.width || height != graphics.height)
            graphics.Resize(width, height);
        const float seconds = smoke ? 0.f : std::chrono::duration<float>(now - started).count();
        auto frame = makeFrame(seconds, width, height);
        std::array<camera::Target, MaxEntities> targets{};
        for (std::uint32_t i = 0; i < frame.entityCount; ++i) {
            const auto &entity = frame.entities[i];
            // Follow the center of the world-space player card, not its feet.
            const Vector3 center{(entity.mins.x + entity.maxs.x) * .5f, (entity.mins.y + entity.maxs.y) * .5f,
                                 (entity.mins.z + entity.maxs.z) * .5f};
            targets[i] = {entity.id,         entity.team,        entity.health, entity.origin + center,
                          entity.valid != 0, entity.dormant != 0};
        }
        auto &io = ImGui::GetIO();
        io.DisplaySize = {static_cast<float>(width), static_cast<float>(height)};
        io.DeltaTime = delta;
        gui.input.FeedInput(io, io.DisplaySize.x, io.DisplaySize.y);
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({16, 16}, ImGuiCond_Once);
        ImGui::SetNextWindowSize({350, 460}, ImGuiCond_Once);
        if (ImGui::Begin("Camera controls", nullptr, ImGuiWindowFlags_NoCollapse)) {
            if (ImGui::BeginTabBar("Camera tabs")) {
                camera::DrawTrackingTab(settings);
                ImGui::EndTabBar();
            }
            ImGui::Separator();
            ImGui::Text("Pitch %.2f deg / yaw %.2f deg", angles.pitch, angles.yaw);
            if (ImGui::Button("Reset camera"))
                angles = {-3.f, 20.f};
            ImGui::TextWrapped("Blue: teammate. Red: opponent. Gold: tracking. Gray: dead or dormant.");
        }
        ImGui::End();
        const auto before = angles;
        // Smoke uses explicit synthetic activation; interactive mode reads the actual hold key.
        const bool held = smoke ? frameNumber >= 2 && frameNumber < 32 : camera::ActivationHeld(window, VK_RBUTTON);
        const auto selection = camera::Update(frame.cameraOrigin, angles,
                                              std::span<const camera::Target>(targets.data(), frame.entityCount),
                                              frame.localEntityId, frame.localTeam, settings, delta, held);
        if (smoke) {
            if (!held)
                CameraCheck(before.pitch == angles.pitch && before.yaw == angles.yaw && !selection,
                            "camera must freeze without hold input");
            if (held) {
                CameraCheck(selection && selection->id == 2, "camera must select the live opposing demo target");
                selectedEnemy = true;
            }
            moved |= std::abs(angles.yaw - 20.f) > 10.f;
            released |= frameNumber >= 32 && before.yaw == angles.yaw;
        }
        const float left = width >= 800 ? 382.f : 16.f;
        const Viewport viewport{left, 16.f, (std::max)(1.f, static_cast<float>(width) - left - 16.f),
                                (std::max)(1.f, static_cast<float>(height) - 32.f)};
        ApplyCamera(frame, angles, viewport);
        DrawCameraScene(frame, selection);
        ImGui::Render();
        graphics.Clear();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        CameraCheck(SUCCEEDED(graphics.chain->Present(smoke ? 0 : 1, 0)), "camera demo Present");
        if (smoke)
            graphics.VerifyState();
        if (smoke && ++frameNumber == 36) {
            CameraCheck(moved && released && selectedEnemy, "camera demo hold / track / release lifecycle");
            CameraCheck(ImGui::GetDrawData()->TotalVtxCount > 100, "camera tab and projected scene generated geometry");
            graphics.VerifyDebugLayer();
            return;
        }
    }
}
} // namespace awareness::demo
