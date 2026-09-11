#pragma once
#include "settings.hpp"
#include "../app/app_paths.hpp"
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <string_view>
#include <filesystem>
#include <string>
#include <vector>

namespace awareness::profiles {
struct State {
    Configuration config;
    VisualOptions visual;
    TrackingConfiguration tracking;
    EffectsConfiguration effects;
};
enum class Operation { None, Refresh, Create, Replace, Load, Duplicate, Rename, Archive, Import, Export, OpenFolder };
struct Entry {
    std::string name;
    std::uintmax_t bytes{};
    std::string modified;
};
struct Request {
    Operation operation{Operation::None};
    std::string name, newName;
    std::filesystem::path path;
};
struct Completion {
    Operation operation{Operation::None};
    bool success{}, cancelled{}, loaded{}, catalogReady{};
    std::string name, message;
    std::vector<Entry> entries;
    State state;
};
struct View {
    std::vector<Entry> entries;
    std::string message{"Save your current setup as a named profile."}, loadedName, selectionRequest;
    bool busy{};
};
inline constexpr std::size_t MaximumProfiles = 256, MaximumProfileBytes = 512 * 1024;
inline bool ValidName(const std::string &name) noexcept {
    try {
        const auto wide = vortex::Wide(name);
        if (wide.empty() || wide.size() > 64 || wide.front() == L' ' || wide.back() == L' ' || wide.back() == L'.')
            return false;
        for (wchar_t c : wide)
            if (c < 32 || c == 127 || std::wstring_view(L"<>:\"/\\|?*").find(c) != std::wstring_view::npos)
                return false;
        auto stem = wide.substr(0, wide.find(L'.'));
        for (auto &c : stem)
            if (c >= L'a' && c <= L'z')
                c -= L'a' - L'A';
        if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" || stem == L"CONIN$" ||
            stem == L"CONOUT$")
            return false;
        if (stem.size() == 4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT")) &&
            ((stem[3] >= L'0' && stem[3] <= L'9') || stem[3] == L'\u00b9' || stem[3] == L'\u00b2' ||
             stem[3] == L'\u00b3'))
            return false;
        return true;
    } catch (...) {
        return false;
    }
}
inline std::string SuggestedName(const std::filesystem::path &source) {
    auto name = source.stem().wstring();
    for (auto &c : name)
        if (c < 32 || c == 127 || std::wstring_view(L"<>:\"/\\|?*").find(c) != std::wstring_view::npos)
            c = L'-';
    while (!name.empty() && (name.front() == L' ' || name.front() == L'.'))
        name.erase(name.begin());
    while (!name.empty() && (name.back() == L' ' || name.back() == L'.'))
        name.pop_back();
    if (name.size() > 48) {
        name.resize(48);
        if (name.back() >= 0xD800 && name.back() <= 0xDBFF)
            name.pop_back();
    }
    auto result = vortex::Utf8(name);
    return ValidName(result) ? result : "Imported profile";
}

// Named snapshots are separate from the working OverlaySettings.ini file. These
// synchronous methods are called by Browser's worker, never by its drawing code.
class Store {
    std::filesystem::path directory_;

