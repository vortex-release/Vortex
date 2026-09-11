#include "steam_profile.hpp"
#include "spectator_panel.hpp"
#include "session_badge.hpp"
#include <imgui_impl_dx11.h>
#include <fstream>
#include <cstdio>
#include <filesystem>
int main() {
    using namespace awareness;
    unsigned failures{};
    const auto check = [&](bool value, const char *message) {
        if (!value) {
            ++failures;
            std::printf("FAIL %s\n", message);
        }
    };
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    const auto fixture = std::filesystem::path(path).parent_path() / L"steam_api64.dll";
    auto module =
        LoadLibraryExW(fixture.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module)
        return 2;
    const auto calls = reinterpret_cast<int (*)()>(GetProcAddress(module, "FixtureAvatarReads"));
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device,
                                 nullptr, &context)))
        return 3;
    {
        SteamProfile profile;
        profile.Update(device.Get(), 1000);
        check(profile.Connected() && std::string(profile.Name()) == "Observer Demo", "exports resolve current persona");
        check(profile.Avatar() != nullptr && calls && calls() == 1, "avatar uploaded once");
        profile.Update(device.Get(), 4000);
        check(calls && calls() == 1, "repeated polls reuse the cached image handle");
        auto *other = profile.AvatarFor(device.Get(), 2, 4100);
        check(other != nullptr && calls() == 2, "spectator avatar is uploaded");
        check(profile.AvatarFor(device.Get(), 2, 4200) == other && calls() == 2, "spectator avatar is reused");
        check(!profile.AvatarFor(device.Get(), 3, 4150) && calls() == 2, "new avatar requests are bounded per frame");
        check(profile.AvatarFor(device.Get(), 3, 4250) != nullptr && calls() == 3,
              "queued spectator resolves on later frame");
        check(!profile.AvatarFor(device.Get(), 0, 4300), "missing Steam ID never reuses somebody else's avatar");
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        if (profile.Avatar())
            profile.Avatar()->GetResource(&resource);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> image;
        if (resource)
            resource.As(&image);
        D3D11_TEXTURE2D_DESC desc{};
        if (image)
            image->GetDesc(&desc);
        check(desc.Width == 64 && desc.Height == 64 && desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM,
              "RGBA avatar texture has expected size");
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = {640, 360};
        io.DeltaTime = 1.f / 60;
        io.Fonts->AddFontDefault();
        check(ImGui_ImplDX11_Init(device.Get(), context.Get()), "spectator renderer initializes");
        D3D11_TEXTURE2D_DESC cardDesc{};
        cardDesc.Width = 640;
        cardDesc.Height = 360;
        cardDesc.MipLevels = cardDesc.ArraySize = cardDesc.SampleDesc.Count = 1;
        cardDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        cardDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> card, readback;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> cardTarget;
        check(SUCCEEDED(device->CreateTexture2D(&cardDesc, nullptr, &card)) &&
                  SUCCEEDED(device->CreateRenderTargetView(card.Get(), nullptr, &cardTarget)),
              "spectator card target");
        auto readDesc = cardDesc;
        readDesc.BindFlags = 0;
        readDesc.Usage = D3D11_USAGE_STAGING;
        readDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        device->CreateTexture2D(&readDesc, nullptr, &readback);
        cs2::SpectatorFrame watchers;
        watchers.count = 2;
        watchers.entries[0].steamId = 2;
        watchers.entries[0].mode = 2;
        watchers.entries[1].steamId = 3;
        watchers.entries[1].mode = 3;
        std::memcpy(watchers.entries[0].name, "Cedar", sizeof("Cedar"));
        std::memcpy(watchers.entries[1].name, "Juniper", sizeof("Juniper"));
        VisualOptions visual;
        bool edited{};
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        const auto *list = DrawSpectatorPanel(&watchers, visual, profile, device.Get(), false, edited);
        const auto *badge = DrawSessionBadge(visual, profile.Name(),
                                             ImTextureRef(reinterpret_cast<ImTextureID>(profile.Avatar())), true, 26);
        check(badge && badge->VtxBuffer.Size > 100, "badge draws logo, connection, name and avatar");
        if (badge)
            for (const auto &vertex : badge->VtxBuffer)
                check(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y) && vertex.pos.x >= 0 &&
                          vertex.pos.x <= 640 && vertex.pos.y >= 0 && vertex.pos.y <= 360,
                      "badge geometry fits viewport");
        ImGui::Render();
        check(list && ImGui::GetDrawData()->TotalVtxCount > 50 && !edited,
              "floating avatars and names render while the menu is closed");
        const float background[]{.025f, .03f, .045f, 1};
        context->ClearRenderTargetView(cardTarget.Get(), background);
        auto *rtv = cardTarget.Get();
        context->OMSetRenderTargets(1, &rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->CopyResource(readback.Get(), card.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        if (SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &map))) {
            std::vector<unsigned char> bgra(640 * 360 * 4);
            for (unsigned y = 0; y < 360; ++y)
                for (unsigned x = 0; x < 640; ++x) {
                    const auto *p = static_cast<unsigned char *>(map.pData) + y * map.RowPitch + x * 4;
                    auto *q = bgra.data() + (y * 640 + x) * 4;
                    q[0] = p[2];
                    q[1] = p[1];
                    q[2] = p[0];
                    q[3] = p[3];
                }
            context->Unmap(readback.Get(), 0);
            BITMAPFILEHEADER fh{};
            fh.bfType = 0x4d42;
            fh.bfOffBits = 54;
            fh.bfSize = 54 + static_cast<DWORD>(bgra.size());
            BITMAPINFOHEADER ih{};
            ih.biSize = 40;
            ih.biWidth = 640;
            ih.biHeight = -360;
            ih.biPlanes = 1;
            ih.biBitCount = 32;
            std::ofstream screenshot("spectator-card.bmp", std::ios::binary);
            screenshot.write(reinterpret_cast<const char *>(&fh), sizeof(fh));
            screenshot.write(reinterpret_cast<const char *>(&ih), sizeof(ih));
            screenshot.write(reinterpret_cast<const char *>(bgra.data()), bgra.size());
        }
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        check(!DrawSpectatorPanel(nullptr, visual, profile, device.Get(), false, edited),
              "empty spectator card hides without stale names");
        check(!DrawSessionBadge(visual, "Name", {}, false, 26, nullptr, {0, 0, 640, 360}),
              "open menu over badge suppresses it without covering controls");
        check(DrawSessionBadge(visual, "Name", {}, false, 26, nullptr, {0, 100, 200, 300}) != nullptr,
              "menu outside badge corner retains badge");
        visual.sessionBadge = 0;
        check(!DrawSessionBadge(visual, "Name", {}, false, -1), "disabled badge emits nothing");
        visual.sessionBadge = 1;
        visual.badgeLight = 0;
        const std::string longName(200, 'W');
        check(DrawSessionBadge(visual, longName.c_str(), {}, false, -1) != nullptr,
              "long name and unavailable ping draw");
        const auto fit = vortex::brand::FitText(ImGui::GetFont(), 13, "A very long player name", 70);
        check(ImGui::GetFont()->CalcTextSizeA(13, FLT_MAX, 0, fit.c_str()).x <= 70,
              "name ellipsis fits allotted space");
        ImGui::EndFrame();
        ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext();
    }
    FreeLibrary(module);
    SteamProfileSample sample;
    ImagePixels pixels;
    check(!ReadSteamProfile({}, sample) && !sample.id, "missing Steam stays offline");
    SteamProfileApi mock;
    mock.friends = mock.user = mock.utils = &sample;
    mock.persona = [](void *) -> const char * { return "Name\nDemo"; };
    mock.steamId = [](void *) -> std::uint64_t { return 1; };
    mock.avatar = [](void *, std::uint64_t) { return 2; };
    mock.imageSize = [](void *, int, UINT *w, UINT *h) {
        *w = *h = 50000;
        return true;
    };
    mock.imageRgba = [](void *, int, BYTE *, int) { return false; };
    check(ReadSteamProfile(mock, sample) && std::string(sample.name) == "Name Demo",
          "profile control characters are removed");
    check(!ReadSteamAvatar(mock, sample, pixels) && pixels.rgba.empty(), "oversized avatar rejected before allocating");
    sample.width = sample.height = 32;
    check(!ReadSteamAvatar(mock, sample, pixels) && pixels.rgba.empty(),
          "failed avatar decode does not publish pixels");
    std::printf("Steam profile checks: %u failures\n", failures);
    return failures ? 1 : 0;
}
