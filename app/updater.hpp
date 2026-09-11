#pragma once
#include "app_paths.hpp"
#include "github_release.hpp"
#include <chrono>
#include <Velopack.h>
#include <atomic>
#include <future>
#include <mutex>
#include <functional>
namespace vortex {
enum class UpdateStage { Unconfigured, Development, Idle, Checking, Available, Downloading, Ready, Error };
struct UpdateStatus {
    UpdateStage stage{UpdateStage::Idle};
    std::string message, version, notes;
    int progress{};
};
void InitializeUpdates();
void Log(const std::string &message) noexcept;
bool ValidUpdateUrl(const std::string &url, bool allowLocal = false);
class Updater {
  public:
    explicit Updater(std::string url);
    ~Updater();
    UpdateStatus Status();
    bool Busy() const { return busy_.load(); }
    void Check();
    void Download();
    void Cancel() noexcept { cancel_.store(true); }
    bool Apply(std::string &error, bool restart = true);

  private:
    void Set(UpdateStage stage, std::string message);
    bool EnsureManager();
    void Start(std::function<void()> operation);
    std::string url_;
    bool github_{};
    std::atomic<bool> cancel_{};
    std::chrono::steady_clock::time_point lastCheck_{};
    vpkc_update_source_t *source_{};
    std::optional<GitHubRelease> release_;
    std::string feed_;
    std::map<std::string, std::string> checksums_;
    std::mutex mutex_;
    UpdateStatus status_;
    std::atomic<bool> busy_{};
    std::future<void> work_;
    vpkc_update_manager_t *manager_{};
    vpkc_update_info_t *update_{};
    vpkc_asset_t *pending_{};
};
} // namespace vortex
