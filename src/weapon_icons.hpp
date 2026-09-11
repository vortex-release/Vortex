#pragma once
#include "weapon_catalog.hpp"
#include "runtime_support.hpp"
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <imgui.h>
#include <vector>
namespace awareness {
class WeaponAtlas {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;

  public:
    HRESULT Initialize(ID3D11Device *device) {
        const auto resource = FindResourceW(OverlayModule(), MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10));
        if (!resource)
            return HRESULT_FROM_WIN32(GetLastError());
        const auto size = SizeofResource(OverlayModule(), resource);
        const auto bytes = static_cast<BYTE *>(LockResource(LoadResource(OverlayModule(), resource)));
        if (!bytes || !size)
            return E_FAIL;
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(com) && com != RPC_E_CHANGED_MODE)
            return com;
        struct ComScope {
            bool initialized;
            ~ComScope() {
                if (initialized)
                    CoUninitialize();
            }
        } scope{SUCCEEDED(com)};
        using Microsoft::WRL::ComPtr;
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr))
            return hr;
        if (FAILED(hr = factory->CreateStream(&stream)) || FAILED(hr = stream->InitializeFromMemory(bytes, size)) ||
            FAILED(
                hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) ||
            FAILED(hr = decoder->GetFrame(0, &frame)) || FAILED(hr = factory->CreateFormatConverter(&converter)) ||
            FAILED(hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                              nullptr, 0, WICBitmapPaletteTypeCustom)))
            return hr;
        UINT width{}, height{};
        if (FAILED(hr = converter->GetSize(&width, &height)))
            return hr;
        if (!width || !height || width > 4096 || height > 4096)
            return E_INVALIDARG;
        std::vector<BYTE> pixels(static_cast<std::size_t>(width) * height * 4);
        if (FAILED(hr = converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())))
            return hr;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{pixels.data(), width * 4, 0};
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(hr = device->CreateTexture2D(&desc, &initial, &texture)))
            return hr;
        return device->CreateShaderResourceView(texture.Get(), nullptr, &view_);
    }
    ImTextureRef Texture() const noexcept { return ImTextureRef(reinterpret_cast<ImTextureID>(view_.Get())); }
};
inline bool DrawWeaponIcon(ImDrawList *draw, ImTextureRef atlas, std::uint32_t id, ImVec2 at, ImVec2 size,
                           ImU32 color) {
    const auto *icon = FindWeaponIcon(id);
    if (!icon || !atlas.GetTexID())
        return false;
    draw->AddImage(atlas, at, {at.x + size.x, at.y + size.y}, {icon->u0, icon->v0}, {icon->u1, icon->v1}, color);
    return true;
}
} // namespace awareness
