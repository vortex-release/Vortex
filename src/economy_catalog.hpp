#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace awareness::cosmetics {
enum class ItemKind : std::uint8_t { Weapon, Knife, Glove, Agent };
struct Definition {
    std::uint16_t id{};
    ItemKind kind{};
    std::uint8_t team{};
    std::string name, internalName, model;
};
struct PaintKit {
    std::uint32_t id{};
    std::string name, internalName;
    float minWear{}, maxWear{1};
    bool legacy{};
};
struct FinishPair {
    std::uint16_t definition{};
    std::uint32_t paintKit{};
};
struct Catalog {
    std::vector<Definition> definitions;
    std::vector<PaintKit> paints;
    std::vector<FinishPair> pairs;
    const Definition *Find(std::uint32_t id) const noexcept;
    const PaintKit *Paint(std::uint32_t id) const noexcept;
    bool Supports(std::uint32_t definition, std::uint32_t paint) const noexcept;
};
// Transactional and bounded. Public for offline fixtures and local catalog tools.
bool ParseCatalog(std::string_view itemsGame, std::string_view english, Catalog &out, std::string &error);
bool LoadCatalog(const std::filesystem::path &csgoDirectory, Catalog &out, std::string &error);
class CatalogController {
    mutable std::mutex mutex_;
    std::thread worker_;
    std::shared_ptr<const Catalog> catalog_;
    std::string error_;
    std::atomic<bool> busy_{};

  public:
    ~CatalogController();
    bool Start(const std::filesystem::path &csgoDirectory);
    void Stop();
    bool Busy() const noexcept { return busy_.load(std::memory_order_acquire); }
    std::shared_ptr<const Catalog> Snapshot() const;
    std::string Error() const;
};
} // namespace awareness::cosmetics
