#pragma once
#include "profile_store.hpp"
#include <commdlg.h>
#include <shellapi.h>
#include <atomic>
#include <future>

namespace awareness::profiles {
// One user operation at a time. A native picker can be cancelled on shutdown;
// no file enumeration, parsing, saving or dialog wait runs in the render loop.
class Browser {
    std::future<Completion> pending_;
    std::atomic_bool stopping_{};
    std::atomic<HWND> dialog_{};
    std::filesystem::path directory_;
    static UINT_PTR CALLBACK PickerHook(HWND window, UINT message, WPARAM, LPARAM data) {
        if (message == WM_INITDIALOG) {
            auto *self = reinterpret_cast<Browser *>(reinterpret_cast<OPENFILENAMEW *>(data)->lCustData);
            self->dialog_ = GetParent(window);
            if (self->stopping_)
                PostMessageW(self->dialog_, WM_CLOSE, 0, 0);
        }
        return 0;
    }
    std::filesystem::path Pick(bool save, const std::string &name) {
        const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        struct ComScope {
            HRESULT value;
            ~ComScope() {
                if (SUCCEEDED(value))
                    CoUninitialize();
            }
        } scope{com};
        wchar_t file[32768]{};
        if (save && ValidName(name))
            wcsncpy_s(file, (vortex::Wide(name) + L".ini").c_str(), _TRUNCATE);
        OPENFILENAMEW picker{sizeof(picker)};
        picker.lpstrFile = file;
        picker.nMaxFile = static_cast<DWORD>(std::size(file));
        picker.lpstrTitle = save ? L"Export Vortex profile - choose a new filename" : L"Import Vortex profile";
        picker.lpstrFilter = L"Vortex profiles\0*.ini\0All files\0*.*\0";
        picker.lpstrDefExt = L"ini";
        picker.Flags =
            OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ENABLEHOOK | (save ? 0 : OFN_FILEMUSTEXIST);
        picker.lpfnHook = PickerHook;
        picker.lCustData = reinterpret_cast<LPARAM>(this);
        const bool selected = !stopping_ && (save ? GetSaveFileNameW(&picker) : GetOpenFileNameW(&picker));
        dialog_ = nullptr;
        if (!selected) {
            if (CommDlgExtendedError())
                throw std::runtime_error("Windows could not open the file picker. Try again.");
            return {};
        }
        return file;
    }
    Completion Run(Request request, State state) noexcept {
        Completion result;
        result.operation = request.operation;
        try {
            result.name = request.name;
            if (stopping_) {
                result.cancelled = true;
                return result;
            }
            Store store(directory_.empty() ? vortex::DataDirectory() / L"profiles" : directory_);
            if ((request.operation == Operation::Import || request.operation == Operation::Export) &&
                request.path.empty())
                request.path = Pick(request.operation == Operation::Export, request.name);
            if (stopping_ || ((request.operation == Operation::Import || request.operation == Operation::Export) &&
                              request.path.empty())) {
                result.cancelled = true;
                result.message = "No files changed.";
            } else {
                switch (request.operation) {
                case Operation::Refresh:
                    result.message = "Profile library refreshed.";
                    break;
                case Operation::Create:
                    store.Save(request.name, state);
                    result.message = "Saved \"" + request.name + "\".";
                    break;
                case Operation::Replace:
                    store.Save(request.name, state, true);
                    result.message = "Updated \"" + request.name + "\" with your current settings.";
                    break;
                case Operation::Load:
                    result.state = store.Load(request.name, state.config.worldUnitsPerMeter);
                    result.loaded = true;
                    result.message = "Loaded \"" + request.name + "\".";
                    break;
                case Operation::Duplicate:
                    result.name = store.Duplicate(request.name);
                    result.message = "Created \"" + result.name + "\".";
                    break;
                case Operation::Rename:
                    store.Rename(request.name, request.newName);
                    result.name = request.newName;
                    result.message = "Renamed to \"" + result.name + "\".";
                    break;
                case Operation::Archive:
                    store.Archive(request.name);
                    result.message =
                        "Archived \"" + request.name + "\". It can be imported again from the profile folder.";
                    break;
                case Operation::Import:
                    result.name = store.Import(request.path, request.name);
                    result.message = "Imported \"" + result.name + "\". Select it and choose Load profile to apply it.";
                    break;
                case Operation::Export:
                    store.Export(request.name, request.path);
                    result.message = "Exported \"" + request.name + "\". Custom media paths are preserved.";
                    break;
                case Operation::OpenFolder:
                    if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", store.Directory().c_str(), nullptr,
                                                                nullptr, SW_SHOWNORMAL)) <= 32)
                        throw std::runtime_error("Windows could not open the profile folder.");
                    result.message = "Opened your profile folder.";
                    break;
                default:
                    throw std::runtime_error("Choose a profile action.");
                }
                result.success = true;
            }
            try {
                result.entries = store.List();
                result.catalogReady = true;
            } catch (...) {
                result.message += " Use Refresh to update the library list.";
            }
        } catch (const std::exception &error) {
            result.success = result.loaded = false;
            result.message = error.what();
            try {
                Store store(directory_.empty() ? vortex::DataDirectory() / L"profiles" : directory_);
                result.entries = store.List();
                result.catalogReady = true;
            } catch (...) {
            }
        } catch (...) {
            result.success = result.loaded = false;
            result.message = "The profile operation could not finish. Your current settings were kept.";
        }
        return result;
    }

  public:
    explicit Browser(std::filesystem::path directory = {}) : directory_(std::move(directory)) {}
    Browser(const Browser &) = delete;
    Browser &operator=(const Browser &) = delete;
    ~Browser() {
        stopping_ = true;
        if (const auto window = dialog_.load())
            PostMessageW(window, WM_CLOSE, 0, 0);
        if (pending_.valid())
            pending_.wait();
    }
    bool Busy() const noexcept { return pending_.valid(); }
    bool Start(Request request, State state = {}) noexcept {
        if (Busy() || request.operation == Operation::None || stopping_)
            return false;
        try {
            pending_ = std::async(std::launch::async, [this, request = std::move(request), state]() mutable {
                return Run(std::move(request), state);
            });
            return true;
        } catch (...) {
            return false;
        }
    }
    bool Poll(Completion &result) noexcept {
        if (!pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return false;
        try {
            result = pending_.get();
        } catch (...) {
            result = {};
            result.message = "The profile operation could not finish. Try again.";
        }
        return true;
    }
};
} // namespace awareness::profiles
