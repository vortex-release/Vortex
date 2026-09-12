#include <awareness/OverlayApi.hpp>
#include <awareness/HudLayout.hpp>
#include "bootstrap.hpp"
#include "app_config.hpp"
#include "runtime_support.hpp"
#include "cached_source.hpp"
#include "cs2_model_fill.hpp"
#include "kill_sound.hpp"
#include "trajectory_native.hpp"
#include "world_native.hpp"
#include "viewmodel_native.hpp"
#include "game_frame.hpp"
#include "cosmetics_native.hpp"
#include "weather_native.hpp"
#include "scoreboard_native.hpp"
#include "native_model_mask.hpp"
#include "grenade_lineups_draw.hpp"
#include "combat_draw.hpp"
#include "world_visuals_draw.hpp"
#include "trajectory_draw.hpp"
#include "window_bridge.hpp"
#include "session_tools.hpp"
#include "overlay_panel.hpp"
#include "configuration.hpp"
#include "settings.hpp"
#include "profile_io.hpp"
#include "profile_browser.hpp"
#include "weapon_icons.hpp"
#include "camera_control.hpp"
#include "entity_effects.hpp"
#include "entity_filter.hpp"
#include "model_preview.hpp"
#include "menu_background.hpp"
#include "menu_motion.hpp"
#include "font_catalog.hpp"
#include "steam_profile.hpp"
#include "spectator_panel.hpp"
#include "session_badge.hpp"
#include "assist_runtime.hpp"
#include "player_decoration.hpp"
#include "scene_grade.hpp"
#include "scene_depth_capture.hpp"
#include "depth_resolve.hpp"
#include "trajectory_gpu.hpp"
#include "area_renderer.hpp"
#include <awareness/FovRing.hpp>
#include <awareness/AwarenessRing.hpp>
#include <thread>
#include <d3d11_1.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>

using Microsoft::WRL::ComPtr;
using namespace awareness;
namespace {
using Present = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *, UINT, UINT);
Present originalPresent{};

bool Readable(const void *pointer, SIZE_T length) noexcept {
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(pointer, &region, sizeof(region)) || region.State != MEM_COMMIT ||
        (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return false;
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
    return address >= base && length <= region.RegionSize && address - base <= region.RegionSize - length;
}
void *ResolvePresentEntry(void *entry) noexcept {
    // Follow existing unconditional API forwarding jumps. Some overlays temporarily
    // restore the public entry during Present; chain at their destination so both
    // hooks keep working. This is bounded instruction inspection, not a memory scan.
    void *visited[8]{};
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < i; ++j)
            if (visited[j] == entry)
                return nullptr;
        visited[i] = entry;
        if (!Readable(entry, 6))
            return nullptr;
        const auto *bytes = static_cast<const unsigned char *>(entry);
        if (bytes[0] == 0xE9) {
            std::int32_t relative{};
            std::memcpy(&relative, bytes + 1, sizeof(relative));
            entry = const_cast<unsigned char *>(bytes + 5 + relative);
        } else if (bytes[0] == 0xEB) {
            const auto relative = static_cast<std::int8_t>(bytes[1]);
            entry = const_cast<unsigned char *>(bytes + 2 + relative);
        } else if (bytes[0] == 0xFF && bytes[1] == 0x25) {
            std::int32_t relative{};
            std::memcpy(&relative, bytes + 2, sizeof(relative));
            const auto *slot = bytes + 6 + relative;
            if (!Readable(slot, sizeof(entry)))
                return nullptr;
            std::memcpy(&entry, slot, sizeof(entry));
        } else
            return entry;
    }
    return nullptr;
}

ImU32 Packed(Color c, float alpha) noexcept {
    return ImGui::ColorConvertFloat4ToU32({c.r, c.g, c.b, c.a * alpha});
}
ImVec2 Im(Vector2 v) noexcept {
    return {v.x, v.y};
}
void DrawFilledRect(ImDrawList *d, Vector2 min, Vector2 max, ImU32 color) {
    d->AddRectFilled(Im(min), Im(max), color);
}
void DrawLine(ImDrawList *d, Vector2 a, Vector2 b, ImU32 color, float width) {
    d->AddLine(Im(a), Im(b), color, width);
}
void DrawText(ImDrawList *d, ImFont *font, float size, Vector2 at, const char *text, ImU32 color, ImU32 shadow,
              bool centered = true) {
    const auto extent = font->CalcTextSizeA(size, FLT_MAX, 0.f, text);
    if (centered)
        at.x -= extent.x * .5f;
    at.x = std::floor(at.x);
    at.y = std::floor(at.y);
    d->AddText(font, size, {at.x + 1.f, at.y + 1.f}, shadow, text);
    d->AddText(font, size, Im(at), color, text);
}
Color HealthColor(float fraction, const Configuration &c) noexcept {
    return fraction <= c.lowHealthThreshold ? c.low : fraction <= c.mediumHealthThreshold ? c.medium : c.healthy;
}

class ImGuiScope {
    ImGuiContext *previous_;

  public:
    explicit ImGuiScope(ImGuiContext *context) : previous_(ImGui::GetCurrentContext()) {
        ImGui::SetCurrentContext(context);
    }
    ~ImGuiScope() { ImGui::SetCurrentContext(previous_); }
};

// D3D11.1 context state objects preserve all graphics/compute bindings, including
// tessellation, UAVs, multiple render targets, and resources that alias the back buffer.
class SceneScope {
    ID3D11DeviceContext1 *context_;
    ComPtr<ID3DDeviceContextState> previous_;

  public:
    SceneScope(ID3D11DeviceContext1 *context, ID3DDeviceContextState *overlay) : context_(context) {
        BeginScene(overlay);
    }
    void BeginScene(ID3DDeviceContextState *overlay) { context_->SwapDeviceContextState(overlay, &previous_); }
    void EndScene() {
        // Empty the overlay state before caching it: no back-buffer references may
        // survive Present, otherwise the host's next ResizeBuffers would fail.
        context_->ClearState();
        context_->SwapDeviceContextState(previous_.Get(), nullptr);
    }
    ~SceneScope() { EndScene(); }
};

class Renderer {
    ComPtr<ID3D11Device1> device_;
    ComPtr<ID3D11DeviceContext1> context_;
    ComPtr<ID3DDeviceContextState> overlayState_;
    ImGuiContext *imgui_{};
    bool backendReady_{};
    bool previewVisible_{true};
    ImFont *font_{};
    ImFont *uiFont_{};
    ImFont *customFont_{};
    std::array<ImFont *, FontPresets.size()> fonts_{};
    MenuMotion menuMotion_;
    SteamProfile steamProfile_;
    WeaponAtlas weapons_;
    ModelPreview modelPreview_;
    SceneGrade sceneGrade_;
    flight::DepthRenderer trajectories_;
    combat::AreaRenderer areas_;
    scene_depth::Resolver depthResolver_;
    HRESULT depthStart_{S_FALSE};
    ULONGLONG nextDepthAttempt_{}, nextDepthLog_{};
    EffectsState areaStatus_;
    flight::RenderStatus trajectoryStatus_;
    MenuBackground menuBackground_;
    EntityEffects effectsRenderer_;
    std::vector<EffectVertex> effectVertices_;
    char fontPath_[260]{};
    float uiScale_{};
    std::chrono::steady_clock::time_point previousTime_{std::chrono::steady_clock::now()};

