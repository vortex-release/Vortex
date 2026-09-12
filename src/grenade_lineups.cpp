#include "grenade_lineups.hpp"
#include "../dependencies/nlohmann/json.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <cstring>
#include <chrono>
#include <shobjidl.h>
#include <shellapi.h>
#include "../app/app_paths.hpp"
#include <stdexcept>

namespace awareness::lineups {
namespace {
using Json = nlohmann::json;
bool Number(float value, float low, float high) noexcept {
    return std::isfinite(value) && value >= low && value <= high;
}
bool Text(std::string_view text, std::size_t maximum, bool required = true) noexcept {
    if ((required && text.empty()) || text.size() > maximum)
        return false;
    for (unsigned char ch : text)
        if (ch < 32 || ch == 127)
            return false;
    return true;
}
bool Position(Vector3 p) noexcept {
    return Number(p.x, -65536, 65536) && Number(p.y, -65536, 65536) && Number(p.z, -65536, 65536);
}
bool Map(std::string_view text) noexcept {
    if (text.empty() || text.size() > 95)
        return false;
    for (char ch : text)
        if (!(ch >= 'a' && ch <= 'z') && !(ch >= '0' && ch <= '9') && ch != '_' && ch != '-')
            return false;
    return true;
}
float Yaw(float value) noexcept {
    return std::remainder(value, 360.f);
}
bool Duplicate(const Record &a, const Record &b) noexcept {
    return a.name == b.name && a.map == b.map && a.kind == b.kind && a.throwType == b.throwType &&
           a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z &&
           a.pitch == b.pitch && a.yaw == b.yaw && a.eyeHeight == b.eyeHeight && a.notes == b.notes;
}
unsigned Unsigned(const Json &value, unsigned maximum) {
    if (!value.is_number_integer() || value < 0 || value > maximum)
        throw std::runtime_error("A lineup has an invalid category or throw type.");
    return value.get<unsigned>();
}
Vector3 Vector(const Json &value) {
    if (!value.is_array() || value.size() != 3)
        throw std::runtime_error("A lineup position needs three coordinates.");
    return {value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
}
struct File {
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit File(HANDLE handle) : value(handle) {}
    ~File() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    File(const File &) = delete;
    File &operator=(const File &) = delete;
};
} // namespace
Kind WeaponKind(std::uint32_t weapon) noexcept {
    switch (weapon) {
    case 43:
        return Kind::Flash;
    case 44:
        return Kind::HE;
    case 45:
        return Kind::Smoke;
    case 46:
    case 48:
        return Kind::Fire;
    case 47:
        return Kind::Decoy;
    default:
        return Kind::Any;
    }
}
std::string NormalizeMap(std::string_view name) {
    if (name.empty() || name.size() > 512)
        return {};
    auto slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos)
        name.remove_prefix(slash + 1);
    std::string result(name);
    for (char &ch : result)
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
    for (std::string_view extension : {".vmap_c", ".vmap", ".bsp"})
        if (result.ends_with(extension)) {
            result.resize(result.size() - extension.size());
            break;
        }
    return Map(result) ? result : std::string{};
}
bool Valid(const Record &r) noexcept {
    return Text(r.name, 95) && Text(r.notes, 255, false) && Map(r.map) && static_cast<unsigned>(r.kind) <= 5 &&
           r.throwType < std::size(ThrowNames) && Position(r.position) && Number(r.pitch, -89, 89) &&
           Number(r.yaw, -180, 180) && Number(r.eyeHeight, 20, 90);
}
bool Fresh(const Capture &c, double now) noexcept {
    return c.valid && Position(c.feet) && Position(c.eye) && Number(c.pitch, -89, 89) &&
           Number(c.yaw, -360000, 360000) && std::isfinite(now) && std::isfinite(c.time) && now >= c.time &&
           now - c.time <= .25;
}
std::string_view MapFor(const Capture &c, const Options &o) noexcept {
    if (!c.map.empty())
        return c.map;
    const auto *end = static_cast<const char *>(std::memchr(o.mapOverride, 0, sizeof(o.mapOverride)));
    return end ? std::string_view(o.mapOverride, static_cast<std::size_t>(end - o.mapOverride)) : std::string_view{};
}
Record MakeRecord(const Capture &c, std::string name, std::uint32_t type, std::string notes) {
    if (!Fresh(c, c.time) || !Map(c.map) || WeaponKind(c.weapon) == Kind::Any)
        throw std::runtime_error("Hold a grenade in a loaded map to capture a lineup.");
    Record r;
    r.name = std::move(name);
    r.map = c.map;
    r.notes = std::move(notes);
    r.kind = WeaponKind(c.weapon);
    r.throwType = type;
    r.position = c.feet;
    r.pitch = c.pitch;
    r.yaw = Yaw(c.yaw);
    r.eyeHeight = c.eye.z - c.feet.z;
    if (!Valid(r))
        throw std::runtime_error("The lineup name, instructions, or capture position is invalid.");
    return r;
}
Vector3 AimPoint(const Record &r) noexcept {
    constexpr float radians = .0174532925199433f;
    const float pitch = r.pitch * radians, yaw = r.yaw * radians;
    return r.position + Vector3{std::cos(pitch) * std::cos(yaw) * 8192, std::cos(pitch) * std::sin(yaw) * 8192,
                                r.eyeHeight - std::sin(pitch) * 8192};
}
Selection Select(std::span<const Record> records, const Capture &c, const Options &o, float units,
                 double now) noexcept {
    Selection result;
    if (!o.enabled || !Valid(o) || !Fresh(c, now) || !Number(units, .001f, 1000))
        return result;
    const auto map = MapFor(c, o);
    if (!Map(map))
        return result;
    const auto held = WeaponKind(c.weapon);
    if (o.heldOnly && held == Kind::Any)
        return result;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto &r = records[i];
        if (!r.enabled || r.map != map || (o.heldOnly && r.kind != Kind::Any && r.kind != held))
            continue;
        const float distance = Distance(r.position, c.feet);
        if (!std::isfinite(distance) || distance > o.range * units)
            continue;
        Guide g{i, distance / units, std::hypot(r.pitch - c.pitch, Yaw(r.yaw - c.yaw)), distance <= o.standTolerance,
                false};
        g.aligned = g.atStand && g.angle <= o.aimTolerance;
        std::size_t at{};
        while (at < result.count && (result.guides[at].distance < g.distance ||
                                     (result.guides[at].distance == g.distance && result.guides[at].angle <= g.angle)))
            ++at;
        if (at >= result.guides.size())
            continue;
        const auto end = std::min(result.count, result.guides.size() - 1);
        for (auto move = end; move > at; --move)
            result.guides[move] = result.guides[move - 1];
        result.guides[at] = g;
        result.count = std::min(result.count + 1, result.guides.size());
    }
    return result;
}
const Record *Library::Find(std::uint64_t id) const noexcept {
    for (const auto &r : records_)
        if (r.id == id)
            return &r;
    return nullptr;
}
std::uint64_t Library::Add(Record r) {
    r.map = NormalizeMap(r.map);
    if (!Valid(r))
        throw std::runtime_error("Invalid lineup.");
    if (records_.size() >= MaximumRecords)
        throw std::runtime_error("The lineup library is full (2048 entries).");
    r.id = nextId_++;
    records_.push_back(std::move(r));
    return records_.back().id;
}
bool Library::Update(Record r) {
    r.map = NormalizeMap(r.map);
    if (!Valid(r))
        return false;
    for (auto &current : records_)
        if (current.id == r.id) {
            current = std::move(r);
            return true;
        }
    return false;
}
bool Library::Remove(std::uint64_t id) {
    const auto before = records_.size();
    std::erase_if(records_, [id](const auto &r) { return r.id == id; });
    return records_.size() != before;
}
std::size_t Library::Parse(std::string_view data, bool merge) {
    if (data.empty() || data.size() > MaximumBytes)
        throw std::runtime_error("Lineup JSON must be smaller than 2 MB.");
    unsigned depth{};
    // A depth cap prevents pathological JSON from exhausting the parser stack.
    auto callback = [&depth](int level, Json::parse_event_t, Json &) {
        depth = std::max(depth, static_cast<unsigned>(level));
        if (depth > 16)
            throw std::runtime_error("Lineup JSON nesting is too deep.");
        return true;
    };
    const auto json = Json::parse(data, callback);
    const bool legacy = json.is_array();
    if (!legacy &&
        (!json.is_object() || !json.contains("version") || json.at("version") != 1 || !json.contains("lineups")))
        throw std::runtime_error("Unsupported lineup file. Use Vortex JSON or Lefrizzel Ai lineups.json.");
    const auto &entries = legacy ? json : json.at("lineups");
    if (!entries.is_array() || entries.size() > MaximumRecords)
        throw std::runtime_error("The lineup file exceeds 2048 entries.");
    Library next = merge ? *this : Library{};
    std::size_t added{};
    for (const auto &entry : entries) {
        Record r;
        r.name = entry.at("name").get<std::string>();
        r.map = NormalizeMap(entry.at("map").get<std::string>());
        r.kind = static_cast<Kind>(Unsigned(entry.at("kind"), 5));
        r.throwType = entry.contains("throw") ? Unsigned(entry.at("throw"), 14) : 0;
        r.enabled = entry.value("enabled", true);
        r.position = Vector(entry.at("pos"));
        const auto angles = Vector(entry.at("ang"));
        r.pitch = angles.x;
        r.yaw = angles.y;
        // Anthony stores captured eye height in ang.z. Older packs use roll=0.
        r.eyeHeight = legacy ? (angles.z == 0 ? 64 : angles.z) : entry.value("eyeHeight", 64.f);
        r.notes = entry.value(legacy ? "inputs" : "notes", std::string{});
        if (!Valid(r))
            throw std::runtime_error("A lineup contains invalid names, angles, map, or coordinates.");
        bool duplicate = false;
        for (const auto &present : next.records_)
            if (Duplicate(r, present)) {
                duplicate = true;
                break;
            }
        if (!duplicate) {
            next.Add(std::move(r));
            ++added;
        }
    }
    *this = std::move(next);
    return added;
}
std::string Library::Serialize() const {
    Json entries = Json::array();
    for (const auto &r : records_) {
        if (!Valid(r))
            throw std::runtime_error("The lineup library contains an invalid entry.");
        entries.push_back({{"name", r.name},
                           {"map", r.map},
                           {"kind", static_cast<unsigned>(r.kind)},
                           {"throw", r.throwType},
                           {"enabled", r.enabled},
                           {"pos", {r.position.x, r.position.y, r.position.z}},
                           {"ang", {r.pitch, r.yaw, 0}},
                           {"eyeHeight", r.eyeHeight},
                           {"notes", r.notes}});
    }
    auto result = Json({{"version", 1}, {"lineups", std::move(entries)}}).dump(2);
    if (result.size() > MaximumBytes)
        throw std::runtime_error("The lineup library exceeds its 2 MB file limit.");
    return result;
}
std::size_t Library::Load(const std::filesystem::path &path, bool merge) {
    File input(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (input.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(input.value, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || info.nFileSizeHigh ||
        !info.nFileSizeLow || info.nFileSizeLow > MaximumBytes)
        throw std::runtime_error("The selected lineup file is unavailable or larger than 2 MB.");
    std::string data(info.nFileSizeLow, '\0');
    DWORD read{};
    if (!ReadFile(input.value, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) || read != data.size())
        throw std::runtime_error("The lineup file could not be read completely.");
    return Parse(data, merge);
}
void Library::Save(const std::filesystem::path &path) const {
    const auto data = Serialize();
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    const auto attributes = GetFileAttributesW(absolute.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        throw std::runtime_error("The destination must be a regular JSON file.");
    static std::atomic<unsigned> serial{};
    auto temporary = absolute;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++serial);
    bool created = false;
    try {
        {
            File output(
                CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (output.value == INVALID_HANDLE_VALUE)
                throw std::runtime_error("The lineup destination is not writable.");
            created = true;
            DWORD written{};
            if (!WriteFile(output.value, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) ||
                written != data.size() || !FlushFileBuffers(output.value))
                throw std::runtime_error("The lineup file could not be saved completely.");
        }
        if (!MoveFileExW(temporary.c_str(), absolute.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("The previous lineup file could not be replaced.");
    } catch (...) {
        if (created)
            DeleteFileW(temporary.c_str());
        throw;
    }
}

namespace {
struct ComInit {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ~ComInit() {
        if (SUCCEEDED(result))
            CoUninitialize();
    }
};
thread_local IFileDialog *activePicker{};
thread_local const std::atomic<bool> *pickerStopping{};
void CALLBACK CancelPicker(HWND, UINT, UINT_PTR, DWORD) {
    // IFileDialog::Show pumps this worker apartment's messages. Close must run
    // here, never from the render thread through an unmarshalled COM pointer.
    if (activePicker && pickerStopping && pickerStopping->load(std::memory_order_relaxed))
        activePicker->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
}
struct PickerCancelScope {
    UINT_PTR timer{};
    PickerCancelScope(IFileDialog *dialog, const std::atomic<bool> &stopping) {
        activePicker = dialog;
        pickerStopping = &stopping;
        timer = SetTimer(nullptr, 0, 50, CancelPicker);
    }
    ~PickerCancelScope() {
        if (timer)
            KillTimer(nullptr, timer);
        activePicker = nullptr;
        pickerStopping = nullptr;
    }
};
std::filesystem::path PickJson(bool save, const std::atomic<bool> &stopping) {
    ComInit com;
    if (FAILED(com.result))
        throw std::runtime_error("The file picker could not start.");
    IFileDialog *raw{};
    const auto hr = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&raw));
    if (FAILED(hr))
        throw std::runtime_error("The file picker is unavailable.");
    const auto release = [](auto *value) {
        if (value)
            value->Release();
    };
    std::unique_ptr<IFileDialog, decltype(release)> dialog(raw, release);
    const COMDLG_FILTERSPEC filter[]{{L"Lineup JSON", L"*.json"}};
    raw->SetFileTypes(1, filter);
    raw->SetDefaultExtension(L"json");
    raw->SetTitle(save ? L"Export Vortex lineups" : L"Import lineups");
    DWORD flags{};
    raw->GetOptions(&flags);
    raw->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
    if (save)
        raw->SetFileName(L"Vortex lineups.json");
    PickerCancelScope cancel(raw, stopping);
    if (!cancel.timer)
        throw std::runtime_error("The file picker could not initialize cancellation.");
    if (stopping.load(std::memory_order_relaxed))
        return {};
    const auto shown = raw->Show(nullptr);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    if (FAILED(shown))
        throw std::runtime_error("The file picker did not complete.");
    IShellItem *itemRaw{};
    if (FAILED(raw->GetResult(&itemRaw)))
        throw std::runtime_error("No file was selected.");
    std::unique_ptr<IShellItem, decltype(release)> item(itemRaw, release);
    PWSTR name{};
    if (FAILED(itemRaw->GetDisplayName(SIGDN_FILESYSPATH, &name)))
        throw std::runtime_error("The selected file has no local path.");
    std::filesystem::path result(name);
    CoTaskMemFree(name);
    return result;
}
} // namespace
void Controller::Stop() noexcept {
    stopping_->store(true, std::memory_order_relaxed);
    try {
        if (pending_.valid())
            pending_.wait();
    } catch (...) {
    }
}
void Controller::Start(std::filesystem::path directory) {
    if (started_ || stopping_->load(std::memory_order_relaxed))
        return;
    directory_ = std::move(directory);
    started_ = true;
    Submit({Operation::Reload});
}
void Controller::Tick() {
    if (!started_)
        Start({});
    if (!pending_.valid() || pending_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;
    try {
        auto result = pending_.get();
        if (result.publish) {
            library_ = std::move(result.library);
            ++revision_;
        }
        message_ = std::move(result.message);
    } catch (const std::exception &error) {
        message_ = error.what();
    }
}
void Controller::Submit(Request request) {
    if (Busy() || !started_ || stopping_->load(std::memory_order_relaxed) || request.operation == Operation::None)
        return;
    auto working = library_;
    auto directory = directory_;
    auto stopping = stopping_;
    try {
        pending_ =
            std::async(std::launch::async, [working = std::move(working), directory = std::move(directory),
                                            request = std::move(request), stopping = std::move(stopping)]() mutable {
                Completion result;
                try {
                    if (stopping->load(std::memory_order_relaxed))
                        return result;
                    if (directory.empty())
                        directory = vortex::DataDirectory() / L"lineups";
                    std::filesystem::create_directories(directory);
                    const auto attrs = GetFileAttributesW(directory.c_str());
                    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY) ||
                        (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
                        throw std::runtime_error("The lineup folder is unavailable.");
                    const auto current = directory / L"lineups.json";
                    bool save = false;
                    switch (request.operation) {
                    case Operation::Capture:
                        working.Add(std::move(request.record));
                        save = true;
                        result.message = "Lineup saved.";
                        break;
                    case Operation::Update:
                        if (!working.Update(std::move(request.record)))
                            throw std::runtime_error("The lineup could not be updated.");
                        save = true;
                        result.message = "Lineup updated.";
                        break;
                    case Operation::Remove:
                        if (!working.Remove(request.record.id))
                            throw std::runtime_error("The lineup is no longer available.");
                        save = true;
                        result.message = "Lineup removed.";
                        break;
                    case Operation::Reload:
                        if (std::filesystem::exists(current))
                            working.Load(current);
                        else
                            working = Library{};
                        result.message = "Library loaded.";
                        result.publish = true;
                        break;
                    case Operation::Import: {
                        auto path = request.path.empty() ? PickJson(false, *stopping) : request.path;
                        if (path.empty()) {
                            result.message = "Import cancelled.";
                            return result;
                        }
                        const auto count = working.Load(path, true);
                        save = true;
                        result.message = std::to_string(count) + " lineups imported.";
                        break;
                    }
                    case Operation::Export: {
                        auto path = request.path.empty() ? PickJson(true, *stopping) : request.path;
                        if (path.empty()) {
                            result.message = "Export cancelled.";
                            return result;
                        }
                        working.Save(path);
                        result.message = "Library exported.";
                        break;
                    }
                    case Operation::OpenFolder:
                        if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr,
                                                                    nullptr, SW_SHOWNORMAL)) <= 32)
                            throw std::runtime_error("The lineup folder could not be opened.");
                        break;
                    default:
                        break;
                    }
                    if (save) {
                        working.Save(current);
                        result.publish = true;
                    }
                    if (result.publish)
                        result.library = std::move(working);
                } catch (const std::exception &error) {
                    result.publish = false;
                    result.message = error.what();
                }
                return result;
            });
    } catch (const std::exception &error) {
        message_ = error.what();
    }
}
} // namespace awareness::lineups
