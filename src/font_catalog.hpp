#pragma once
#include <Windows.h>
#include <array>
#include <string>
namespace awareness {
struct FontPreset {
    const char *name;
    const wchar_t *file;
};
inline constexpr std::array<FontPreset, 12> FontPresets{{{"Segoe UI", L"segoeui.ttf"},
                                                         {"Bahnschrift", L"bahnschrift.ttf"},
                                                         {"Verdana", L"verdana.ttf"},
                                                         {"Tahoma", L"tahoma.ttf"},
                                                         {"Trebuchet MS", L"trebuc.ttf"},
                                                         {"Consolas", L"consola.ttf"},
                                                         {"Arial", L"arial.ttf"},
                                                         {"Segoe UI Variable", L"SegUIVar.ttf"},
                                                         {"Segoe UI Semibold", L"seguisb.ttf"},
                                                         {"Calibri", L"calibri.ttf"},
                                                         {"Candara", L"candara.ttf"},
                                                         {"Corbel", L"corbel.ttf"}}};
inline std::string FontFile(std::size_t index) {
    if (index >= FontPresets.size())
        return {};
    wchar_t windows[MAX_PATH]{};
    const auto length = GetWindowsDirectoryW(windows, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return {};
    const std::wstring path = std::wstring(windows) + L"\\Fonts\\" + FontPresets[index].file;
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string result(size, '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, result.data(), size, nullptr, nullptr))
        return {};
    result.pop_back();
    return result;
}
} // namespace awareness