    void DestroyImGui() noexcept {
        if (!imgui_)
            return;
        ImGuiScope scope(imgui_);
        if (backendReady_)
            ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext(imgui_);
        imgui_ = nullptr;
        backendReady_ = false;
        font_ = uiFont_ = customFont_ = nullptr;
        fonts_.fill(nullptr);
    }
    ImFont *PresetFont(std::size_t index) {
        if (index >= fonts_.size())
            index = 0;
        if (fonts_[index])
            return fonts_[index];
        const auto path = FontFile(index);
        if (!path.empty())
            fonts_[index] = ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 15.f);
        if (!fonts_[index])
            fonts_[index] = index ? PresetFont(0) : ImGui::GetIO().Fonts->AddFontDefault();
        return fonts_[index];
    }
    void RefreshFonts(const Configuration &c, const VisualOptions &visual) {
        auto &io = ImGui::GetIO();
        if (std::strcmp(fontPath_, c.fontPath) != 0) {
            if (customFont_)
                io.Fonts->RemoveFont(customFont_);
            customFont_ = nullptr;
            wchar_t path[520]{};
            if (c.fontPath[0] && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, c.fontPath, -1, path, 520)) {
                const auto attributes = GetFileAttributesW(path);
                if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                    customFont_ = io.Fonts->AddFontFromFileTTF(c.fontPath, 15.f);
            }
            std::memcpy(fontPath_, c.fontPath, sizeof(fontPath_));
        }
        uiFont_ = PresetFont(visual.menuFont);
        font_ = customFont_ ? customFont_ : PresetFont(visual.hudFont);
        io.FontDefault = uiFont_;
    }
    HRESULT CreateImGui(const Configuration &c) {
        DestroyImGui();
        ImGuiContext *previous = ImGui::GetCurrentContext();
        imgui_ = ImGui::CreateContext();
        uiScale_ = 0;
        ImGui::SetCurrentContext(previous);
        if (!imgui_)
            return E_OUTOFMEMORY;
        ImGuiScope scope(imgui_);
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.BackendPlatformName = "awareness_host_viewport";
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ConfigurePanelStyle();
        fontPath_[0] = 0;
        RefreshFonts(c, {});
        backendReady_ = ImGui_ImplDX11_Init(device_.Get(), context_.Get());
        std::memcpy(fontPath_, c.fontPath, sizeof(fontPath_));
        return backendReady_ ? S_OK : E_FAIL;
    }

  public:
    ~Renderer() { DestroyImGui(); }
    bool WantsPreview() const noexcept { return previewVisible_; }
    void Diagnostics(PanelInformation &info) const noexcept {
        info.trajectoryDepth = trajectoryStatus_.depthAvailable;
        info.trajectoryVertices = trajectoryStatus_.vertices;
        info.trajectoryDrawCalls = trajectoryStatus_.drawCalls;
        info.trajectoryResult = trajectoryStatus_.result;
        info.areaDepth = areaStatus_.status == EffectsStatus::Ready;
    }
    bool MenuAnimating() const noexcept { return menuMotion_.Visible(); }
    HRESULT Initialize(IDXGISwapChain *chain, const Configuration &config) {
        ComPtr<ID3D11Device> device;
        HRESULT hr = chain->GetDevice(IID_PPV_ARGS(&device));
        if (FAILED(hr))
            return hr;
        if (FAILED(hr = device.As(&device_)))
            return hr;
        ComPtr<ID3D11DeviceContext> immediate;
        device->GetImmediateContext(&immediate);
        if (FAILED(hr = immediate.As(&context_)))
            return hr;
        const D3D_FEATURE_LEVEL level = device->GetFeatureLevel();
        const UINT flags = (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)
                               ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                               : 0;
        hr = device_->CreateDeviceContextState(flags, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), nullptr,
                                               &overlayState_);
        if (FAILED(hr))
            return hr;
        if (FAILED(hr = weapons_.Initialize(device.Get())))
            return hr;
        if (FAILED(modelPreview_.Initialize(device.Get())))
            OverlayLog("Could not initialize the character preview");
        return CreateImGui(config);
    }
    HRESULT Render(IDXGISwapChain *chain, const FrameSnapshot &frame, Configuration &c, VisualOptions &visual,
                   std::uint32_t &drawn, WindowBridge &bridge, const PanelInformation &info, bool hasFreshFrame,
                   bool &edited, PanelActions &actions, TrackingConfiguration &tracking, EffectsConfiguration &effects,
                   const EffectGeometryData *geometry, ID3D11DepthStencilView *sceneDepth, bool reversedDepth,
                   EffectsState &effectsState, const PreviewPose *previewPose, const flight::Trails &trails,
                   const cs2::FlightSnapshot &flightData, const combat::WorldSnapshot &world,
                   const combat::ReplayFrame &ghosts, const worldvisuals::Drops &drops) {
        if (!backendReady_) {
            const HRESULT hr = CreateImGui(c);
            if (FAILED(hr))
                return hr;
        }
        // Only local references to the swap-chain image/view: resizing needs no hook.
        ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT hr = chain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr))
            return hr;
        D3D11_TEXTURE2D_DESC desc{};
        backBuffer->GetDesc(&desc);
        ComPtr<ID3D11RenderTargetView> target;
        if (FAILED(hr = device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &target)))
            return hr;
        const Viewport v = frame.viewport.width == 0.f
                               ? Viewport{0.f, 0.f, static_cast<float>(desc.Width), static_cast<float>(desc.Height)}
                               : frame.viewport;
        if (v.x < 0.f || v.y < 0.f || v.x + v.width > static_cast<float>(desc.Width) ||
            v.y + v.height > static_cast<float>(desc.Height))
            return E_INVALIDARG;
        const bool hiddenModels = info.cs2 && c.enabled && hasFreshFrame && effects.materialEnabled &&
                                  effects.visibility != EffectVisibility::AlwaysVisible;
        const bool needDepth =
            c.enabled && hasFreshFrame &&
            (visual.bulletTracers || visual.grenadeTrails || visual.grenadePrediction || hiddenModels ||
             visual.combat.ghosts || (visual.combat.areas && visual.combat.fireArea));
        if (needDepth && (depthStart_ == S_OK || GetTickCount64() >= nextDepthAttempt_)) {
            depthStart_ = scene_depth::Start(device_.Get(), context_.Get(), desc.Width, desc.Height);
            nextDepthAttempt_ = GetTickCount64() + 1000;
        }
        auto capturedDepth = needDepth && depthStart_ == S_OK ? scene_depth::BeginOverlay() : scene_depth::Snapshot{};
        // Declared before SceneScope so capture resumes only after host state restoration.
        struct ResumeDepth {
            bool started;
            ~ResumeDepth() {
                if (started)
                    scene_depth::EndOverlay();
                else
                    scene_depth::DiscardFrame();
            }
        } resumeDepth{needDepth && depthStart_ == S_OK};
        if (!sceneDepth) {
            sceneDepth = capturedDepth.view.Get();
            reversedDepth = capturedDepth.reversed;
        }
        SceneScope scene(context_.Get(), overlayState_.Get());
        ComPtr<ID3D11DepthStencilView> resolvedDepth;
        const auto depthResult =
            depthResolver_.Resolve(device_.Get(), context_.Get(), desc, sceneDepth, reversedDepth, resolvedDepth);
        sceneDepth = resolvedDepth.Get();
        if (info.cs2 && GetTickCount64() >= nextDepthLog_) {
            nextDepthLog_ = GetTickCount64() + 5000;
            char depthLog[256]{};
            std::snprintf(depthLog, sizeof(depthLog),
                          "Depth: ready=%d start=0x%08X resolve=0x%08X binds=%u clears=%u draws=%u copies=%u "
                          "reverse=%d target=%ux%u.",
                          sceneDepth != nullptr, static_cast<unsigned>(depthStart_), static_cast<unsigned>(depthResult),
                          capturedDepth.eligibleBindings, capturedDepth.depthClears, capturedDepth.qualifiedDraws,
                          capturedDepth.copies, reversedDepth, desc.Width, desc.Height);
            OverlayLog(depthLog);
        }
        ImGuiScope imgui(imgui_);
        RefreshFonts(c, visual);
        studio::Apply(visual);
        auto *targetPointer = target.Get();
        context_->OMSetRenderTargets(1, &targetPointer, nullptr);
        auto &io = ImGui::GetIO();
        io.DisplaySize = {static_cast<float>(desc.Width), static_cast<float>(desc.Height)};
        io.DisplayFramebufferScale = {1.f, 1.f};
        const auto now = std::chrono::steady_clock::now();
        io.DeltaTime = std::clamp(std::chrono::duration<float>(now - previousTime_).count(), .001f, .1f);
        previousTime_ = now;
        const float menuAlpha = menuMotion_.Update(bridge.Visible(), io.DeltaTime, visual.menuAnimations != 0);
        if ((c.enabled && visual.sessionBadge) || bridge.Visible() ||
            (visual.spectators && info.spectators && info.spectators->count))
            steamProfile_.Update(device_.Get());
        if (bridge.Visible()) {
            // The studio uses a clean solid surface; no background image decoding on Present.
            if (previewVisible_)
                modelPreview_.Render(context_.Get(), previewPose, io.DeltaTime, visual.previewRotate != 0,
                                     styling::Tint(effects, visual.playerStyle));
            context_->OMSetRenderTargets(1, &targetPointer, nullptr);
        }
        bridge.FeedInput(io, io.DisplaySize.x, io.DisplaySize.y);
        if (const auto captured = bridge.TakeBinding()) {
            tracking.hotkey = captured;
            edited = true;
        }
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        awareness::testing::BeginFrame();
        auto *spectatorDraw = c.enabled ? DrawSpectatorPanel(hasFreshFrame ? info.spectators : nullptr, visual,
                                                             steamProfile_, device_.Get(), bridge.Visible(), edited)
                                        : nullptr;
        if (menuAlpha > 0) {
            bool open = bridge.Visible();
            PanelMedia media{
                ImTextureRef(reinterpret_cast<ImTextureID>(menuBackground_.View())),
                ImTextureRef(reinterpret_cast<ImTextureID>(modelPreview_.View())),
                {static_cast<float>(menuBackground_.Width()), static_cast<float>(menuBackground_.Height())},
                menuBackground_.Status(),
                previewPose,
                ImTextureRef(reinterpret_cast<ImTextureID>(steamProfile_.Avatar())),
                steamProfile_.Name(),
                steamProfile_.Connected()};
            auto panelInfo = info;
            panelInfo.waitingForBind = bridge.WaitingForBind();
            edited |= DrawOverlayPanel(c, visual, panelInfo, open, actions, weapons_.Texture(), tracking, effects,
                                       media, bridge.Visible());
            previewVisible_ = actions.previewVisible;
            if (actions.beginBind)
                bridge.BeginBindCapture();
            if (actions.browse)
                menuBackground_.Browse();
            if (actions.reloadBackground)
                menuBackground_.Reload();
            if (actions.previewDrag != 0)
                modelPreview_.Drag(actions.previewDrag);
            if (!open)
                bridge.SetVisible(false);
        }
        auto *badgeDraw = c.enabled
                              ? DrawSessionBadge(visual, steamProfile_.Name(),
                                                 ImTextureRef(reinterpret_cast<ImTextureID>(steamProfile_.Avatar())),
                                                 steamProfile_.Avatar() != nullptr, hasFreshFrame ? info.pingMs : -1,
                                                 font_, menuAlpha > 0 ? actions.menuBounds : ImVec4{})
                              : nullptr;
        // An entity layer beneath the GUI in this DLL's own viewport, clipped to the
        // observer camera viewport. No extra OS swap chain or Present recursion.
        auto *d = ImGui::GetBackgroundDrawList();
        if (c.enabled && hasFreshFrame && info.lineupController && info.lineupCapture)
            lineups::Draw(*d, font_, info.lineupController->Records(), *info.lineupCapture, frame, v, c, visual.lineups,
                          FrameSeconds());
        d->Flags |= ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill;
        // ImGui owns and reuses these buffers; reserve once, retain capacity across frames.
        if (d->VtxBuffer.Capacity < 16384)
            d->VtxBuffer.reserve(16384);
        if (d->IdxBuffer.Capacity < 32768)
            d->IdxBuffer.reserve(32768);
        effectVertices_.clear();
        if (visual.statusHud && c.enabled) {
            char label[120]{};
            std::snprintf(label, sizeof(label), "%s  |  %u records  |  %.0f FPS",
                          hasFreshFrame ? "Connected" : "Waiting", info.pawns, info.fps);
            DrawText(d, font_, 14, {visual.sessionBadge ? 14.f : io.DisplaySize.x - 320, 12}, label,
                     IM_COL32(219, 241, 239, 255), IM_COL32(0, 0, 0, 255), false);
        }
        d->PushClipRect({v.x, v.y}, {v.x + v.width, v.y + v.height}, true);
        if (c.enabled && hasFreshFrame) {
            auto hudCombat = visual.combat;
            if (info.cs2)
                hudCombat.fireArea = 0; // Actual fire cells are drawn in the depth-tested GPU pass.
            combat::Draw(*d, font_, frame, v, c, hudCombat, world, flightData.feedback, ghosts, FrameSeconds());
        }
        if (c.enabled && hasFreshFrame)
            worldvisuals::Draw(*d, font_, weapons_.Texture(), frame, v, c, visual.worldVisuals, flightData.footsteps,
                               drops, FrameSeconds());
        const auto effective = awareness::tracking::Resolve(
            tracking, visual.trackingProfiles, info.cs2 ? info.trackingWeapon : awareness::tracking::LocalWeapon(frame),
            info.cs2);
        if (hasFreshFrame && effective.enabled && effects.fovCircle) {
            if (const auto ring = ProjectFovRing(frame.viewProjection, v, effective.fovDegrees)) {
                d->AddEllipse({ring->center.x + 1, ring->center.y + 1}, Im(ring->radius),
                              Packed({0, 0, 0, .25f}, effects.fovColor.a), 0, 192, effects.fovThickness + 1);
                d->AddEllipse(Im(ring->center), Im(ring->radius), Packed(effects.fovColor, 1), 0, 192,
                              effects.fovThickness);
            }
        }
        std::array<std::uint32_t, MaxEntities> order{};
        const auto count = (std::min)(frame.entityCount, MaxEntities);
        for (std::uint32_t i = 0; i < count; ++i)
            order[i] = i;
        std::array<float, MaxEntities> distances{};
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto delta = frame.entities[i].origin - frame.cameraOrigin;
            const auto value = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
            distances[i] = std::isfinite(value) ? value : 0.f;
        }
        std::sort(order.begin(), order.begin() + count,
                  [&](auto a, auto b) { return distances[a] == distances[b] ? a < b : distances[a] > distances[b]; });
        for (std::uint32_t i = 0; c.enabled && hasFreshFrame && i < count; ++i) {
            const auto &e = frame.entities[order[i]];
            ScreenBox box;
            const bool onScreen = CalculateBoundingBox(e.origin, e.mins, e.maxs, frame.viewProjection, v, box);
            auto arrowConfig = c;
            arrowConfig.teamFilter = TeamFilter::All;
            const float arrowAlpha = EntityOpacity(frame, e, arrowConfig);
            if (visual.awarenessEnabled && arrowAlpha > 0 &&
                AwarenessTeam(e.team, frame.localTeam, visual.awarenessTeams) &&
                (!visual.awarenessOffscreen || !onScreen)) {
                const auto direction = AwarenessDirection(frame.cameraOrigin, e.origin, frame.viewProjection,
                                                          info.cs2 ? Vector3{0, 0, 1} : Vector3{0, 1, 0});
                if (direction) {
                    const auto arrow = PlaceAwarenessArrow(*direction, v, visual.awarenessRadius, visual.awarenessSize);
                    if (arrow) {
                        const auto shadow = [](Vector2 p) { return ImVec2{p.x + 1, p.y + 1}; };
                        d->AddTriangleFilled(shadow(arrow->tip), shadow(arrow->left), shadow(arrow->right),
                                             Packed({0, 0, 0, .55f}, arrowAlpha * visual.awarenessColor.a));
                        d->AddTriangleFilled(Im(arrow->tip), Im(arrow->left), Im(arrow->right),
                                             Packed(visual.awarenessColor, arrowAlpha));
                    }
                }
            }
            const float alpha = EntityOpacity(frame, e, c);
            if (alpha <= 0)
                continue;
            const bool knownTeam = frame.localTeam != 0 && e.team != 0;
            const bool teammate = knownTeam && e.team == frame.localTeam;
            const float meters = Distance(frame.cameraOrigin, e.origin) / c.worldUnitsPerMeter;
            if (!info.cs2 && (effects.materialEnabled || effects.glowEnabled))
                AppendEffectGeometry(effectVertices_, e, alpha, geometry, effects.geometry, effectsState);

            if (!onScreen)
                continue;
            const float hp = HealthFraction(e.health, e.maxHealth);
            const auto healthColor = HealthColor(hp, c);
            const auto teamColor = !knownTeam ? c.neutral : teammate ? c.teammate : c.opponent;
            const auto color = Packed(c.colorBoxesByHealth ? healthColor : teamColor, alpha);
            const auto outline = Packed(c.outline, alpha);
            const float center = (box.min.x + box.max.x) * .5f;
            if (c.boxes && visual.fillBoxes)
                styling::Fill(*d, box, c.colorBoxesByHealth ? healthColor : teamColor, alpha * visual.fillOpacity,
                              visual.playerStyle);
            if (c.lines) {
                Vector2 at{center, box.max.y};
                Vector2 projected;
                if (WorldToScreen(e.origin, frame.viewProjection, v, projected))
                    at = projected;
                const float originY = visual.lineOrigin == 0   ? v.y
                                      : visual.lineOrigin == 1 ? v.y + v.height
                                                               : v.y + v.height * .5f;
                const Vector2 start{v.x + v.width * .5f, originY};
                DrawLine(d, start, at, outline, c.lineThickness + 2);
                DrawLine(d, start, at, color, c.lineThickness);
            }
            if (c.boxes)
                styling::Border(*d, box, c.colorBoxesByHealth ? healthColor : teamColor, c.outline, alpha,
                                c.boxThickness, visual.cornerBoxes != 0, visual.playerStyle);
            if (c.healthBars) {
                ScreenBox bar;
                if (c.healthBar == HealthBar::Vertical)
                    bar = {{box.min.x - c.barThickness - 5.f, box.min.y}, {box.min.x - 5.f, box.max.y}};
                else
                    bar = {{box.min.x, box.max.y + 5.f}, {box.max.x, box.max.y + 5.f + c.barThickness}};
                DrawFilledRect(d, {bar.min.x - 1.f, bar.min.y - 1.f}, {bar.max.x + 1.f, bar.max.y + 1.f}, outline);
                DrawFilledRect(d, bar.min, bar.max, Packed(c.barBackground, alpha));
                if (c.healthBar == HealthBar::Vertical)
                    bar.min.y = bar.max.y - (bar.max.y - bar.min.y) * hp;
                else
                    bar.max.x = bar.min.x + (bar.max.x - bar.min.x) * hp;
                DrawFilledRect(d, bar.min, bar.max, Packed(healthColor, alpha));
                if (visual.healthNumbers) {
                    char number[16]{};
                    std::snprintf(number, sizeof(number), "%.0f", e.health);
                    DrawText(d, font_, (std::max)(10.f, c.fontPixels - 3), {bar.min.x - 12, bar.min.y}, number,
                             Packed(c.text, alpha), outline, true);
                }
            }
            if (c.names && e.name[0])
                DrawText(d, font_, c.fontPixels, {center, (std::max)(v.y, box.min.y - c.fontPixels - 3.f)}, e.name,
                         Packed(c.text, alpha), outline, true);
            const bool hasWeapon = visual.weaponIcons && FindWeaponIcon(frame.weaponDefinitionIndices[order[i]]);
            const float afterBar = c.healthBars && c.healthBar == HealthBar::Horizontal ? c.barThickness + 7.f : 3.f;
            const auto footer = CalculateHudFooter(box, v, afterBar, c.fontPixels, c.distances != 0, hasWeapon);
            if (c.distances) {
                char text[48]{};
                std::snprintf(text, sizeof(text), "%.1f m", static_cast<double>(meters));
                DrawText(d, font_, c.fontPixels, {center, footer.distanceY}, text, Packed(c.text, alpha), outline,
                         true);
            }
            if (hasWeapon) {
                DrawWeaponIcon(d, weapons_.Texture(), frame.weaponDefinitionIndices[order[i]],
                               {footer.weaponX, footer.weaponY}, {footer.weaponWidth, footer.weaponHeight},
                               Packed(c.text, alpha));
            }
            ++drawn;
        }
        d->PopClipRect();
        ImGui::Render();
        if (menuAlpha < 1) {
            const auto *data = ImGui::GetDrawData();
            for (auto *list : data->CmdLists) {
                if (list == d || list == spectatorDraw || list == badgeDraw)
                    continue; // Keep HUD and projectile visibility independent of the menu animation.
                for (auto &vertex : list->VtxBuffer) {
                    const auto alpha = static_cast<ImU32>(((vertex.col >> IM_COL32_A_SHIFT) & 255) * menuAlpha);
                    vertex.col = (vertex.col & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
                }
            }
        }
        if (c.enabled && hasFreshFrame)
            sceneGrade_.Render(device_.Get(), context_.Get(), backBuffer.Get(), target.Get(), visual.combat);
        if (c.enabled && hasFreshFrame && info.cs2)
            areas_.Render(device_.Get(), context_.Get(), target.Get(), desc, sceneDepth, reversedDepth,
                          frame.viewProjection, v, world, visual.combat, c.opacity, areaStatus_);
        if (c.enabled && hasFreshFrame && visual.combat.ghosts)
            modelPreview_.RenderGhosts(context_.Get(), target.Get(), frame, v, c, visual.combat, ghosts, FrameSeconds(),
                                       sceneDepth, reversedDepth);
        effectsState.result = effectsRenderer_.Render(device_.Get(), context_.Get(), target.Get(), desc, sceneDepth,
                                                      reversedDepth, frame.viewProjection, v, effectVertices_,
                                                      styling::Tint(effects, visual.playerStyle), effectsState);
        if (info.cs2)
            native_mask::Render(device_.Get(), context_.Get(), target.Get(), desc, sceneDepth, reversedDepth,
                                hiddenModels, effectsState);
        if (c.enabled && hasFreshFrame)
            trajectories_.Render(device_.Get(), context_.Get(), target.Get(), sceneDepth, reversedDepth, v, trails,
                                 flightData.prediction, flightData.tracers, visual.grenadeTrails != 0,
                                 visual.grenadePrediction != 0, visual.bulletTracers != 0,
                                 static_cast<flight::Shots>(visual.tracerTeams), frame.localEntityId, frame.localTeam,
                                 frame.viewProjection, FrameSeconds(), c.opacity, visual.paths, trajectoryStatus_);
        context_->OMSetRenderTargets(1, &targetPointer, nullptr);
        // The DX11 backend configures source-alpha blending and disables depth
        // testing/writes. No extra full-screen depth texture or clear is needed.
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        return S_OK;
    }
};

