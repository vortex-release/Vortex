#pragma once
#include "image_texture.hpp"
#include "settings.hpp"
#include <commdlg.h>
#include <future>
#include <atomic>
#include <chrono>
#include <string>
#include <utility>

namespace awareness {
// Disk decoding and the native picker never run on Present. GPU creation stays on Present.
class MenuBackground {
    struct Result {
        std::string path;
        ImagePixels pixels;
        HRESULT result{S_FALSE};
        bool picked{};
    };
    std::future<Result> pending_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
    std::string requested_, observed_;
    ULONGLONG loadAt_{};
    std::atomic<HWND> dialog_{};
    std::atomic_bool stopping_{};
    bool browse_{};
    UINT width_{}, height_{};
    HRESULT result_{S_FALSE};
    static UINT_PTR CALLBACK Hook(HWND window, UINT message, WPARAM, LPARAM data) {
        if (message == WM_INITDIALOG) {
            auto *ofn = reinterpret_cast<OPENFILENAMEW *>(data);
            auto *self = reinterpret_cast<MenuBackground *>(ofn->lCustData);
            self->dialog_ = GetParent(window);
            if (self->stopping_)
                PostMessageW(self->dialog_, WM_CLOSE, 0, 0);
        }
        return 0;
    }
    Result Read(std::string path, bool browse) {
        Result value;
        wchar_t wide[2048]{};
        if (browse) {
            const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            struct ComScope {
                bool active;
                ~ComScope() {
                    if (active)
                        CoUninitialize();
                }
            } comScope{SUCCEEDED(com)};
            OPENFILENAMEW picker{sizeof(picker)};
            picker.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp\0All files\0*.*\0";
            picker.lpstrFile = wide;
            picker.nMaxFile = 2048;
            picker.lpstrTitle = L"Choose background";
            picker.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ENABLEHOOK;
            picker.lpfnHook = Hook;
            picker.lCustData = reinterpret_cast<LPARAM>(this);
            const bool selected = !stopping_ && GetOpenFileNameW(&picker);
            dialog_ = nullptr;
            if (!selected)
                return value;
            char utf8[2048]{};
            if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, utf8, sizeof(utf8), nullptr, nullptr)) {
                value.result = E_INVALIDARG;
                return value;
            }
            path = utf8;
            value.picked = true;
        } else if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, wide, 2048)) {
            value.result = E_INVALIDARG;
            return value;
        }
        value.path = std::move(path);
        value.result = DecodeImage(wide, {}, value.pixels);
        return value;
    }

  public:
    ~MenuBackground() {
        stopping_ = true;
        if (HWND dialog = dialog_.load())
            PostMessageW(dialog, WM_CLOSE, 0, 0);
        if (pending_.valid())
            pending_.wait();
    }
    void Browse() noexcept { browse_ = true; }
    void Reload() {
        requested_ = "\x01";
        loadAt_ = 0;
    }
    bool Update(ID3D11Device *device, VisualOptions &visual) {
        bool changed = false;
        if (observed_ != visual.backgroundPath) {
            observed_ = visual.backgroundPath;
            loadAt_ = GetTickCount64() + 250;
            view_.Reset();
            result_ = S_FALSE;
        }
        if (pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto data = pending_.get();
                if (data.picked && SUCCEEDED(data.result)) {
                    strcpy_s(visual.backgroundPath, data.path.c_str());
                    visual.backgroundEnabled = 1;
                    requested_ = data.path;
                    observed_ = data.path;
                    changed = true;
                }
                if (!data.path.empty() && data.path == visual.backgroundPath) {
                    result_ = data.result;
                    view_.Reset();
                    if (SUCCEEDED(result_))
                        result_ = UploadImage(device, data.pixels, view_);
                    width_ = data.pixels.width;
                    height_ = data.pixels.height;
                } else if (FAILED(data.result))
                    result_ = data.result;
            } catch (...) {
                result_ = E_FAIL;
            }
        }
        if (!pending_.valid() && (browse_ || (requested_ != visual.backgroundPath && GetTickCount64() >= loadAt_))) {
            requested_ = visual.backgroundPath;
            const bool browse = std::exchange(browse_, false);
            if (!browse) {
                view_.Reset();
                result_ = S_FALSE;
            }
            if (browse || !requested_.empty()) {
                try {
                    pending_ = std::async(std::launch::async,
                                          [this, path = requested_, browse] { return Read(path, browse); });
                } catch (...) {
                    result_ = E_OUTOFMEMORY;
                }
            }
        }
        return changed;
    }
    ID3D11ShaderResourceView *View() const noexcept { return view_.Get(); }
    UINT Width() const noexcept { return width_; }
    UINT Height() const noexcept { return height_; }
    const char *Status() const noexcept {
        return pending_.valid() ? "Loading..." : FAILED(result_) ? "Couldn't open this image." : "";
    }
};
} // namespace awareness
