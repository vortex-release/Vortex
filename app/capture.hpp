#pragma once
#include "app_paths.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <fstream>
#include <vector>
namespace vortex {
inline void CaptureTestFrame(ID3D11Device *device, ID3D11DeviceContext *context, IDXGISwapChain *chain, int page) {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> source, copy;
    if (FAILED(chain->GetBuffer(0, IID_PPV_ARGS(&source))))
        throw std::runtime_error("Cannot capture smoke frame.");
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &copy)))
        throw std::runtime_error("Cannot create smoke readback.");
    context->CopyResource(copy.Get(), source.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(context->Map(copy.Get(), 0, D3D11_MAP_READ, 0, &map)))
        throw std::runtime_error("Cannot map smoke frame.");
    std::vector<unsigned char> pixels(static_cast<size_t>(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; y++)
        for (UINT x = 0; x < desc.Width; x++) {
            auto *input = static_cast<unsigned char *>(map.pData) + y * map.RowPitch + x * 4;
            auto *output = pixels.data() + (static_cast<size_t>(y) * desc.Width + x) * 4;
            output[0] = input[2];
            output[1] = input[1];
            output[2] = input[0];
            output[3] = 255;
        }
    context->Unmap(copy.Get(), 0);
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = desc.Width;
    info.biHeight = -static_cast<LONG>(desc.Height);
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    std::ofstream output(LogDirectory() / (L"launcher-page-" + std::to_wstring(page) + L".bmp"), std::ios::binary);
    output.write(reinterpret_cast<char *>(&header), sizeof(header));
    output.write(reinterpret_cast<char *>(&info), sizeof(info));
    output.write(reinterpret_cast<char *>(pixels.data()), pixels.size());
}
} // namespace vortex