struct Runtime {
    ComPtr<IDXGISwapChain> chain;
    std::atomic<IDXGISwapChain *> bound{};
    std::atomic<bool> binding{false};
    HWND targetWindow{}, window{};
    DWORD renderThread{};
    void *presentAddress{};
    bool automatic{}, useCs2{};
    std::mutex dataMutex;
    Configuration config{};
    VisualOptions visual{};
    flight::Trails trails;
    combat::ReplayFrame ghosts;
    // Runtime is heap-owned. Reuse these buffers instead of reserving large
    // temporaries on the application's Present thread on every frame.
    FrameSnapshot renderFrameScratch;
    cs2::FlightSnapshot flightScratch;
    combat::WorldSnapshot worldScratch;
    std::uint64_t hitSerial{}, combatReset{};
    double nextGhostRead{};
    std::uintptr_t flightGeneration{};
    std::uint64_t flightReset{};
    TrackingConfiguration tracking{};
    TrackingState trackingState{};
    EffectsConfiguration effects{};
    EffectsState effectsState{};
    std::unique_ptr<EffectGeometryData> effectGeometry;
    ComPtr<ID3D11DepthStencilView> effectDepth;
    bool effectReversed{};
    bool settingsDirty{};
    ULONGLONG saveAt{};
    profiles::WorkingFile profileIO;
    profiles::Browser profileBrowser;
    profiles::View profileView;
    DeferredLog diagnosticLog{OverlayLog};
    lineups::Controller lineupController;
    lineups::PanelState lineupPanel;
    lineups::Capture lineupCapture;
    std::uint64_t profileOperationRevision{};
    std::string profileOperationName;
    bool profileLibraryRequested{};
    const std::wstring workingProfilePath{SettingsPath()};
    CameraInput cameraInput{};
    bool hasCameraInput{};
    std::uint64_t frameRevision{}, cameraFrameRevision{};
    std::uint64_t configRevision{};
    float renderMs{}, readMs{}, totalMs{};
    float fps{60};
    std::chrono::steady_clock::time_point previousPresent{std::chrono::steady_clock::now()};
    char settingsMessage[160]{"Changes save automatically when Auto-save is enabled."};
    FrameSnapshot frame{};
    Statistics stats{};
    ULONGLONG submittedAt{};
    bool hasFrame{};
    std::unique_ptr<Renderer> renderer;
    WindowBridge bridge;
    assist::Runtime assistedInput;
    ~Runtime() {
        assistedInput.Stop();
        lineupController.Stop();
        profileIO.Stop();
    }
    session::Tools sessionTools;
    cs2::CachedSource source;
    sound::Player sounds;
    sound::Player hitSounds{sound::Backend::WaveOut};
    cs2::KillEvents kills;
    char previousStatus[160]{};

