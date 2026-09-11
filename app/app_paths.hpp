#pragma once
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace vortex {
inline std::filesystem::path ModuleDirectory(HMODULE module = nullptr) {
    std::wstring path(32768, L'\0');
    const DWORD size = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size())
        throw std::runtime_error("Cannot locate application files.");
    path.resize(size);
    return std::filesystem::path(path).parent_path();
}
inline std::filesystem::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR value{};
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &value)))
        throw std::runtime_error("Cannot locate your Windows profile.");
    std::filesystem::path result(value);
    CoTaskMemFree(value);
    return result;
}
inline std::filesystem::path DataDirectory() {
    // Explicit override is also used by smoke tests so they never touch a user's profile.
    wchar_t overridePath[32768]{};
    const auto count = GetEnvironmentVariableW(L"VORTEX_DATA_DIR", overridePath, 32768);
    auto path =
        count && count < 32768 ? std::filesystem::path(overridePath) : KnownFolder(FOLDERID_LocalAppData) / L"Vortex";
    if (!path.is_absolute())
        throw std::runtime_error("VORTEX_DATA_DIR must be an absolute path.");
    std::filesystem::create_directories(path);
    return path;
}
inline std::filesystem::path LogDirectory() {
    auto path = DataDirectory() / L"logs";
    std::filesystem::create_directories(path);
    return path;
}
inline std::string Utf8(const std::wstring &text) {
    if (text.empty())
        return {};
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr,
                                    0, nullptr, nullptr);
    if (!count)
        throw std::runtime_error("Invalid Unicode text.");
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count, nullptr, nullptr);
    return result;
}
inline std::wstring Wide(const std::string &text) {
    if (text.empty())
        return {};
    int count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!count)
        throw std::runtime_error("Invalid UTF-8 text.");
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}
inline std::string WindowsError(const char *operation, DWORD error = GetLastError()) {
    wchar_t message[1024]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, message, 1024,
                   nullptr);
    return std::string(operation) + ": " + Utf8(message) + " (" + std::to_string(error) + ")";
}
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
} // namespace vortex
