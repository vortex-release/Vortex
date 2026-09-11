#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace awareness {
struct ImagePixels {
    UINT width{}, height{};
    std::vector<BYTE> rgba;
};
inline HRESULT DecodeImage(const wchar_t *path, std::span<const BYTE> bytes, ImagePixels &out) {
    using Microsoft::WRL::ComPtr;
    out = {};
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE)
        return com;
    struct Scope {
        bool initialized;
        ~Scope() {
            if (initialized)
                CoUninitialize();
        }
    } scope{SUCCEEDED(com)};
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr))
        return hr;
    if (path) {
        hr = factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    } else {
        if (bytes.empty() || bytes.size() > MAXDWORD)
            return E_INVALIDARG;
        if (FAILED(hr = factory->CreateStream(&stream)) ||
            FAILED(
                hr = stream->InitializeFromMemory(const_cast<BYTE *>(bytes.data()), static_cast<DWORD>(bytes.size()))))
            return hr;
        hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    }
    if (FAILED(hr) || FAILED(hr = decoder->GetFrame(0, &frame)))
        return hr;
    UINT w{}, h{};
    if (FAILED(hr = frame->GetSize(&w, &h)))
        return hr;
    if (!w || !h || w > 65536 || h > 65536 || std::uint64_t(w) * h > 268435456)
        return E_INVALIDARG;
    IWICBitmapSource *source = frame.Get();
    if (w > 2048 || h > 2048) {
        const float ratio = 2048.f / static_cast<float>((std::max)(w, h));
        w = (std::max)(1u, static_cast<UINT>(w * ratio));
        h = (std::max)(1u, static_cast<UINT>(h * ratio));
        if (FAILED(hr = factory->CreateBitmapScaler(&scaler)) ||
            FAILED(hr = scaler->Initialize(frame.Get(), w, h, WICBitmapInterpolationModeFant)))
            return hr;
        source = scaler.Get();
    }
    if (FAILED(hr = factory->CreateFormatConverter(&converter)) ||
        FAILED(hr = converter->Initialize(source, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                          WICBitmapPaletteTypeCustom)))
        return hr;
    ImagePixels next{w, h, std::vector<BYTE>(static_cast<std::size_t>(w) * h * 4)};
    if (FAILED(hr = converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(next.rgba.size()), next.rgba.data())))
        return hr;
    out = std::move(next);
    return S_OK;
}
inline HRESULT UploadImage(ID3D11Device *device, const ImagePixels &image,
                           Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> &view) {
    if (!image.width || !image.height || image.rgba.size() != std::size_t(image.width) * image.height * 4)
        return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = image.width;
    desc.Height = image.height;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{image.rgba.data(), image.width * 4, 0};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = device->CreateTexture2D(&desc, &data, &texture);
    if (FAILED(hr))
        return hr;
    return device->CreateShaderResourceView(texture.Get(), nullptr, view.ReleaseAndGetAddressOf());
}
} // namespace awareness