    void PollWorkingProfile() {
        profiles::WorkingFile::Result result;
        bool loaded{};
        while (profileIO.Poll(result)) {
            std::scoped_lock lock(dataMutex);
            if (result.action == profiles::WorkingFile::Action::Save) {
                if (result.revision == configRevision) {
                    settingsDirty = !result.success;
                    if (!result.success)
                        saveAt = GetTickCount64() + 2000;
                }
                std::snprintf(settingsMessage, sizeof(settingsMessage), "%s",
                              result.success ? (result.revision == configRevision
                                                    ? "Working profile saved."
                                                    : "Snapshot saved; newer changes are not saved yet.")
                                             : "Save failed. Your changes are still here; retry Save.");
            } else if (!result.success) {
                std::snprintf(settingsMessage, sizeof(settingsMessage),
                              "Could not load a valid profile. Current settings kept.");
            } else if (result.revision != configRevision) {
                std::snprintf(settingsMessage, sizeof(settingsMessage),
                              "Settings changed while loading. Current changes kept; load again.");
            } else {
                const auto units = config.worldUnitsPerMeter;
                config = result.state.config;
                config.worldUnitsPerMeter = units;
                visual = result.state.visual;
                tracking = result.state.tracking;
                effects = result.state.effects;
                ++configRevision;
                settingsDirty = result.action == profiles::WorkingFile::Action::Reset;
                saveAt = GetTickCount64() + 500;
                loaded = true;
                profileView.loadedName.clear();
                std::snprintf(settingsMessage, sizeof(settingsMessage), "%s",
                              settingsDirty ? "Defaults restored." : "Working profile loaded.");
            }
        }
        if (loaded) {
            trails.Clear();
            ghosts.Clear();
            kills.Reset();
            nextGhostRead = 0;
        }
    }
    void PollProfileLibrary() {
        profiles::Completion result;
        if (!profileBrowser.Poll(result))
            return;
        bool loaded{};
        {
            std::scoped_lock lock(dataMutex);
            if (result.loaded && result.success) {
                if (profileOperationRevision != configRevision) {
                    result.message = "Settings changed while loading. Your current changes were kept; load again.";
                } else {
                    const auto units = config.worldUnitsPerMeter;
                    config = result.state.config;
                    config.worldUnitsPerMeter = units;
                    visual = result.state.visual;
                    tracking = result.state.tracking;
                    effects = result.state.effects;
                    ++configRevision;
                    settingsDirty = true;
                    saveAt = GetTickCount64() + 500;
                    loaded = true;
                    profileView.loadedName = result.name;
                    std::snprintf(settingsMessage, sizeof(settingsMessage),
                                  "Named profile loaded. Save keeps these working settings.");
                }
            }
        }
        if (result.success && profileView.loadedName == profileOperationName) {
            if (result.operation == profiles::Operation::Rename)
                profileView.loadedName = result.name;
            if (result.operation == profiles::Operation::Archive)
                profileView.loadedName.clear();
        }
        if (result.success &&
            (result.operation == profiles::Operation::Create || result.operation == profiles::Operation::Import ||
             result.operation == profiles::Operation::Duplicate || result.operation == profiles::Operation::Rename))
            profileView.selectionRequest = result.name;
        if (result.catalogReady)
            profileView.entries = std::move(result.entries);
        profileView.message = std::move(result.message);
        if (loaded) {
            trails.Clear();
            ghosts.Clear();
            kills.Reset();
            nextGhostRead = 0;
        }
    }
    void StartProfileOperation(profiles::Request request) {
        if (profileIO.Busy() || profileBrowser.Busy()) {
            profileView.message = "Finish the current profile operation first.";
            return;
        }
        profiles::State state;
        {
            std::scoped_lock lock(dataMutex);
            state = {config, visual, tracking, effects};
            profileOperationRevision = configRevision;
        }
        profileOperationName = request.name;
        if (profileBrowser.Start(std::move(request), std::move(state)))
            profileView.message = "Working...";
        else
            profileView.message = "Could not start the profile operation. Your settings were kept.";
    }
    void SaveProfile(bool force = false) {
        profiles::WorkingFile::Job job;
        {
            std::scoped_lock lock(dataMutex);
            if (!force && (!settingsDirty || !visual.autoSave || GetTickCount64() < saveAt || profileIO.Busy() ||
                           profileBrowser.Busy()))
                return;
            job = {profiles::WorkingFile::Action::Save,
                   workingProfilePath,
                   {config, visual, tracking, effects},
                   configRevision};
        }
        const bool queued = profileIO.Submit(std::move(job));
        std::scoped_lock lock(dataMutex);
        std::snprintf(settingsMessage, sizeof(settingsMessage), "%s",
                      queued ? "Saving working profile..." : "Profile queue is busy. Changes kept; retry Save.");
    }
    void LoadProfile(bool defaults) {
        profiles::WorkingFile::Job job;
        {
            std::scoped_lock lock(dataMutex);
            job = {defaults ? profiles::WorkingFile::Action::Reset : profiles::WorkingFile::Action::Load,
                   defaults ? DefaultSettingsPath(workingProfilePath) : workingProfilePath,
                   {config, visual, tracking, effects},
                   configRevision};
        }
        const bool queued = profileIO.Submit(std::move(job));
        std::scoped_lock lock(dataMutex);
        std::snprintf(settingsMessage, sizeof(settingsMessage), "%s",
                      queued ? "Loading profile..." : "Profile queue is busy. Current settings kept.");
    }

    void RunTracking(const FrameSnapshot &f, const cs2::FlightSnapshot &flightData, bool fresh, float seconds,
                     std::uint64_t frameNumber) {
        TrackingConfiguration settings;
        combat::Options recoilSettings;
        awareness::tracking::Options prediction;
        camera::TargetTeams teams;
        CameraInput input;
        std::uint64_t revision{};
        bool hasInput{};
        {
            std::scoped_lock lock(dataMutex);
            settings = awareness::tracking::Resolve(
                tracking, visual.trackingProfiles,
                useCs2 ? source.Tracking().weapon : awareness::tracking::LocalWeapon(f), useCs2);
            prediction = visual.trackingProfiles;
            recoilSettings = visual.combat;
            teams = static_cast<camera::TargetTeams>(visual.trackingTeams);
            input = cameraInput;
            revision = configRevision;
            hasInput = hasCameraInput && cameraFrameRevision == frameNumber;
        }
        TrackingState next;
        next.frameNumber = frameNumber;
        next.result = S_FALSE;
        const auto finish = [&](TrackingStatus status) {
            next.status = status;
            std::scoped_lock lock(dataMutex);
            trackingState = next;
        };
        if (!settings.enabled || settings.interpolationSpeed == 0.f)
            return finish(TrackingStatus::Disabled);
        if (bridge.Visible())
            return finish(TrackingStatus::MenuOpen);
        if (!fresh || (useCs2 && !f.localTeam))
            return finish(TrackingStatus::WaitingForData);
        const bool held = useCs2 ? GetForegroundWindow() == window && bridge.BindingHeld(settings.hotkey)
                                 : hasInput && input.activationHeld != 0;
        if (!useCs2 && !hasInput)
            return finish(TrackingStatus::WaitingForCamera);
        if (!held || (useCs2 && bridge.AssistKeys().textInput))
            return finish(TrackingStatus::WaitingForHotkey);
        if (useCs2 && !source.TrackingWeaponCurrent())
            return finish(TrackingStatus::WaitingForData);
        cs2::NativeViewAngles native;
        camera::Angles angles = input.angles;
        if (useCs2) {
            if (!source.ReadCamera(native))
                return finish(TrackingStatus::WaitingForCamera);
            angles = cs2::TrackingAngles(native);
        }
        Vector3 trackingPunch{};
        if (useCs2 && recoilSettings.EnabledFor(flightData.recoil.weapon) && flightData.recoil.valid &&
            flightData.recoil.owner == source.Tracking().owner &&
            flightData.recoil.weaponHandle == source.Tracking().weaponHandle &&
            FrameSeconds() - flightData.sampledAt < .1 &&
            flightData.recoil.shots >= recoilSettings.Profile(flightData.recoil.weapon).startShot) {
            const auto &profile = recoilSettings.Profile(flightData.recoil.weapon);
            trackingPunch = {flightData.recoil.punch.x * profile.vertical,
                             flightData.recoil.punch.y * profile.horizontal, 0};
            angles = combat::ShotAngles(angles, trackingPunch);
        }
        const auto selected =
            TrackFrame(f, angles, settings, std::clamp(seconds, .0001f, .1f), true, useCs2, ActiveTargetBone(), teams,
                       useCs2 ? &source.Tracking() : nullptr, &prediction, FrameSeconds());
        if (!selected)
            return finish(TrackingStatus::NoTarget);
        angles = combat::CompensatedAngles(angles, trackingPunch);
        next.angles = angles;
        next.targetId = selected->id;
        // Serialize configuration changes with the final commit. Never overwrite newer mouse angles.
        std::scoped_lock lock(dataMutex);
        if (configRevision != revision || bridge.Visible() ||
            (useCs2 && (GetForegroundWindow() != window || !bridge.BindingHeld(settings.hotkey)))) {
            next.status = TrackingStatus::WaitingForHotkey;
            trackingState = next;
            return;
        }
        next.result = useCs2 ? source.ApplyCamera(native, angles) : S_OK;
        next.active = next.result == S_OK;
        next.status = next.active ? TrackingStatus::Following : TrackingStatus::WriteBlocked;
        trackingState = next;
    }

