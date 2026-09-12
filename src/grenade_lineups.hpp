#pragma once
#include <awareness/OverlayApi.hpp>
#include <array>
#include <filesystem>
#include <future>
#include <atomic>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Lineup workflow and legacy interchange adapted from Lefrizzel Ai (MIT).
// See licenses/Lefrizzel-Ai-LICENSE.txt and docs/anthony-feature-inventory.md.
namespace awareness::lineups {
inline constexpr std::size_t MaximumRecords = 2048, MaximumBytes = 2 * 1024 * 1024;
enum class Kind : std::uint32_t { Any, HE, Flash, Smoke, Fire, Decoy };
inline constexpr const char *KindNames[]{"Any grenade", "HE grenade",           "Flashbang",
                                         "Smoke",       "Molotov / incendiary", "Decoy"};
inline constexpr const char *ThrowNames[]{"Stand",       "Jump",        "Walk",          "Run",
                                          "Crouch",      "Run + jump",  "Crouch + jump", "Crouch + walk",
                                          "Walk + jump", "Left + jump", "Right + jump",  "Back + jump",
                                          "Left",        "Right",       "Back"};
struct Options {
    std::uint32_t enabled{}, heldOnly{1};
    float range{35}, standTolerance{8}, aimTolerance{1};
    Color color{.7f, .35f, 1, .9f};
    char mapOverride[96]{}; // Used only while the native map name is unavailable.
};
inline bool Valid(const Options &o) noexcept {
    const auto number = [](float n, float lo, float hi) { return std::isfinite(n) && n >= lo && n <= hi; };
    std::size_t length{};
    while (length < sizeof(o.mapOverride) && o.mapOverride[length]) {
        const char c = o.mapOverride[length++];
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_' && c != '-')
            return false;
    }
    return length < sizeof(o.mapOverride) && o.enabled <= 1 && o.heldOnly <= 1 && number(o.range, 5, 150) &&
           number(o.standTolerance, 1, 32) && number(o.aimTolerance, .1f, 5) && number(o.color.r, 0, 1) &&
           number(o.color.g, 0, 1) && number(o.color.b, 0, 1) && number(o.color.a, 0, 1);
}
Kind WeaponKind(std::uint32_t weapon) noexcept;
std::string NormalizeMap(std::string_view);
struct Capture {
    bool valid{};
    std::string map;
    Vector3 feet{}, eye{};
    float pitch{}, yaw{}; // Source pitch: positive points down.
    std::uint32_t weapon{};
    double time{};
};
struct Record {
    std::uint64_t id{};
    std::string name, map, notes;
    Kind kind{};
    std::uint32_t throwType{};
    Vector3 position{};
    float pitch{}, yaw{}, eyeHeight{64};
    bool enabled{true};
};
bool Valid(const Record &) noexcept;
bool Fresh(const Capture &, double now) noexcept;
std::string_view MapFor(const Capture &, const Options &) noexcept;
Record MakeRecord(const Capture &, std::string name, std::uint32_t throwType, std::string notes = {});
Vector3 AimPoint(const Record &) noexcept;
struct Guide {
    std::size_t index{};
    float distance{}, angle{};
    bool atStand{}, aligned{};
};
struct Selection {
    std::array<Guide, 8> guides{};
    std::size_t count{};
};
// Fixed-sized nearest set; no allocations or mutable game data on the drawing path.
Selection Select(std::span<const Record>, const Capture &, const Options &, float unitsPerMeter, double now) noexcept;
class Library {
    std::vector<Record> records_;
    std::uint64_t nextId_{1};

  public:
    const auto &Records() const noexcept { return records_; }
    const Record *Find(std::uint64_t id) const noexcept;
    std::uint64_t Add(Record);
    bool Update(Record);
    bool Remove(std::uint64_t id);
    // Transactional parse: any malformed record leaves the existing library untouched.
    // Merge preserves existing records and skips exact duplicates. Legacy Anthony arrays accepted.
    std::size_t Parse(std::string_view, bool merge);
    std::string Serialize() const;
    std::size_t Load(const std::filesystem::path &, bool merge = false);
    void Save(const std::filesystem::path &) const; // atomic replacement
};
enum class Operation { None, Capture, Update, Remove, Reload, Import, Export, OpenFolder };
struct Request {
    Operation operation{};
    Record record;
    std::filesystem::path path;
};
class Controller {
    struct Completion {
        Library library;
        std::string message;
        bool publish{};
    };
    Library library_;
    std::filesystem::path directory_;
    std::future<Completion> pending_;
    std::string message_;
    bool started_{};
    std::shared_ptr<std::atomic<bool>> stopping_{std::make_shared<std::atomic<bool>>(false)};
    std::uint64_t revision_{};

  public:
    ~Controller() { Stop(); }
    void Stop() noexcept;
    void Start(std::filesystem::path directory);
    void Tick();
    void Submit(Request);
    bool Busy() const noexcept { return pending_.valid(); }
    std::uint64_t Revision() const noexcept { return revision_; }
    const Library &Records() const noexcept { return library_; }
    const std::string &Message() const noexcept { return message_; }
};
} // namespace awareness::lineups