    static bool SameName(const std::string &a, const std::string &b) {
        const auto wa = vortex::Wide(a), wb = vortex::Wide(b);
        return CompareStringOrdinal(wa.c_str(), static_cast<int>(wa.size()), wb.c_str(), static_cast<int>(wb.size()),
                                    TRUE) == CSTR_EQUAL;
    }
    void CheckDirectory() const {
        const auto attributes = GetFileAttributesW(directory_.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("The profile folder is unavailable.");
    }
    void CheckFile(const std::filesystem::path &path, bool bounded = true) const {
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            throw std::runtime_error("The selected profile is unavailable.");
        const auto bytes = std::filesystem::file_size(path);
        if (bounded && (!bytes || bytes > MaximumProfileBytes))
            throw std::runtime_error("Profiles must be valid INI files smaller than 512 KB.");
    }
    void CheckCapacity() const {
        if (List().size() >= MaximumProfiles)
            throw std::runtime_error("The library holds 256 profiles. Archive one before adding another.");
    }

  public:
    explicit Store(std::filesystem::path directory = vortex::DataDirectory() / L"profiles")
        : directory_(std::filesystem::absolute(std::move(directory)).lexically_normal()) {
        std::filesystem::create_directories(directory_);
        CheckDirectory();
    }
    const std::filesystem::path &Directory() const noexcept { return directory_; }
    std::filesystem::path Path(const std::string &name) const {
        CheckDirectory();
        if (!ValidName(name))
            throw std::runtime_error("Use 1-64 characters without slashes, reserved names, or trailing spaces.");
        return directory_ / (vortex::Wide(name) + L".ini");
    }
    std::vector<Entry> List() const {
        CheckDirectory();
        std::vector<Entry> result;
        result.reserve(16);
        std::size_t visited{};
        for (const auto &entry : std::filesystem::directory_iterator(directory_)) {
            if (++visited > 4096)
                throw std::runtime_error("The profile folder has too many files to list safely.");
            auto extension = entry.path().extension().wstring();
            for (auto &c : extension)
                c = static_cast<wchar_t>(towlower(c));
            if (extension != L".ini")
                continue;
            const auto name = vortex::Utf8(entry.path().stem().wstring());
            if (!ValidName(name))
                continue;
            const auto attributes = GetFileAttributesW(entry.path().c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                continue;
            if (result.size() >= MaximumProfiles)
                throw std::runtime_error("The profile library exceeds its 256-profile limit.");
            std::error_code sizeError;
            const auto bytes = entry.file_size(sizeError);
            if (sizeError)
                continue;
            Entry item{name, bytes, {}};
            WIN32_FILE_ATTRIBUTE_DATA data{};
            SYSTEMTIME utc{}, local{};
            wchar_t date[64]{};
            if (GetFileAttributesExW(entry.path().c_str(), GetFileExInfoStandard, &data) &&
                FileTimeToSystemTime(&data.ftLastWriteTime, &utc) &&
                SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
                swprintf_s(date, L"%04u-%02u-%02u %02u:%02u", local.wYear, local.wMonth, local.wDay, local.wHour,
                           local.wMinute);
                item.modified = vortex::Utf8(date);
            }
            result.push_back(std::move(item));
        }
        std::sort(result.begin(), result.end(), [](const Entry &a, const Entry &b) {
            const auto wa = vortex::Wide(a.name), wb = vortex::Wide(b.name);
            return CompareStringOrdinal(wa.c_str(), -1, wb.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        });
        return result;
    }
    State Load(const std::string &name, float units = 1.f) const {
        const auto path = Path(name);
        CheckFile(path);
        State state;
        state.config.worldUnitsPerMeter = units;
        if (!LoadSettings(path.wstring(), state.config, state.visual, &state.tracking, &state.effects))
            throw std::runtime_error("This profile is invalid or from an unsupported version. Current settings kept.");
        return state;
    }
    void Save(const std::string &name, const State &state, bool replace = false) const {
        const auto path = Path(name);
        if (replace)
            CheckFile(path, false);
        else
            CheckCapacity();
        if (!SaveSettings(path.wstring(), state.config, state.visual, state.tracking, state.effects, replace))
            throw std::runtime_error(replace ? "The profile could not be saved. Its previous version was kept."
                                             : "That name already exists, or the profile could not be created.");
    }
    std::string UniqueName(const std::string &suggested) const {
        const auto base = ValidName(suggested) ? suggested : std::string("New profile");
        if (!std::filesystem::exists(Path(base)))
            return base;
        auto wide = vortex::Wide(base);
        if (wide.size() > 52) {
            wide.resize(52);
            if (wide.back() >= 0xD800 && wide.back() <= 0xDBFF)
                wide.pop_back();
        }
        for (unsigned i = 2; i < 10000; ++i) {
            auto name = vortex::Utf8(wide) + " (" + std::to_string(i) + ")";
            if (!std::filesystem::exists(Path(name)))
                return name;
        }
        throw std::runtime_error("Choose a different profile name.");
    }
    std::string Duplicate(const std::string &name) const {
        const auto state = Load(name);
        auto shortName = vortex::Wide(name);
        if (shortName.size() > 54) {
            shortName.resize(54);
            if (shortName.back() >= 0xD800 && shortName.back() <= 0xDBFF)
                shortName.pop_back();
        }
        auto duplicate = UniqueName(vortex::Utf8(shortName) + " copy");
        Save(duplicate, state);
        return duplicate;
    }
    void Rename(const std::string &name, const std::string &newName) const {
        const auto source = Path(name), destination = Path(newName);
        CheckFile(source, false);
        if (SameName(name, newName))
            throw std::runtime_error("Choose a different profile name.");
        // MoveFileEx without REPLACE_EXISTING makes the collision check atomic.
        if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("That name already exists, or the profile could not be renamed.");
    }
    void Archive(const std::string &name) const {
        const auto source = Path(name);
        CheckFile(source, false);
        const auto id = std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(GetCurrentProcessId());
        // Stay in the same directory so relative media references remain valid.
        // The hidden extension removes the snapshot from the library without deleting it.
        const auto destination = directory_ / (vortex::Wide(name) + L"-" + id + L".ini.archived");
        if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("The profile could not be archived.");
    }
    std::string Import(const std::filesystem::path &path, const std::string &requestedName = {}) const {
        if (path.empty() || !path.is_absolute())
            throw std::runtime_error("Choose an absolute path to a Vortex INI profile.");
        CheckFile(path);
        State state;
        if (!LoadSettings(path.wstring(), state.config, state.visual, &state.tracking, &state.effects))
            throw std::runtime_error("The import is not a valid Vortex profile. Your settings were kept.");
        const auto name = UniqueName(requestedName.empty() ? SuggestedName(path) : requestedName);
        Save(name, state);
        return name;
    }
    void Export(const std::string &name, const std::filesystem::path &destination) const {
        if (destination.empty() || !destination.is_absolute() || destination.filename().empty())
            throw std::runtime_error("Choose an absolute path for the exported profile.");
        const auto state = Load(name);
        if (!SaveSettings(destination.wstring(), state.config, state.visual, state.tracking, state.effects, false))
            throw std::runtime_error("Choose a new filename in a writable folder. Existing exports are kept.");
    }
};
} // namespace awareness::profiles