    HRESULT Bind(IDXGISwapChain *swapChain) {
        DXGI_SWAP_CHAIN_DESC desc{};
        HRESULT hr = swapChain->GetDesc(&desc);
        if (FAILED(hr) || !IsWindow(desc.OutputWindow))
            return E_INVALIDARG;
        auto next = std::make_unique<Renderer>();
        Configuration c;
        {
            std::scoped_lock lock(dataMutex);
            c = config;
        }
        if (FAILED(hr = next->Initialize(swapChain, c)))
            return hr;
        bridge.SetOverlayKeys(c.toggleKey, c.alternateToggleKey);
        bridge.SetTrackingKey(tracking.hotkey);
        if (useCs2)
            bridge.SetTick(&session::Tools::TickCallback, &sessionTools);
        if (FAILED(hr = bridge.Attach(desc.OutputWindow)))
            return hr;
        window = desc.OutputWindow;
        if (useCs2)
            assistedInput.Start(bridge);
        renderThread = GetCurrentThreadId();
        renderer = std::move(next);
        chain = swapChain;
        bound.store(swapChain, std::memory_order_release);
        OverlayLog("DirectX 11 Present captured. Insert opens the menu; Home toggles drawing.");
        return S_OK;
    }
    HRESULT RenderFrame(UINT flags) {
        const auto frameStart = std::chrono::steady_clock::now();
        TrackingState previousTracking;
        EffectsState previousEffects;
        {
            std::scoped_lock lock(dataMutex);
            ++stats.presentCalls;
            stats.drawnEntities = 0;
            previousTracking = trackingState;
            previousEffects = effectsState;
            effectsState = {};
            trackingState.active = 0;
            trackingState.result = S_FALSE;
            trackingState.status = tracking.enabled ? TrackingStatus::WaitingForData : TrackingStatus::Disabled;
        }
        if (flags & DXGI_PRESENT_TEST)
            return S_FALSE;
        if (GetCurrentThreadId() != renderThread)
            return RPC_E_WRONG_THREAD;
        PollWorkingProfile();
        PollProfileLibrary();
        g_ActiveTargetBone = static_cast<int>(visual.activeTargetBone);
        SaveProfile();
        std::unique_ptr<EffectGeometryData> geometry;
        ComPtr<ID3D11DepthStencilView> sceneDepth;
        bool reversedDepth{};
        EffectsConfiguration effectConfig;
        EffectsState nextEffects;
        if (bridge.TakeMenuToggle())
            bridge.SetVisible(!bridge.Visible());
        const bool toggle = bridge.TakeOverlayToggle();
        Configuration c;
        TrackingConfiguration t;
        auto &f = renderFrameScratch;
        bool hasData;
        ULONGLONG timestamp;
        std::uint64_t revision{};
        std::uint64_t frameNumber{};
        {
            std::scoped_lock lock(dataMutex);
            if (toggle) {
                config.enabled = config.enabled ? 0u : 1u;
                ++configRevision;
                settingsDirty = true;
                saveAt = GetTickCount64() + 500;
            }
            stats.enabled = config.enabled;
            c = config;
            f = frame;
            hasData = hasFrame;
            timestamp = submittedAt;
            t = tracking;
            frameNumber = frameRevision;
            revision = configRevision;
            effectConfig = effects;
            geometry = std::move(effectGeometry);
            sceneDepth = std::move(effectDepth);
            reversedDepth = effectReversed;
        }
        bridge.SetOverlayKeys(c.toggleKey, c.alternateToggleKey);
        bridge.SetTrackingKey(t.hotkey);
        sessionTools.Configure(useCs2 && visual.keepAwake, useCs2 && visual.autoAccept);
        sounds.Configure(visual.killSoundEnabled != 0, visual.killSoundPath, visual.killSoundVolume);
        if (IsIconic(window) ||
            (!bridge.Visible() && !renderer->MenuAnimating() && !c.enabled && !t.enabled && !visual.killSoundEnabled)) {
            kills.Reset();
            trails.Clear();
            cs2::PauseTrajectories();
            cs2::PauseWorldEffects();
            cs2::ConfigureViewmodel({}, false);
            cs2::ConfigureWeather({}, false);
            cs2::ConfigureScoreboard({}, false);
            cosmetics::Configure({});
            native_mask::Discard();
            ghosts.Clear();
            if (useCs2) {
                assist::Request pausedInput;
                pausedInput.options = visual.assists;
                pausedInput.window = window;
                pausedInput.deadline = GetTickCount64() + 100;
                assistedInput.Configure(pausedInput);
                cs2::PauseModelFill();
                source.ClearHighlight();
            }
            return S_FALSE;
        }
        PanelInformation info;
        const auto readStart = std::chrono::steady_clock::now();
        if (useCs2) {
            source.Configure(c, visual, effectConfig, bridge.Visible() && renderer->WantsPreview());
            hasData = source.Update(f, ActiveTargetBone());
            timestamp = source.SampledAt();
            const auto &status = source.GetStatus();
            info = {status.message,     true,
                    status.ready,       status.reads.controllers,
                    status.reads.pawns, status.reads.failedReads,
                    status.gameBuild,   status.expectedBuild};
            info.buildVerified = status.buildVerified;
            info.rawBuildValue = status.rawBuildValue;
            info.buildRva = status.buildRva;
            info.inactivePawns = status.reads.inactivePawns;
            info.bonePositions = status.reads.bonePositions;
            info.failedBoneReads = status.reads.failedBoneReads;
            if (std::strcmp(previousStatus, status.message) != 0) {
                OverlayLog(status.message);
                std::memcpy(previousStatus, status.message, sizeof(previousStatus));
            }
        } else {
            info.ready = hasData;
            info.message = hasData ? "Entity data connected" : "Waiting for the host to supply entity data";
            info.pawns = hasData ? f.entityCount : 0;
        }
        info.sessionStatus = session::StatusText(sessionTools.GetStatus());
        info.acceptedMatches = sessionTools.Accepted();
        info.keepAwakeActive = sessionTools.PowerActive();
        const bool fresh =
            hasData && (!c.staleFrameMilliseconds || GetTickCount64() - timestamp <= c.staleFrameMilliseconds);
        lineupController.Tick();
        info.modelDiagnostics = cs2::GetModelFillDiagnostics();
        const auto cosmeticsCatalog = useCs2 && (bridge.Visible() || renderer->MenuAnimating())
                                          ? cosmetics::CatalogRuntime().Snapshot()
                                          : nullptr;
        info.cosmeticsCatalog = cosmeticsCatalog.get();
        info.cosmeticsStatus = cosmetics::Name(cosmetics::Status().state);
        info.weatherStatus = weather::Label(cs2::ReadWeatherDiagnostics().status);
        info.scoreboardReady = cs2::GetScoreboardStatus().connected;
        info.nativeFramesReady = cs2::frame::Diagnostics().connected;
        info.nativeFrameFailures = cs2::frame::Diagnostics().failedCallbacks;
        info.lineupController = &lineupController;
        info.lineupPanel = &lineupPanel;
        info.lineupCapture = &lineupCapture;
        lineupCapture.valid = false;
        auto &flightData = flightScratch;
        ResetInPlace(flightData);
        if (useCs2) {
            cs2::ConfigureTrajectories(visual, fresh && c.enabled,
                                       !bridge.Visible() && !bridge.AssistKeys().textInput &&
                                           GetForegroundWindow() == window);
            cs2::RefreshTrajectoryInputs();
            cs2::CopyTrajectories(flightData, &diagnosticLog);
            cs2::ConfigureViewmodel(visual.cameraVisuals, fresh && c.enabled);
            const auto &capture = flightData.lineup;
            if (fresh && capture.valid && FrameSeconds() - capture.time <= .25) {
                lineupCapture.valid = true;
                lineupCapture.feet = capture.feet;
                lineupCapture.eye = capture.eye;
                lineupCapture.pitch = capture.angles.x;
                lineupCapture.yaw = capture.angles.y;
                lineupCapture.weapon = capture.weapon;
                lineupCapture.time = capture.time;
            }
            info.pathsConnected = flightData.hooked;
            info.tracerCallbacks = flightData.tracerCallbacks;
            info.acceptedTracers = flightData.acceptedTracers;
            info.recoilInput = flightData.recoil.valid;
            info.recoilWrites = flightData.recoilWrites;
            info.recoilResult = flightData.recoilResult;
            info.viewConnected = flightData.viewConnected;
            info.eventsConnected = flightData.eventsConnected;
            info.tracerConnected = flightData.tracerConnected;
            info.punchConnected = flightData.punchConnected;
            info.bulletConnected = flightData.bulletConnected;
            info.particleConnected = flightData.particleConnected;
            info.bulletCallbacks = flightData.bulletCallbacks;
            info.particleCallbacks = flightData.particleCallbacks;
            info.fireSamples = flightData.fireSamples;
            info.fireEvents = flightData.fireEvents;
            info.impactEvents = flightData.impactEvents;
            info.rejectedTracers = flightData.rejectedTracers;
            info.recoilReadFailures = flightData.recoilReadFailures;
            info.recoilWeapon = flightData.recoil.weapon;
            info.recoilShots = flightData.recoil.shots;
            info.recoilPunch = flightData.recoil.punch;
            auto projectionViewport = f.viewport;
            if (!Valid(projectionViewport)) {
                RECT area{};
                GetClientRect(window, &area);
                projectionViewport = {0, 0, float(area.right), float(area.bottom)};
            }
            for (std::size_t i = 0; i < flightData.tracers.Lines().count; ++i) {
                const auto &shot = flightData.tracers.Lines()[i];
                ImVec2 a, b;
                if (flight::ScreenLine(shot.start, shot.end, f.viewProjection, projectionViewport, a, b))
                    ++info.visibleTracers;
                else
                    ++info.clippedTracers;
            }
            cs2::ProjectileFrame projectiles;
            if (!fresh || !c.enabled || !visual.grenadeTrails || !source.ReadFlight(projectiles))
                trails.Clear();
            else {
                if (projectiles.generation != flightGeneration || flightData.resetSerial != flightReset)
                    trails.Clear();
                flightGeneration = projectiles.generation;
                flightReset = flightData.resetSerial;
                trails.Update({projectiles.values.data(), projectiles.count}, FrameSeconds());
            }
        }
        info.appVersion = vortex::AppVersion;
        info.trackingWeapon = useCs2 ? source.Tracking().weapon : awareness::tracking::LocalWeapon(f);
        if (useCs2 && fresh && visual.worldVisuals.footsteps)
            flightData.footsteps = worldvisuals::Footsteps::MergeLive(flightData.footsteps, source.Footsteps(),
                                                                      FrameSeconds(), visual.worldVisuals.footDuration);
        info.footstepEvents = flightData.footstepEvents + source.FootstepCount();
        info.droppedWeapons = useCs2 ? source.Dropped().count : 0;
        auto &world = worldScratch;
        world.Clear();
        if (useCs2 && fresh && c.enabled) {
            if (combatReset != flightData.resetSerial) {
                ghosts.Clear();
                nextGhostRead = 0;
                combatReset = flightData.resetSerial;
            }
            if (visual.combat.bombTimer || visual.combat.areas || visual.combat.utilityTimers)
                source.ReadWorld(flightData.gameTime, world, flightData.infernos);
            const auto now = FrameSeconds();
            if (visual.combat.areas && visual.combat.fireArea)
                combat::AppendInfernoEvents(flightData.infernos, now, world);
            if (visual.combat.areas && visual.combat.blastArea)
                for (std::size_t i = 0; i < flightData.blasts.count && world.areaCount < world.areas.size(); ++i) {
                    const auto &b = flightData.blasts[i];
                    const float age = static_cast<float>(now - b.time);
                    if (age >= 0 && age < .85f)
                        world.areas[world.areaCount++] = {
                            1, combat::AreaType::Blast, b.position, 90 + age * 100, 0, .85f - age, .85f};
                }
            if (!visual.combat.ghosts) {
                ghosts.Clear();
                nextGhostRead = 0;
            } else if (now >= nextGhostRead) {
                source.ReadGhosts(f, ghosts, now, visual.combat.ghostDuration);
                nextGhostRead = now + 1. / 64.;
            }
        } else {
            ghosts.Clear();
            flightData.feedback.Clear();
        }
        info.utilityEntities = world.discovered;
        info.fireEntities = world.fireEntities;
        info.burningCells = world.burningCells;
        info.fireReadFailures = world.fireReadFailures;
        info.areaCount = static_cast<std::uint32_t>(world.areaCount);
        static double nextWorldLog{};
        if (useCs2 && visual.combat.areas && FrameSeconds() >= nextWorldLog) {
            nextWorldLog = FrameSeconds() + 5;
            char message[256]{};
            std::snprintf(message, sizeof(message),
                          "World: fresh=%d utility=%u infernos=%u burning=%u readFailures=%u areas=%u enabled=%u "
                          "fire=%u events=%llu.",
                          fresh, world.discovered, world.fireEntities, world.burningCells, world.fireReadFailures,
                          info.areaCount, visual.combat.areas, visual.combat.fireArea, flightData.infernoEvents);
            OverlayLog(message);
        }
        info.skyCount = source.SkyCount();
        info.skyFailed = source.SkyFailures();
        info.assists = assistedInput.Status();
        info.pingMs = useCs2 && fresh ? source.Ping() : -1;
        info.spectators = useCs2 ? &source.Spectators() : nullptr;
        info.trackingState = previousTracking;
        info.effectsState = previousEffects;
        info.ageMs = hasData ? GetTickCount64() - timestamp : 0;
        readMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - readStart).count();
        info.settingsMessage = settingsMessage;
        info.profileIoBusy = profileIO.Busy() || profileBrowser.Busy();
        profileView.busy = info.profileIoBusy;
        info.profiles = &profileView;
        if (bridge.Visible() && !profileLibraryRequested && !info.profileIoBusy) {
            StartProfileOperation({profiles::Operation::Refresh});
            profileLibraryRequested = true;
            profileView.busy = info.profileIoBusy = profileBrowser.Busy();
        }
        renderer->Diagnostics(info);
        info.areaCells = world.burningCells;
        info.renderMs = renderMs;
        info.readMs = useCs2 ? source.ReadMilliseconds() : readMs;
        info.totalMs = totalMs;
        const auto presentTime = std::chrono::steady_clock::now();
        const float seconds = std::chrono::duration<float>(presentTime - previousPresent).count();
        previousPresent = presentTime;
        fps += (1.f / (std::max)(.001f, seconds) - fps) * (-std::expm1(-6.f * std::clamp(seconds, 0.f, .1f)));
        info.fps = fps;
        if (!fresh && !useCs2) {
            info.ready = false;
            info.message = hasData ? "Entity data is stale" : info.message;
        }
        const auto soundStatus = sounds.Status();
        info.soundStatus = soundStatus.c_str();
        const auto hitStatus = hitSounds.Status();
        info.hitSoundStatus = hitStatus.c_str();
        if (IsIconic(window) || (!bridge.Visible() && !renderer->MenuAnimating() &&
                                 ((!c.enabled && !t.enabled && !visual.killSoundEnabled) || !fresh))) {
            kills.Reset();
            trails.Clear();
            cs2::PauseTrajectories();
            cs2::PauseWorldEffects();
            cs2::ConfigureViewmodel({}, false);
            cs2::ConfigureWeather({}, false);
            cs2::ConfigureScoreboard({}, false);
            cosmetics::Configure({});
            native_mask::Discard();
            ghosts.Clear();
            if (useCs2) {
                assist::Request pausedInput;
                pausedInput.options = visual.assists;
                pausedInput.window = window;
                pausedInput.deadline = GetTickCount64() + 100;
                assistedInput.Configure(pausedInput);
                cs2::PauseModelFill();
                source.ClearHighlight();
            }
            return S_FALSE;
        }
        std::uint32_t drawn{};
        bool edited = false;
        std::string pickedSound;
        if (sounds.TakePickedPath(pickedSound)) {
            strncpy_s(visual.killSoundPath, pickedSound.c_str(), _TRUNCATE);
            edited = true;
        }
        if (hitSounds.TakePickedPath(pickedSound)) {
            strncpy_s(visual.combat.hitSoundPath, pickedSound.c_str(), _TRUNCATE);
            edited = true;
        }
        PanelActions actions;
        PreviewPose previewPose;
        if (useCs2 && fresh && bridge.Visible() && renderer->WantsPreview())
            source.ReadPreview(f, previewPose);
        const auto renderStart = std::chrono::steady_clock::now();
        const HRESULT hr =
            renderer->Render(chain.Get(), f, c, visual, drawn, bridge, info, fresh, edited, actions, t, effectConfig,
                             geometry.get(), sceneDepth.Get(), reversedDepth, nextEffects, &previewPose, trails,
                             flightData, world, ghosts, useCs2 ? source.Dropped() : worldvisuals::Drops{});
        renderMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - renderStart).count();
        if (actions.rescan)
            source.Rescan();
        sounds.Configure(visual.killSoundEnabled != 0, visual.killSoundPath, visual.killSoundVolume);
        if (useCs2)
            cs2::ConfigureTrajectories(visual, fresh && c.enabled && !actions.rescan,
                                       !bridge.Visible() && !bridge.AssistKeys().textInput &&
                                           GetForegroundWindow() == window);
        if (actions.rescan) {
            trails.Clear();
            ghosts.Clear();
            nextGhostRead = 0;
        }
        hitSounds.Configure(visual.combat.hitSound != 0, visual.combat.hitSoundPath, visual.combat.hitVolume);
        if (actions.browseHitSound)
            hitSounds.Browse();
        if (actions.testHitSound)
            hitSounds.Preview();
        unsigned hits{};
        const double soundNow = FrameSeconds();
        for (std::size_t i = 0; i < flightData.feedback.hits.count; ++i) {
            const auto &hit = flightData.feedback.hits[i];
            if (hit.serial > hitSerial && soundNow >= hit.time && soundNow - hit.time < .2)
                ++hits;
        }
        hitSerial = flightData.feedback.serial;
        if (fresh && c.enabled && !actions.rescan)
            hitSounds.Trigger(hits);
        cs2::ConfigureWorldEffects(visual.combat, world, visual.scene,
                                   useCs2 && fresh && c.enabled && SUCCEEDED(hr) && !actions.rescan);
        if (useCs2) {
            const bool nativeFresh = fresh && c.enabled && SUCCEEDED(hr) && !actions.rescan;
            cs2::ConfigureWeather(visual.weather, nativeFresh);
            cs2::ConfigureScoreboard(visual.scoreboard, nativeFresh);
            auto loadout = visual.cosmetics;
            if (!nativeFresh)
                loadout.enabled = 0;
            cosmetics::Configure(loadout);
        }
        if (actions.browseSound)
            sounds.Browse();
        if (actions.testSound)
            sounds.Preview();
        cs2::KillSample killSample;
        const bool killReady = useCs2 && fresh && !actions.rescan && source.ReadKills(killSample);
        if (const auto count = kills.Update(killSample, killReady, visual.killSoundEnabled != 0))
            sounds.Trigger(count);
        const auto hiddenStatus = nextEffects;
        auto nativeEffects = styling::Tint(effectConfig, visual.playerStyle);
        if (useCs2) {
            if (cs2::UpdateModelFill(f, c, nativeEffects,
                                     fresh && SUCCEEDED(hr) && !actions.rescan && source.GetStatus().buildVerified &&
                                         source.GetStatus().gameBuild == cs2::offsets::ExpectedBuild,
                                     nextEffects, visual.shadedFill != 0, &source.ModelTargets())) {
                if (nativeEffects.materialEnabled && nativeEffects.visibility != EffectVisibility::AlwaysVisible) {
                    nextEffects.depthAvailable = nextEffects.depthAvailable && hiddenStatus.depthAvailable;
                    nextEffects.meshCount += hiddenStatus.meshCount;
                    if (nextEffects.status != EffectsStatus::NoGeometry) {
                        nextEffects.status =
                            nextEffects.depthAvailable ? hiddenStatus.status : EffectsStatus::DepthUnavailable;
                        nextEffects.result = hiddenStatus.result;
                    }
                }
                const auto halo = NativeHalo(nativeEffects, visual, FrameSeconds());
                if (halo.materialEnabled && halo.materialColor.a > 0) {
                    EffectsState haloState;
                    source.UpdateHighlight(f, c, halo, fresh && SUCCEEDED(hr) && !actions.rescan, haloState);
                    // A glow failure does not switch off a valid two-color material pass.
                    if (FAILED(haloState.result))
                        nextEffects.result = haloState.result;
                } else if (!source.ClearHighlight())
                    nextEffects.result = E_ACCESSDENIED;
            } else {
                source.UpdateHighlight(f, c, nativeEffects, fresh && SUCCEEDED(hr) && !actions.rescan, nextEffects);
            }
        }
        {
            std::scoped_lock lock(dataMutex);
            if (edited && configRevision == revision) {
                config = c;
                tracking = t;
                effects = effectConfig;
                ++configRevision;
                settingsDirty = true;
                saveAt = GetTickCount64() + 500;
            }
            effectsState = nextEffects;
            stats.enabled = config.enabled;
            if (SUCCEEDED(hr) && fresh && c.enabled)
                ++stats.renderedFrames;
            stats.drawnEntities = drawn;
        }
        if (actions.save)
            SaveProfile(true);
        if (actions.load || actions.reset)
            LoadProfile(actions.reset);
        if (actions.profile.operation != profiles::Operation::None)
            StartProfileOperation(std::move(actions.profile));
        if (useCs2) {
            const auto resolved =
                awareness::tracking::Resolve(t, visual.trackingProfiles, source.Tracking().weapon, true);
            assist::Request request;
            request.options = visual.assists;
            request.addresses = source.AssistAddresses();
            request.window = window;
            request.deadline = GetTickCount64() + 100;
            request.trackingKey = resolved.hotkey;
            request.trackingEnabled = resolved.enabled != 0;
            request.recoilEnabled = visual.combat.EnabledFor(source.Tracking().weapon);
            request.enabled = c.enabled && fresh && SUCCEEDED(hr) && !actions.rescan && !bridge.Visible();
            assistedInput.Configure(request);
        }
        RunTracking(f, flightData, fresh && SUCCEEDED(hr) && !actions.rescan, seconds, frameNumber);

        totalMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
        return hr;
    }
};
std::atomic<Runtime *> runtime{};

HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain *chain, UINT interval, UINT flags) noexcept {
    auto *r = runtime.load(std::memory_order_acquire);
    if (r && r->automatic && !r->bound.load(std::memory_order_acquire) && !(flags & DXGI_PRESENT_TEST)) {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (SUCCEEDED(chain->GetDesc(&desc)) && desc.OutputWindow == r->targetWindow && !r->binding.exchange(true)) {
            try {
                if (FAILED(r->Bind(chain)))
                    OverlayLog("The game swap chain could not initialize the GUI renderer.");
            } catch (...) {
                OverlayLog("The GUI renderer could not initialize.");
            }
            r->binding = false;
        }
    }
    const bool selected = r && r->bound.load(std::memory_order_acquire) == chain;
    HRESULT renderResult = S_FALSE;
    struct CaptureFrameGuard {
        bool selected;
        const HRESULT &result;
        ~CaptureFrameGuard() {
            if (selected && result != S_OK)
                scene_depth::DiscardFrame();
        }
    } captureFrameGuard{selected, renderResult};
    if (selected) {
        try {
            renderResult = r->RenderFrame(flags);
        } catch (...) {
            renderResult = E_FAIL;
        }
    }
    const HRESULT result = originalPresent(chain, interval, flags);
    if (selected) {
        std::scoped_lock lock(r->dataMutex);
        r->stats.lastRenderResult = FAILED(result) ? result : renderResult;
    }
    return result;
}
HRESULT Install(std::unique_ptr<Runtime> next, void *address, IDXGISwapChain *knownChain) {
    if (runtime.load())
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    next->presentAddress = ResolvePresentEntry(address);
    if (!next->presentAddress)
        return E_UNEXPECTED;
    if (MH_CreateHook(next->presentAddress, reinterpret_cast<void *>(&PresentHook),
                      reinterpret_cast<void **>(&originalPresent)) != MH_OK)
        return E_FAIL;
    if (knownChain) {
        const HRESULT hr = next->Bind(knownChain);
        if (FAILED(hr)) {
            MH_RemoveHook(next->presentAddress);
            originalPresent = nullptr;
            return hr;
        }
    }
    auto *r = next.release();
    runtime.store(r, std::memory_order_release);
    if (MH_EnableHook(r->presentAddress) != MH_OK) {
        r->assistedInput.Stop();
        const HRESULT detached = r->bridge.Detach();
        if (SUCCEEDED(detached)) {
            MH_RemoveHook(r->presentAddress);
            runtime = nullptr;
            originalPresent = nullptr;
            delete r;
        }
        return E_FAIL;
    }
    return S_OK;
}
HRESULT ProbePresentAddress(void *&address) {
    const wchar_t *className = L"EntityAwarenessDx11Probe";
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = OverlayModule();
    wc.lpszClassName = className;
    const ATOM registered = RegisterClassExW(&wc);
    if (!registered && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    const auto window =
        CreateWindowExW(0, className, L"", WS_POPUP, 0, 0, 8, 8, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) {
        if (registered)
            UnregisterClassW(className, wc.hInstance);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = desc.BufferDesc.Height = 8;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> chain;
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1,
                                               D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context);
    if (FAILED(hr))
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION,
                                           &desc, &chain, &device, nullptr, &context);
    if (SUCCEEDED(hr))
        address = (*reinterpret_cast<void ***>(chain.Get()))[8];
    chain.Reset();
    context.Reset();
    device.Reset();
    DestroyWindow(window);
    if (registered)
        UnregisterClassW(className, wc.hInstance);
    return hr;
}
void SetDefaultFont(Configuration &config) {
    wchar_t windows[MAX_PATH]{}, font[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    swprintf_s(font, L"%s\\Fonts\\segoeui.ttf", windows);
    if (GetFileAttributesW(font) != INVALID_FILE_ATTRIBUTES)
        WideCharToMultiByte(CP_UTF8, 0, font, -1, config.fontPath, sizeof(config.fontPath), nullptr, nullptr);
}
HRESULT StartAutomatic(HWND window, const Configuration *config, bool cs2Mode) {
    DWORD process{};
    if (!IsWindow(window) || !GetWindowThreadProcessId(window, &process) || process != GetCurrentProcessId())
        return E_INVALIDARG;
    if (config && !ValidConfiguration(*config))
        return E_INVALIDARG;
    if (runtime.load())
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    auto r = std::make_unique<Runtime>();
    r->automatic = true;
    r->useCs2 = cs2Mode;
    r->targetWindow = window;
    if (config) {
        r->config = *config;
        r->visual.autoSave = 0;
    } else {
        SetDefaultFont(r->config);
        if (cs2Mode) {
            r->config.worldUnitsPerMeter = 39.3700787f;
            r->config.fadeStartMeters = 100.f;
            r->config.maxDistanceMeters = 200.f;
        }
        LoadStartupSettings(SettingsPath(), r->config, r->visual, &r->tracking, &r->effects);
    }
    r->stats.enabled = r->config.enabled;
    if (cs2Mode) {
        FrameSnapshot initial;
        r->source.Update(initial);
    }
    void *address{};
    HRESULT hr = ProbePresentAddress(address);
    if (FAILED(hr))
        return hr;
    const auto installed = Install(std::move(r), address, nullptr);
    if (SUCCEEDED(installed) && cs2Mode) {
        // Material creation must not prevent the independent event/input hooks starting.
        cs2::StartTrajectories();
        cs2::StartWorldEffects();
        cs2::StartViewmodel();
        cosmetics::Initialize(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")));
        cs2::StartWeather();
        cs2::StartScoreboard();
        constexpr cs2::frame::Callback nativeCallbacks[]{cosmetics::Tick, cs2::TickWeather, cs2::TickScoreboard};
        if (FAILED(cs2::frame::Start(nativeCallbacks)))
            OverlayLog("Native extensions unavailable: frame dispatcher validation failed.");
        cs2::StartModelFill();
    }
    return installed;
}
struct WindowSearch {
    HWND window{};
    long long area{};
};
BOOL CALLBACK FindGameWindow(HWND window, LPARAM param) {
    auto &best = *reinterpret_cast<WindowSearch *>(param);
    DWORD process{};
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER))
        return TRUE;
    RECT rect{};
    GetClientRect(window, &rect);
    const auto area = static_cast<long long>(rect.right) * rect.bottom;
    if (rect.right >= 300 && rect.bottom >= 200 && area > best.area) {
        best.window = window;
        best.area = area;
    }
    return TRUE;
}
} // namespace

