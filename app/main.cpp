#include "../src/ui_icons.hpp"
#include "app_paths.hpp"
#include "capture.hpp"
#include "app_config.hpp"
#include "loader.hpp"
#include "updater.hpp"
#include "window_frame.hpp"
#include "../src/branding.hpp"
#include "../src/font_catalog.hpp"
#include "../src/menu_motion.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <shellapi.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>
using Microsoft::WRL::ComPtr;
using namespace vortex;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace {
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
ComPtr<IDXGISwapChain> swapchain;
ComPtr<ID3D11RenderTargetView> target;
bool closeRequested{}, resizePending{};
UINT nextWidth{}, nextHeight{};
float uiScale = 1.f;
ImFont *regularFont{}, *semiboldFont{};
std::array<ImFont *, awareness::FontPresets.size()> launcherFonts{};
bool animations{true};
std::uint32_t launcherFont{};
void ApplyLauncherFont() {
    auto *atlas = ImGui::GetIO().Fonts;
    const auto load = [&](std::size_t i) {
        if (!launcherFonts[i]) {
            const auto path = awareness::FontFile(i);
            if (!path.empty())
                launcherFonts[i] = atlas->AddFontFromFileTTF(path.c_str(), 16);
            if (!launcherFonts[i])
                launcherFonts[i] = launcherFonts[0] ? launcherFonts[0] : atlas->AddFontDefault();
        }
        return launcherFonts[i];
    };
    load(0);
    regularFont = load(launcherFont);
    semiboldFont = launcherFont == 0 ? load(8) : regularFont;
    ImGui::GetIO().FontDefault = regularFont;
}
float Animate(ImGuiID id, float goal, float initial = 0) {
    auto *state = ImGui::GetStateStorage();
    const float value = awareness::MotionStep(state->GetFloat(id, initial), goal, ImGui::GetIO().DeltaTime, animations);
    state->SetFloat(id, value);
    return value;
}
void ItemInteraction() {
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible)
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                            ImGui::GetColorU32(ImGuiCol_NavCursor), 7 * uiScale, 1.2f * uiScale);
}
void CheckHr(HRESULT hr, const char *message) {
    if (FAILED(hr))
        throw std::runtime_error(message);
}
void CreateTarget() {
    ComPtr<ID3D11Texture2D> buffer;
    CheckHr(swapchain->GetBuffer(0, IID_PPV_ARGS(&buffer)), "Cannot access the window surface.");
    CheckHr(device->CreateRenderTargetView(buffer.Get(), nullptr, &target), "Cannot create the window surface.");
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
        return 1;
    switch (message) {
    case WM_NCCALCSIZE:
        if (wparam) {
            if (IsZoomed(window)) {
                MONITORINFO monitor{sizeof(monitor)};
                if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
                    reinterpret_cast<NCCALCSIZE_PARAMS *>(lparam)->rgrc[0] = monitor.rcWork;
            }
            return 0;
        }
        break;
    case WM_NCHITTEST:
        return FrameHitTest(window, lparam);
    case WM_CLOSE:
        closeRequested = true;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            nextWidth = LOWORD(lparam);
            nextHeight = HIWORD(lparam);
            resizePending = true;
        }
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO *>(lparam)->ptMinTrackSize = {MulDiv(800, GetDpiForWindow(window), 96),
                                                                  MulDiv(600, GetDpiForWindow(window), 96)};
        return 0;
    case WM_DPICHANGED: {
        auto *rect = reinterpret_cast<RECT *>(lparam);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
void InitializeGraphics(HWND window) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL feature{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                               D3D11_SDK_VERSION, &desc, &swapchain, &device, &feature, &context);
    if (FAILED(hr))
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                           &desc, &swapchain, &device, &feature, &context);
    CheckHr(hr, "Vortex could not initialize Direct3D 11.");
    CreateTarget();
}
const ImVec4 accent{.67f, .57f, .94f, 1}, muted{.51f, .52f, .58f, 1};
void Theme() {
    auto &s = ImGui::GetStyle();
    s = ImGuiStyle{};
    ImGui::StyleColorsDark();
    s.WindowPadding = {0, 0};
    s.FramePadding = {14, 10};
    s.ItemSpacing = {10, 12};
    s.WindowRounding = 0;
    s.ChildRounding = 12;
    s.FrameRounding = 7;
    s.PopupRounding = 8;
    s.ScrollbarSize = 7;
    s.ScrollbarRounding = 7;
    s.DisabledAlpha = .42f;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.Colors[ImGuiCol_WindowBg] = {.038f, .041f, .05f, 1};
    s.Colors[ImGuiCol_ChildBg] = {.055f, .061f, .074f, 1};
    s.Colors[ImGuiCol_Border] = {1, 1, 1, .065f};
    s.Colors[ImGuiCol_Text] = {.92f, .93f, .96f, 1};
    s.Colors[ImGuiCol_TextDisabled] = muted;
    s.Colors[ImGuiCol_Button] = {.11f, .12f, .145f, 1};
    s.Colors[ImGuiCol_ButtonHovered] = {.16f, .16f, .20f, 1};
    s.Colors[ImGuiCol_ButtonActive] = {.21f, .19f, .28f, 1};
    s.Colors[ImGuiCol_FrameBg] = {.08f, .087f, .105f, 1};
    s.Colors[ImGuiCol_FrameBgHovered] = {.14f, .12f, .19f, 1};
    s.Colors[ImGuiCol_FrameBgActive] = {.20f, .17f, .28f, 1};
    s.Colors[ImGuiCol_Header] = {.17f, .14f, .23f, 1};
    s.Colors[ImGuiCol_HeaderHovered] = {.21f, .18f, .28f, 1};
    s.Colors[ImGuiCol_Tab] = {.09f, .08f, .12f, 1};
    s.Colors[ImGuiCol_TabHovered] = {.22f, .18f, .30f, 1};
    s.Colors[ImGuiCol_TabSelected] = {.19f, .15f, .27f, 1};
    s.Colors[ImGuiCol_TabSelectedOverline] = accent;
    s.Colors[ImGuiCol_Separator] = {1, 1, 1, .08f};
    s.Colors[ImGuiCol_CheckMark] = accent;
    s.Colors[ImGuiCol_NavCursor] = accent;
    s.ScaleAllSizes(uiScale);
    s.FontScaleDpi = uiScale;
}
void Icon(ImDrawList *d, ImVec2 p, int which, ImU32 color) {
    constexpr vortex::icons::Id items[]{vortex::icons::Id::LayoutDashboard, vortex::icons::Id::Download,
                                        vortex::icons::Id::SlidersHorizontal, vortex::icons::Id::Monitor,
                                        vortex::icons::Id::Folder};
    vortex::icons::Draw(d, items[std::clamp(which, 0, 4)], {p.x - 2 * uiScale, p.y - 2 * uiScale}, 19 * uiScale, color);
}
bool Navigation(const char *label, int icon, bool active) {
    const auto p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x, s = uiScale;
    const bool clicked = ImGui::InvisibleButton(label, {width, 42 * s}, ImGuiButtonFlags_EnableNav);
    auto *d = ImGui::GetWindowDrawList();
    const auto id = ImGui::GetItemID();
    const float goal = active ? 1.f : ImGui::IsItemHovered() ? .4f : 0;
    const float t = Animate(id, goal, active ? 1.f : 0.f);
    ItemInteraction();
    if (t > .001f)
        d->AddRectFilled(p, {p.x + width, p.y + 42 * s},
                         ImGui::ColorConvertFloat4ToU32({accent.x, accent.y, accent.z, .095f * t}), 7 * s);
    const auto ink = ImGui::ColorConvertFloat4ToU32(active ? ImVec4(.94f, .93f, .99f, 1) : muted);
    Icon(d, {p.x + 13 * s, p.y + 14 * s}, icon, active ? ImGui::ColorConvertFloat4ToU32(accent) : ink);
    d->AddText({p.x + 42 * s, p.y + 12 * s}, ink, label);
    if (active)
        d->AddCircleFilled({p.x + width - 12 * s, p.y + 21 * s}, 2 * s, ImGui::ColorConvertFloat4ToU32(accent));
    return clicked;
}
void Heading(const char *text, float scale = 1.7f) {
    ImGui::PushFont(semiboldFont);
    ImGui::SetWindowFontScale(scale);
    ImGui::TextUnformatted(text);
    ImGui::SetWindowFontScale(1);
    ImGui::PopFont();
}
void Hint(const char *text) {
    ImGui::PushStyleColor(ImGuiCol_Text, muted);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
bool Primary(const char *text, ImVec2 size = {0, 40}) {
    ImGui::PushStyleColor(ImGuiCol_Button, accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.75f, .67f, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.58f, .47f, .84f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyle().Alpha < 1.f ? ImVec4(.92f, .93f, .96f, 1) : ImVec4(.07f, .055f, .1f, 1));
    ImGui::PushFont(semiboldFont);
    const bool result = ImGui::Button(text, {size.x * uiScale, size.y * uiScale});
    ImGui::PopFont();
    ImGui::PopStyleColor(4);
    ItemInteraction();
    return result;
}
void Card(const char *name, float height = 0) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22 * uiScale, 20 * uiScale));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.055f, .061f, .074f, 1));
    ImGui::BeginChild(name, {0, height * uiScale}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, 0);
    auto *draw = ImGui::GetWindowDrawList();
    const auto p = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    const auto sheen = ImGui::GetColorU32(ImVec4(1, 1, 1, .018f));
    const auto clear = ImGui::GetColorU32(ImVec4(1, 1, 1, 0));
    draw->AddRectFilledMultiColor({p.x + 5 * uiScale, p.y + 1},
                                  {p.x + size.x - 5 * uiScale, p.y + (std::min)(74 * uiScale, size.y - 2)}, sheen,
                                  sheen, clear, clear);
    draw->AddLine({p.x + 16 * uiScale, p.y + 1}, {p.x + size.x - 16 * uiScale, p.y + 1},
                  ImGui::GetColorU32(ImVec4(1, 1, 1, .06f)));
}
void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}
bool Toggle(const char *label, bool &value) {
    const float s = uiScale;
    const auto p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const bool clicked = ImGui::InvisibleButton(label, {width, 26 * s}, ImGuiButtonFlags_EnableNav);
    if (clicked)
        value = !value;
    auto *d = ImGui::GetWindowDrawList();
    const float t = Animate(ImGui::GetItemID(), value ? 1.f : 0.f, value ? 1.f : 0.f);
    d->PushClipRect(p, {p.x + width - 44 * s, p.y + 26 * s}, true);
    d->AddText({p.x, p.y + 4 * s}, ImGui::GetColorU32(ImGuiCol_Text), label);
    d->PopClipRect();
    const ImVec2 at{p.x + width - 36 * s, p.y + 3 * s};
    d->AddRectFilled(at, {at.x + 36 * s, at.y + 20 * s},
                     value ? ImGui::GetColorU32(accent) : ImGui::GetColorU32(ImVec4(.208f, .212f, .255f, 1)), 10 * s);
    d->AddCircleFilled({at.x + (10 + 16 * t) * s, at.y + 10 * s}, 7 * s,
                       ImGui::GetColorU32(value ? ImVec4(.96f, .95f, 1, 1) : ImVec4(.59f, .59f, .65f, 1)), 24);
    ItemInteraction();
    return clicked;
}
bool ActionRow(const char *id, const char *title, const char *description, int icon) {
    const float s = uiScale;
    Card(id, 84);
    auto p = ImGui::GetCursorScreenPos();
    Icon(ImGui::GetWindowDrawList(), {p.x + 1 * s, p.y + 11 * s}, icon, IM_COL32(177, 160, 224, 255));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 38 * s);
    ImGui::BeginGroup();
    Heading(title, 1.f);
    Hint(description);
    ImGui::EndGroup();
    auto *d = ImGui::GetWindowDrawList();
    const auto pos = ImGui::GetWindowPos();
    const float right = pos.x + ImGui::GetWindowWidth() - 27 * s;
    vortex::icons::Draw(d, vortex::icons::Id::ChevronRight, {right - 13 * s, pos.y + 31 * s}, 19 * s,
                        ImGui::GetColorU32(muted));
    ImGui::SetCursorScreenPos(p);
    const bool clicked =
        ImGui::InvisibleButton("Open", {ImGui::GetContentRegionAvail().x, 43 * s}, ImGuiButtonFlags_EnableNav);
    const float hover = Animate(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.f : 0.f);
    if (hover > .001f)
        d->AddRect(pos, {pos.x + ImGui::GetWindowWidth(), pos.y + 84 * s},
                   ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, .33f * hover)), 12 * s);
    ItemInteraction();
    EndCard();
    return clicked;
}
void OpenPath(HWND window, const std::filesystem::path &path) {
    const auto result =
        reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32)
        throw std::runtime_error("Windows could not open the requested file or folder.");
}
void LaunchPreview(const std::filesystem::path &directory) {
    const auto path = directory / L"ObserverDemo.exe";
    std::wstring command = L"\"" + path.wstring() + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(), &startup,
                        &process))
        throw std::runtime_error(WindowsError("Open preview"));
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}
int RunUpdateTest(const std::wstring &arguments) {
    Updater updater(UpdateUrl);
    updater.Check();
    while (updater.Busy())
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    auto status = updater.Status();
    if (arguments.find(L"--download-update-test") != std::wstring::npos ||
        arguments.find(L"--apply-update-test") != std::wstring::npos) {
        if (status.stage == UpdateStage::Available) {
            updater.Download();
            while (updater.Busy())
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        status = updater.Status();
    }
    std::ofstream out(LogDirectory() / L"update-test.txt");
    out << static_cast<int>(status.stage) << '\n' << status.version << '\n' << status.message << '\n';
    out.close();
    if (arguments.find(L"--apply-update-test") != std::wstring::npos) {
        std::string error;
        if (!updater.Apply(error, false)) {
            Log(error);
            return 4;
        }
        return 0;
    }
    return status.stage == UpdateStage::Error || status.stage == UpdateStage::Development ||
                   status.stage == UpdateStage::Unconfigured
               ? 3
               : 0;
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int show) {
    InitializeUpdates();
    const std::wstring arguments(command);
    const bool smoke = arguments.find(L"--smoke") != std::wstring::npos;
    if (arguments.find(L"-update-test") != std::wstring::npos) {
        try {
            return RunUpdateTest(arguments);
        } catch (const std::exception &e) {
            Log(e.what());
            return 5;
        }
    }
    Handle singleton(CreateMutexW(nullptr, FALSE, L"Local\\Joshu.Vortex.Launcher"));
    if (!singleton)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS && !smoke) {
        if (auto existing = FindWindowW(L"VortexLauncher", L"Vortex")) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }
    HWND window{};
    bool imguiReady{};
    try {
        const auto directory = ModuleDirectory();
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = L"VortexLauncher";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
        wc.hIconSm = wc.hIcon;
        if (!RegisterClassExW(&wc))
            throw std::runtime_error(WindowsError("Register window"));
        window = CreateWindowExW(0, wc.lpszClassName, L"Vortex", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                 MulDiv(900, GetDpiForSystem(), 96), MulDiv(640, GetDpiForSystem(), 96), nullptr,
                                 nullptr, instance, nullptr);
        if (!window)
            throw std::runtime_error(WindowsError("Create window"));
        const MARGINS frame{1, 1, 1, 1};
        DwmExtendFrameIntoClientArea(window, &frame);
        const DWORD rounded = 2;
        DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(33), &rounded, sizeof(rounded));
        SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        InitializeGraphics(window);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        imguiReady = true;
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        const auto preferences = DataDirectory() / L"Launcher.ini";
        BOOL systemAnimations = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &systemAnimations, 0);
        animations =
            GetPrivateProfileIntW(L"Appearance", L"Animations", systemAnimations ? 1 : 0, preferences.c_str()) != 0;
        launcherFont = (std::min)(GetPrivateProfileIntW(L"Appearance", L"Font", 0, preferences.c_str()),
                                  static_cast<UINT>(awareness::FontPresets.size() - 1));
        ApplyLauncherFont();
        uiScale = GetDpiForWindow(window) / 96.f;
        Theme();
        ImGui_ImplWin32_Init(window);
        ImGui_ImplDX11_Init(device.Get(), context.Get());
        if (!smoke)
            ShowWindow(window, show);
        Updater updater(UpdateUrl);
        bool automatic = GetPrivateProfileIntW(L"Updates", L"CheckOnStartup", 1, preferences.c_str()) != 0;
        if (automatic && !smoke)
            updater.Check();
        auto lastCheck = std::chrono::steady_clock::now();
        auto lastRefresh = lastCheck - std::chrono::seconds(10);
        std::vector<Process> processes;
        int selected{}, page{};
        std::future<std::string> loadWork;
        Process pendingLoad, loadedTarget;
        bool loading{}, quit{};
        std::string feedback, loadSuccessFeedback;
        int frames{};
        while (!quit) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                if (msg.message == WM_QUIT)
                    quit = true;
            }
            if (closeRequested) {
                if (loading)
                    feedback = "Please wait for the current operation to finish.";
                else {
                    updater.Cancel();
                    quit = true;
                }
                closeRequested = false;
            }
            if (quit)
                break;
            if (loading && loadWork.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try {
                    feedback = loadWork.get();
                    loadedTarget = pendingLoad;
                    loadSuccessFeedback = feedback;
                } catch (const std::exception &e) {
                    feedback = e.what();
                }
                Log(feedback);
                loading = false;
                // Refresh immediately: the target can close while a load operation completes.
                lastRefresh = std::chrono::steady_clock::time_point{};
            }
            if (smoke)
                page = frames / 10;
            const auto now = std::chrono::steady_clock::now();
            if (!smoke && now - lastRefresh > std::chrono::seconds(3) && !loading) {
                try {
                    const auto oldId = processes.empty()
                                           ? 0
                                           : processes[std::min(selected, static_cast<int>(processes.size() - 1))].id;
                    processes = FindTargets();
                    selected = 0;
                    for (size_t i = 0; i < processes.size(); ++i)
                        if (processes[i].id == oldId)
                            selected = static_cast<int>(i);
                    if (loadedTarget.id &&
                        std::none_of(processes.begin(), processes.end(), [&](const Process &candidate) {
                            return candidate.id == loadedTarget.id && candidate.created == loadedTarget.created;
                        })) {
                        // Only retire our old success notice; preserve any newer error or update feedback.
                        if (feedback == loadSuccessFeedback)
                            feedback = "Game closed.";
                        loadedTarget = {};
                        loadSuccessFeedback.clear();
                    }
                } catch (const std::exception &e) {
                    feedback = e.what();
                }
                lastRefresh = now;
            }
            if (automatic && !smoke && now - lastCheck > std::chrono::hours(6) && !updater.Busy()) {
                updater.Check();
                lastCheck = now;
            }
            if (IsIconic(window) && !smoke) {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                continue;
            }
            if (resizePending && nextWidth && nextHeight) {
                context->OMSetRenderTargets(0, nullptr, nullptr);
                target.Reset();
                CheckHr(swapchain->ResizeBuffers(0, nextWidth, nextHeight, DXGI_FORMAT_UNKNOWN, 0),
                        "Could not resize the window.");
                CreateTarget();
                resizePending = false;
            }
            const float nextScale = GetDpiForWindow(window) / 96.f;
            if (nextScale != uiScale) {
                uiScale = nextScale;
                Theme();
            }
            ApplyLauncherFont();
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            const auto viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
            ImGui::Begin("Vortex", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            const float s = uiScale;
            ImGui::SetCursorPos({0, 0});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.03f, .033f, .04f, 1));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * s, 28 * s));
            ImGui::BeginChild("Navigation", {188 * s, 0}, ImGuiChildFlags_AlwaysUseWindowPadding,
                              ImGuiWindowFlags_NoScrollbar);
            const auto origin = ImGui::GetCursorScreenPos();
            auto *draw = ImGui::GetWindowDrawList();
            vortex::brand::Mark(draw, {origin.x + 17 * s, origin.y + 18 * s}, 30 * s, IM_COL32(240, 239, 248, 255),
                                ImGui::ColorConvertFloat4ToU32(accent));
            ImGui::SetCursorPosX(61 * s);
            Heading("Vortex", 1.5f);
            ImGui::Dummy({0, 37 * s});
            const auto update = updater.Status();
            const char *tabs[]{"Library", "Updates", "Settings"};
            for (int i = 0; i < 3; ++i)
                if (Navigation(tabs[i], i, page == i))
                    page = i;
            if (update.stage == UpdateStage::Available || update.stage == UpdateStage::Ready) {
                ImGui::Spacing();
                ImGui::TextColored(accent, "Update available");
            }
            ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - 89 * s));
            ImGui::Separator();
            ImGui::Dummy({0, 4 * s});
            const auto profileAt = ImGui::GetCursorScreenPos();
            vortex::brand::Avatar(draw, {}, profileAt, 32 * s, false);
            ImGui::SetCursorPosX(60 * s);
            ImGui::BeginGroup();
            ImGui::TextUnformatted("Local profile");
            ImGui::TextDisabled("Vortex %s", AppVersion);
            ImGui::EndGroup();
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 0);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32 * s, 53 * s));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.038f, .041f, .05f, 1));
            ImGui::BeginChild("Content", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding, 0);
            if (page == 0) {
                Heading("Library", 1.7f);

                ImGui::Dummy({0, 10 * s});
                Card("Counter-Strike 2", processes.size() > 1 ? 246.f : 222.f);
                const auto pos = ImGui::GetCursorScreenPos();
                auto *art = ImGui::GetWindowDrawList();
                art->AddRectFilled(pos, {pos.x + 48 * s, pos.y + 48 * s}, IM_COL32(29, 27, 40, 255), 10 * s);
                art->AddText(semiboldFont, 17 * s, {pos.x + 8 * s, pos.y + 14 * s}, IM_COL32(193, 176, 242, 255),
                             "CS2");
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 64 * s);
                ImGui::BeginGroup();
                Heading("Counter-Strike 2", 1.2f);
                ImGui::TextDisabled("Vortex %s  /  Installed", AppVersion);
                ImGui::EndGroup();
                ImGui::SetCursorPosY(84 * s);
                ImGui::Separator();
                ImGui::SetCursorPosY(101 * s);
                const auto statusAt = ImGui::GetCursorScreenPos();
                art->AddCircleFilled({statusAt.x + 3 * s, statusAt.y + 8 * s}, 3 * s,
                                     processes.empty() ? IM_COL32(111, 113, 126, 255) : IM_COL32(182, 159, 241, 255),
                                     16);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14 * s);
                ImGui::TextUnformatted(processes.empty() ? "Waiting for game" : "Ready to load");
                ImGui::SetCursorPosY(126 * s);
                if (processes.empty())
                    Hint("Open Counter-Strike 2 to launch the overlay.");
                else if (processes.size() > 1) {
                    auto selectedLabel =
                        Utf8(processes[selected].name) + "  /  " + std::to_string(processes[selected].id);
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::BeginCombo("##process", selectedLabel.c_str())) {
                        for (size_t i = 0; i < processes.size(); ++i) {
                            auto label = Utf8(processes[i].name) + "  /  " + std::to_string(processes[i].id);
                            if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == selected))
                                selected = static_cast<int>(i);
                        }
                        ImGui::EndCombo();
                    }
                } else
                    Hint("Ready");
                ImGui::SetCursorPosY((processes.size() > 1 ? 184.f : 162.f) * s);
                ImGui::BeginDisabled(processes.empty() || loading || updater.Busy());
                if (Primary(loading ? "Loading..." : "Launch overlay", {164, 40})) {
                    const auto process = processes[selected];
                    pendingLoad = process;
                    loading = true;
                    feedback.clear();
                    loadWork = std::async(std::launch::async, [process, directory] {
                        return LoadDll(process, directory / L"EntityAwarenessOverlay.dll");
                    });
                }
                ImGui::EndDisabled();
                ImGui::SameLine(0, 18 * s);
                ImGui::AlignTextToFramePadding();
                Hint("Insert opens the menu");
                EndCard();
                if (ActionRow("Preview", "Preview", "Offline visual preview", 3))
                    LaunchPreview(directory);
                if (ActionRow("Profiles", "Profiles", "Saved configurations", 4)) {
                    const auto profiles = DataDirectory() / L"profiles";
                    std::filesystem::create_directories(profiles);
                    OpenPath(window, profiles);
                }
                if (update.stage == UpdateStage::Available || update.stage == UpdateStage::Ready) {
                    if (ImGui::SmallButton("View available update"))
                        page = 1;
                }
            } else if (page == 1) {
                Heading("Updates", 1.7f);

                ImGui::Spacing();
                Card("Update", std::max(290.f, ImGui::GetContentRegionAvail().y / s - 30.f));
                ImGui::TextColored(accent, "Vortex %s", AppVersion);
                ImGui::Spacing();
                Heading(update.stage == UpdateStage::Available || update.stage == UpdateStage::Ready
                            ? "Update available"
                            : "Installed version",
                        1.25f);
                ImGui::TextWrapped("%s", update.message.c_str());
                if (!update.version.empty())
                    ImGui::Text("%s  ->  %s", AppVersion, update.version.c_str());
                if (!update.notes.empty()) {
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", update.notes.c_str());
                }
                if (update.stage == UpdateStage::Downloading)
                    ImGui::ProgressBar(update.progress / 100.f, {-1, 12});
                ImGui::Spacing();
                ImGui::BeginDisabled(updater.Busy() || loading);
                if (update.stage == UpdateStage::Available) {
                    if (Primary("Download update"))
                        updater.Download();
                } else if (update.stage == UpdateStage::Ready) {
                    if (Primary("Install and restart")) {
                        if (updater.Apply(feedback))
                            quit = true;
                    }
                } else {
                    ImGui::BeginDisabled(update.stage == UpdateStage::Unconfigured);
                    if (Primary("Check for updates"))
                        updater.Check();
                    ImGui::EndDisabled();
                }
                ImGui::EndDisabled();
                EndCard();
            } else {
                Heading("Settings", 1.7f);

                ImGui::Spacing();
                if (ImGui::BeginTabBar("LauncherPreferences")) {
                    if (ImGui::BeginTabItem("Appearance")) {
                        Card("Appearance", 212);
                        Heading("Interface", 1.15f);

                        ImGui::Spacing();
                        ImGui::TextUnformatted("Interface font");
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::BeginCombo("##launcherFont", awareness::FontPresets[launcherFont].name)) {
                            for (std::uint32_t i = 0; i < awareness::FontPresets.size(); ++i) {
                                const bool installed = i == 0 || !awareness::FontFile(i).empty();
                                ImGui::BeginDisabled(!installed);
                                if (ImGui::Selectable(awareness::FontPresets[i].name, i == launcherFont)) {
                                    launcherFont = i;
                                    const auto value = std::to_wstring(i);
                                    if (!WritePrivateProfileStringW(L"Appearance", L"Font", value.c_str(),
                                                                    preferences.c_str()))
                                        feedback = "The font is applied, but could not be saved.";
                                }
                                ImGui::EndDisabled();
                            }
                            ImGui::EndCombo();
                        }
                        if (Toggle("Interface animations", animations))
                            if (!WritePrivateProfileStringW(L"Appearance", L"Animations", animations ? L"1" : L"0",
                                                            preferences.c_str()))
                                feedback = "The animation preference could not be saved.";
                        EndCard();

                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Updates")) {
                        Card("Preferences", 162);
                        Heading("Automatic updates", 1.15f);
                        if (Toggle("Check automatically", automatic))
                            if (!WritePrivateProfileStringW(L"Updates", L"CheckOnStartup", automatic ? L"1" : L"0",
                                                            preferences.c_str()))
                                feedback = "The update preference could not be saved.";
                        Hint("Check when Vortex opens and every six hours while it is running.");
                        EndCard();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Files & support")) {
                        Card("Files", 220);
                        Heading("Profiles & support", 1.15f);

                        if (ImGui::Button("Profile library")) {
                            const auto profiles = DataDirectory() / L"profiles";
                            std::filesystem::create_directories(profiles);
                            OpenPath(window, profiles);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Working files"))
                            OpenPath(window, DataDirectory());
                        if (ImGui::Button("Open logs"))
                            OpenPath(window, LogDirectory());
                        ImGui::Spacing();
                        Hint("Manage profiles in Overview > My profiles.");
                        EndCard();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
            }
            if (!feedback.empty()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextWrapped("%s", feedback.c_str());
            }
            ImGui::Spacing();
            ImGui::Dummy({0, 2});
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            FrameControls(window, animations);
            ImGui::End();
            ImGui::Render();
            const float clear[]{.055f, .061f, .085f, 1};
            context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
            context->ClearRenderTargetView(target.Get(), clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            if (smoke && frames % 10 == 9)
                CaptureTestFrame(device.Get(), context.Get(), swapchain.Get(), page);
            const auto presented = swapchain->Present(1, 0);
            CheckHr(presented, "The graphics device was lost. Reopen Vortex.");
            if (presented == DXGI_STATUS_OCCLUDED && !smoke)
                MsgWaitForMultipleObjectsEx(0, nullptr, 80, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (smoke && ++frames >= 30)
                quit = true;
        }
        if (loading && loadWork.valid())
            loadWork.wait();
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imguiReady = false;
        target.Reset();
        swapchain.Reset();
        context.Reset();
        device.Reset();
        DestroyWindow(window);
        UnregisterClassW(L"VortexLauncher", instance);
        Log(smoke ? "Launcher smoke passed." : "Vortex closed.");
        return 0;
    } catch (const std::exception &e) {
        Log(e.what());
        if (!smoke)
            MessageBoxW(window, Wide(e.what()).c_str(), L"Vortex", MB_OK | MB_ICONERROR);
        if (imguiReady) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
        if (window)
            DestroyWindow(window);
        return 1;
    }
}