void StartCs2Automatically() noexcept {
    try {
        OverlayLog("CS2 detected. Starting the combined overlay DLL.");
        if (std::wcsstr(GetCommandLineW(), L"-vulkan")) {
            OverlayLog("Vulkan launch mode detected. This build requires CS2 DirectX 11.");
            return;
        }
        const auto deadline = GetTickCount64() + 30000;
        while (!BootstrapStopRequested() && GetTickCount64() < deadline) {
            WindowSearch best;
            EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&best));
            if (best.window && GetModuleHandleW(L"client.dll")) {
                const HRESULT hr = StartAutomatic(best.window, nullptr, true);
                char message[160]{};
                std::snprintf(message, sizeof(message),
                              SUCCEEDED(hr) ? "Present hook installed. Waiting for the first game frame."
                                            : "Automatic startup failed (HRESULT 0x%08lX).",
                              static_cast<unsigned long>(hr));
                OverlayLog(message);
                return;
            }
            Sleep(250);
        }
        OverlayLog("Automatic startup stopped or timed out while waiting for the CS2 window.");
    } catch (...) {
        OverlayLog("Automatic startup encountered an initialization error.");
    }
}
HRESULT __cdecl AwarenessInitialize(IDXGISwapChain *swapChain, const Configuration *config) noexcept {
    try {
        if (!swapChain || (config && !ValidConfiguration(*config)))
            return E_INVALIDARG;
        HRESULT hr = EnsureHookLibrary();
        if (FAILED(hr))
            return hr;
        auto r = std::make_unique<Runtime>();
        if (config) {
            r->config = *config;
            r->visual.autoSave = 0;
        } else {
            SetDefaultFont(r->config);
            LoadStartupSettings(SettingsPath(), r->config, r->visual, &r->tracking, &r->effects);
        }
        r->stats.enabled = r->config.enabled;
        return Install(std::move(r), (*reinterpret_cast<void ***>(swapChain))[8], swapChain);
    } catch (...) {
        return E_FAIL;
    }
}
HRESULT __cdecl AwarenessStartAutomatic(HWND targetWindow, const Configuration *config) noexcept {
    try {
        const HRESULT hr = EnsureHookLibrary();
        return FAILED(hr) ? hr : StartAutomatic(targetWindow, config, false);
    } catch (...) {
        return E_FAIL;
    }
}
HRESULT __cdecl AwarenessSetMenuVisible(BOOL visible) noexcept {
    auto *r = runtime.load(std::memory_order_acquire);
    if (!r)
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    r->bridge.SetVisible(visible != FALSE);
    return S_OK;
}
HRESULT __cdecl AwarenessSubmitFrame(const FrameSnapshot *frame) noexcept {
    try {
        FrameSnapshot copy;
        if (!CopySubmittedFrame(frame, copy))
            return E_INVALIDARG;
        auto *r = runtime.load(std::memory_order_acquire);
        if (!r)
            return HRESULT_FROM_WIN32(ERROR_NOT_READY);
        std::scoped_lock lock(r->dataMutex);
        r->frame = copy;
        for (auto &entity : r->frame.entities)
            entity.name[sizeof(entity.name) - 1] = '\0';
        r->submittedAt = GetTickCount64();
        r->hasFrame = true;
        ++r->frameRevision;
        r->effectGeometry.reset();
        r->effectDepth.Reset();
        r->trackingState.active = 0;
        r->trackingState.result = S_FALSE;
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}
HRESULT __cdecl AwarenessSetConfiguration(const Configuration *config) noexcept {
    try {
        if (!config || !ValidConfiguration(*config))
            return E_INVALIDARG;
        auto *r = runtime.load(std::memory_order_acquire);
        if (!r)
            return HRESULT_FROM_WIN32(ERROR_NOT_READY);
        std::scoped_lock lock(r->dataMutex);
        r->config = *config;
        ++r->configRevision;
        r->settingsDirty = true;
        r->saveAt = GetTickCount64() + 500;
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}
HRESULT __cdecl AwarenessGetStatistics(Statistics *statistics) noexcept {
    try {
        if (!statistics || statistics->size != sizeof(*statistics) || statistics->version != ApiVersion)
            return E_INVALIDARG;
        auto *r = runtime.load(std::memory_order_acquire);
        if (!r)
            return HRESULT_FROM_WIN32(ERROR_NOT_READY);
        std::scoped_lock lock(r->dataMutex);
        *statistics = r->stats;
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}
HRESULT __cdecl AwarenessGetConfiguration(Configuration *configuration) noexcept {
    try {
        if (!configuration || configuration->size != sizeof(*configuration) || configuration->version != ApiVersion)
            return E_INVALIDARG;
        auto *r = runtime.load(std::memory_order_acquire);
        if (!r)
            return HRESULT_FROM_WIN32(ERROR_NOT_READY);
        std::scoped_lock lock(r->dataMutex);
        *configuration = r->config;
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}
HRESULT __cdecl AwarenessSetTrackingConfiguration(const TrackingConfiguration *config) noexcept {
    if (!config || !ValidTrackingConfiguration(*config))
        return E_INVALIDARG;
    auto *r = runtime.load(std::memory_order_acquire);
    if (!r)
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    std::scoped_lock lock(r->dataMutex);
    r->tracking = *config;
    ++r->configRevision;
    r->settingsDirty = true;
    r->saveAt = GetTickCount64() + 500;
    if (!config->enabled) {
        r->trackingState = {};
        r->trackingState.result = S_FALSE;
    }
    return S_OK;
}
HRESULT __cdecl AwarenessGetTrackingConfiguration(TrackingConfiguration *config) noexcept {
    if (!config || config->size != sizeof(*config) || config->version != TrackingApiVersion)
        return E_INVALIDARG;
    auto *r = runtime.load(std::memory_order_acquire);
    if (!r)
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    std::scoped_lock lock(r->dataMutex);
    *config = r->tracking;
    return S_OK;
}
HRESULT __cdecl AwarenessSubmitCameraInput(const CameraInput *input) noexcept {
    if (!input || input->size != sizeof(*input) || input->version != TrackingApiVersion ||
        !camera::Finite(input->angles) || input->activationHeld > 1)
        return E_INVALIDARG;
    auto *r = runtime.load(std::memory_order_acquire);
    if (!r)
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    if (r->useCs2)
        return E_ACCESSDENIED; // Never substitute host activation for the game's actual hold key.
    std::scoped_lock lock(r->dataMutex);
    if (!r->hasFrame)
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    r->cameraInput = *input;
    r->cameraInput.angles = camera::Normalize(input->angles);
    r->cameraFrameRevision = r->frameRevision;
    r->hasCameraInput = true;
    r->trackingState.active = 0;
    r->trackingState.result = S_FALSE;
    return S_OK;
}
HRESULT __cdecl AwarenessGetTrackingState(TrackingState *state) noexcept {
    if (!state || state->size != sizeof(*state) || state->version != TrackingApiVersion)
        return E_INVALIDARG;
    auto *r = runtime.load(std::memory_order_acquire);
    if (!r)
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    std::scoped_lock lock(r->dataMutex);
    *state = r->trackingState;
    return S_OK;
}
HRESULT __cdecl AwarenessSetEffectsConfiguration(const EffectsConfiguration *config) noexcept {
    if (!config || !ValidEffectsConfiguration(*config))
        return E_INVALIDARG;
    auto *r = runtime.load(std::memory_order_acquire);
    if (!r)
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    std::scoped_lock lock(r->dataMutex);
    r->effects = UnifiedHighlight(*config);
    ++r->configRevision;
    r->settingsDirty = true;
    r->saveAt = GetTickCount64() + 500;
    return S_OK;
}
HRESULT __cdecl AwarenessGetEffectsConfiguration(EffectsConfiguration *config) noexcept {
    if (!config || config->size != sizeof(*config) || config->version != EffectsApiVersion)
        return E_INVALIDARG;
    auto *r = runtime.load(std::memory_order_acquire);
    if (!r)
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    std::scoped_lock lock(r->dataMutex);
    *config = r->effects;
    return S_OK;
}
HRESULT __cdecl AwarenessGetEffectsState(EffectsState *state) noexcept {
    if (!state || state->size != sizeof(*state) || state->version != EffectsApiVersion)
        return E_INVALIDARG;
    auto *r = runtime.load(std::memory_order_acquire);
    if (!r)
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    std::scoped_lock lock(r->dataMutex);
    *state = r->effectsState;
    return S_OK;
}
HRESULT __cdecl AwarenessSubmitEffectsInput(const EffectsInput *input) noexcept {
    try {
        if (!input)
            return E_INVALIDARG;
        auto *r = runtime.load(std::memory_order_acquire);
        if (!r)
            return HRESULT_FROM_WIN32(ERROR_NOT_READY);
        if (GetCurrentThreadId() != r->renderThread)
            return RPC_E_WRONG_THREAD;
        auto geometry = std::make_unique<EffectGeometryData>();
        if (!CopyEffectGeometry(*input, *geometry))
            return E_INVALIDARG;
        std::scoped_lock lock(r->dataMutex);
        if (!r->useCs2 && !r->hasFrame)
            return HRESULT_FROM_WIN32(ERROR_NOT_READY);
        r->effectGeometry = std::move(geometry);
        r->effectDepth = input->sceneDepth;
        r->effectReversed = input->reversedDepth != 0;
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}
HRESULT __cdecl AwarenessShutdown() noexcept {
    try {
        RequestBootstrapStop();
        WaitForOverlayBootstrap();
        auto *r = runtime.load(std::memory_order_acquire);
        if (r) {
            if (r->bound.load() && GetCurrentThreadId() != r->renderThread)
                return RPC_E_WRONG_THREAD;
            if (r->useCs2) {
                const auto cosmeticsStopped = cosmetics::StopNative();
                if (FAILED(cosmeticsStopped))
                    return cosmeticsStopped;
                const auto weatherStopped = cs2::StopWeather();
                if (FAILED(weatherStopped))
                    return weatherStopped;
                const auto scoreboardStopped = cs2::StopScoreboard();
                if (FAILED(scoreboardStopped))
                    return scoreboardStopped;
                const auto framesStopped = cs2::frame::Stop();
                if (FAILED(framesStopped))
                    return framesStopped;
                cosmetics::Shutdown();
            }
            const auto depthStopped = scene_depth::Stop();
            if (FAILED(depthStopped))
                return depthStopped;
            const auto fillStopped = cs2::StopModelFill();
            if (FAILED(fillStopped))
                return fillStopped;
            const auto viewmodelStopped = cs2::StopViewmodel();
            if (FAILED(viewmodelStopped))
                return viewmodelStopped;
            const auto worldStopped = cs2::StopWorldEffects();
            if (FAILED(worldStopped))
                return worldStopped;
            const auto flightStopped = cs2::StopTrajectories();
            if (FAILED(flightStopped))
                return flightStopped;
            if (!r->source.ClearHighlight())
                return E_ACCESSDENIED;
            if (r->settingsDirty && r->visual.autoSave)
                r->SaveProfile(true);
            const auto disabled = MH_DisableHook(r->presentAddress);
            if (disabled != MH_OK && disabled != MH_ERROR_DISABLED)
                return E_FAIL;
            r->assistedInput.Stop();
            const HRESULT detached = r->bridge.Detach();
            if (FAILED(detached))
                return detached;
            if (MH_RemoveHook(r->presentAddress) != MH_OK)
                return E_FAIL;
            runtime = nullptr;
            originalPresent = nullptr;
            delete r;
        }
        return StopHookLibrary();
    } catch (...) {
        return E_FAIL;
    }
}

extern "C" __declspec(dllexport) BOOL __cdecl AwarenessGetMenuItemRect(const char *label, unsigned occurrence,
                                                                       float *rect) noexcept {
    return awareness::testing::Find(label, occurrence, rect);
}

extern "C" __declspec(dllexport) BOOL __cdecl AwarenessProfileIoBusy() noexcept {
    auto *r = runtime.load(std::memory_order_acquire);
    if (!r)
        return FALSE;
    if (GetCurrentThreadId() != r->renderThread)
        return TRUE;
    return r->profileIO.Busy() || r->profileBrowser.Busy() || r->lineupController.Busy();
}
