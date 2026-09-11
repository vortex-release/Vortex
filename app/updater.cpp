#include "updater.hpp"
#include "app_config.hpp"
#include "loader.hpp"
#include <winhttp.h>
#include <fstream>
#include <algorithm>
#include <cstring>
namespace vortex {
namespace {
std::mutex logMutex;
std::string LastError() {
    char text[4096]{};
    vpkc_get_last_error(text, sizeof(text));
    return *text ? text : "The update service could not complete the request.";
}
} // namespace
void Log(const std::string &message) noexcept {
    try {
        std::lock_guard lock(logMutex);
        const auto file = LogDirectory() / L"Vortex.log";
        std::error_code error;
        if (std::filesystem::file_size(file, error) > 2 * 1024 * 1024 && !error) {
            std::filesystem::remove(file.wstring() + L".old", error);
            std::filesystem::rename(file, file.wstring() + L".old", error);
        }
        std::ofstream out(file, std::ios::app);
        SYSTEMTIME time{};
        GetLocalTime(&time);
        out << time.wYear << '-' << time.wMonth << '-' << time.wDay << ' ' << time.wHour << ':' << time.wMinute << ':'
            << time.wSecond << ' ' << message << '\n';
    } catch (...) {
    }
}
void InitializeUpdates() {
    // The DLL can outlive this launcher inside its host.
    vpkc_app_set_auto_apply_on_startup(false);
    vpkc_app_run(nullptr);
    vpkc_set_logger([](void *, const char *level,
                       const char *message) { Log(std::string(level ? level : "") + " " + (message ? message : "")); },
                    nullptr);
}
bool ValidUpdateUrl(const std::string &url, bool allowLocal) {
    if (url.empty() || url.size() > 4096 || url.find_first_of("\r\n\t\\\"#") != std::string::npos)
        return false;
    std::wstring wide;
    try {
        wide = Wide(url);
    } catch (...) {
        return false;
    }
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength = parts.dwUserNameLength = parts.dwPasswordLength = parts.dwExtraInfoLength =
        static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts) || !parts.dwHostNameLength ||
        parts.dwUserNameLength || parts.dwPasswordLength || parts.dwExtraInfoLength)
        return false;
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const bool local = host == L"127.0.0.1" || _wcsicmp(host.c_str(), L"localhost") == 0;
    return parts.nScheme == INTERNET_SCHEME_HTTPS || (allowLocal && local && parts.nScheme == INTERNET_SCHEME_HTTP);
}
Updater::Updater(std::string url) : url_(std::move(url)) {
    github_ = url_.starts_with("https://github.com/");
    if (url_.empty())
        Set(UpdateStage::Unconfigured, "Update service is not connected yet.");
    else if (!ValidUpdateUrl(url_, true))
        Set(UpdateStage::Error, "The update address is invalid. Reinstall from the publisher.");
    else
        Set(UpdateStage::Idle, "Check for the latest Vortex release.");
}
Updater::~Updater() {
    Cancel();
    if (work_.valid())
        work_.wait();
    if (pending_)
        vpkc_free_asset(pending_);
    if (update_)
        vpkc_free_update_info(update_);
    if (manager_)
        vpkc_free_update_manager(manager_);
    if (source_) vpkc_free_source(source_);
}
UpdateStatus Updater::Status() {
    std::lock_guard lock(mutex_);
    return status_;
}
void Updater::Set(UpdateStage stage, std::string message) {
    std::lock_guard lock(mutex_);
    status_.stage = stage;
    status_.message = std::move(message);
    if (stage == UpdateStage::Checking || stage == UpdateStage::Idle) {
        status_.version.clear();
        status_.notes.clear();
        status_.progress = 0;
    }
    if (stage == UpdateStage::Downloading)
        status_.progress = 0;
    if (stage == UpdateStage::Ready)
        status_.progress = 100;
}
void Updater::Start(std::function<void()> operation) {
    if (busy_.exchange(true))
        return;
    cancel_.store(false);
    if (work_.valid())
        work_.get();
    try {
        work_ = std::async(std::launch::async, [this, operation = std::move(operation)] {
            try {
                operation();
            } catch (const std::exception &error) {
                Log(error.what());
                Set(UpdateStage::Error, error.what());
            } catch (...) {
                Log("Unexpected update operation failure.");
                Set(UpdateStage::Error, "The update operation failed. Try again shortly.");
            }
            busy_.store(false);
        });
    } catch (...) {
        busy_ = false;
        Set(UpdateStage::Error, "Could not start the update operation. Try again shortly.");
    }
}
bool Updater::EnsureManager() {
    if (manager_)
        return true;
    auto channel = std::string(UpdateChannel);
    vpkc_update_options_t options{};
    options.ExplicitChannel = channel.data();
    options.MaximumDeltasBeforeFallback = github_ ? -1 : 10;
    if (github_ && !source_) {
        source_ = vpkc_new_source_custom_callback(
            [](void *data, const char *) -> char * {
                auto &self = *static_cast<Updater *>(data);
                try {
                    Log(std::string("GitHub update check started; current version ") + AppVersion);
                    const auto endpoint = "https://api.github.com/repos/" + self.url_.substr(19) + "/releases/latest";
                    self.release_ = GitHubRelease::Parse(FetchGitHubText(endpoint, 2*1024*1024, self.cancel_), self.url_);
                    Log("Latest published version " + self.release_->version);
                    if (ReleaseVersion::Parse(self.release_->version) <= ReleaseVersion::Parse(AppVersion)) {
                        self.feed_ = "{\"Assets\":[]}";
                    } else {
                        const auto &sums = self.release_->Asset("SHA256SUMS.txt");
                        auto manifest = FetchGitHubText(sums.url, 65536, self.cancel_);
                        if (manifest.size() != sums.size || (!sums.digest.empty() && Sha256(manifest) != sums.digest))
                            throw std::runtime_error("Checksum manifest verification failed.");
                        self.checksums_ = ParseChecksums(manifest);
                        const auto &feedAsset = self.release_->Asset("releases.win.json");
                        auto feed = FetchGitHubText(feedAsset.url, 1024*1024, self.cancel_);
                        if (feed.size() != feedAsset.size) throw std::runtime_error("Incomplete update feed.");
                        self.feed_ = ValidateReleaseFeed(*self.release_, feed, self.checksums_);
                    }
                    auto *copy = static_cast<char *>(std::malloc(self.feed_.size()+1));
                    if (!copy) throw std::bad_alloc();
                    std::memcpy(copy,self.feed_.c_str(),self.feed_.size()+1);
                    return copy;
                } catch (const std::exception &e) { Log(e.what()); return nullptr; }
                catch (...) { Log("GitHub metadata check failed."); return nullptr; }
            },
            [](void *, char *feed) { std::free(feed); },
            [](void *data, const vpkc_asset_t *asset, const char *path, size_t progress) -> bool {
                auto &self = *static_cast<Updater *>(data);
                try {
                    if (!self.release_ || !asset || !asset->FileName || !path) throw std::runtime_error("Invalid download state.");
                    const auto &remote = self.release_->Asset(asset->FileName);
                    auto checksum = self.checksums_.at(remote.name);
                    if (!asset->SHA256 || checksum != asset->SHA256 || asset->Size != remote.size)
                        throw std::runtime_error("Package metadata changed before download.");
                    Log("Update package download started: " + remote.name);
                    DownloadGitHubAsset(remote,checksum,Wide(path),self.cancel_,[progress](int value) {
                        vpkc_source_report_progress(progress,static_cast<int16_t>(value));
                    });
                    Log("Update download complete; SHA-256 verified.");
                    return true;
                } catch (const std::exception &e) { Log(e.what()); return false; }
                catch (...) { Log("Update download failed."); return false; }
            },this);
        if (!source_) throw std::runtime_error("Could not initialize GitHub update source.");
    }
    const bool created = github_ ? vpkc_new_update_manager_with_source(source_, &options, nullptr, &manager_)
                                 : vpkc_new_update_manager(url_.c_str(), &options, nullptr, &manager_);
    if (!created) {
        Log(LastError());
        Set(UpdateStage::Development, "Install Vortex using its setup file to enable updates.");
        return false;
    }
    char id[512]{};
    vpkc_get_app_id(manager_, id, sizeof(id));
    if (std::string(id) != AppId) {
        vpkc_free_update_manager(manager_);
        manager_ = nullptr;
        throw std::runtime_error("The installation identity does not match this app.");
    }
    return true;
}
void Updater::Check() {
    if (url_.empty() || !ValidUpdateUrl(url_, true) || Busy())
        return;
    const auto now = std::chrono::steady_clock::now();
    if (lastCheck_.time_since_epoch().count() && now-lastCheck_ < std::chrono::seconds(60)) return;
    lastCheck_ = now;
    Start([this] {
        Set(UpdateStage::Checking, "Checking for updates...");
        if (!EnsureManager())
            return;
        if (pending_) {
            vpkc_free_asset(pending_);
            pending_ = nullptr;
        }
        if (vpkc_update_pending_restart(manager_, &pending_) && pending_) {
            if (!pending_->PackageId || std::string(pending_->PackageId) != AppId || !pending_->Version ||
                !*pending_->Version)
                throw std::runtime_error("The downloaded update belongs to a different application.");
            {
                std::lock_guard lock(mutex_);
                status_.version = pending_->Version ? pending_->Version : "";
                status_.notes = pending_->NotesMarkdown ? pending_->NotesMarkdown : "";
            }
            Set(UpdateStage::Ready, "Update downloaded. Install when you are ready.");
            return;
        }
        if (update_) {
            vpkc_free_update_info(update_);
            update_ = nullptr;
        }
        switch (vpkc_check_for_updates(manager_, &update_)) {
        case UPDATE_AVAILABLE:
            if (!update_ || !update_->TargetFullRelease || !update_->TargetFullRelease->PackageId ||
                std::string(update_->TargetFullRelease->PackageId) != AppId || !update_->TargetFullRelease->Version ||
                !*update_->TargetFullRelease->Version)
                throw std::runtime_error("The server returned an update for a different application.");
            {
                std::lock_guard lock(mutex_);
                status_.version = update_->TargetFullRelease->Version;
                status_.notes =
                    update_->TargetFullRelease->NotesMarkdown ? update_->TargetFullRelease->NotesMarkdown : "";
            }
            Log("Update available: " + Status().version);
            Set(UpdateStage::Available, "A new version of Vortex is available.");
            break;
        case NO_UPDATE_AVAILABLE:
            Log("No newer update available.");
            Set(UpdateStage::Idle, "You are up to date.");
            break;
        case REMOTE_IS_EMPTY:
            Log("No newer update available.");
            Set(UpdateStage::Idle, github_ ? "You are up to date." : "No releases have been published yet.");
            break;
        default:
            Log(LastError());
            Set(UpdateStage::Error,
                "Could not reach the update service. You can keep using this version and try again later.");
        }
    });
}
void Updater::Download() {
    if (Busy() || Status().stage != UpdateStage::Available)
        return;
    Start([this] {
        Set(UpdateStage::Downloading, "Downloading update...");
        if (!vpkc_download_updates(
                manager_, update_,
                [](void *data, size_t progress) {
                    auto &self = *static_cast<Updater *>(data);
                    std::lock_guard lock(self.mutex_);
                    self.status_.progress = static_cast<int>(std::min(progress, size_t{100}));
                },
                this))
            throw std::runtime_error("Update download failed: " + LastError());
        Set(UpdateStage::Ready, "Update downloaded. Close the game and preview to install.");
    });
}
bool Updater::Apply(std::string &error, bool restart) {
    if (Busy() || Status().stage != UpdateStage::Ready) {
        error = "Download an update first.";
        return false;
    }
    if (!CanReplaceApplication(ModuleDirectory(), error))
        return false;
    auto *asset = pending_ ? pending_ : update_ ? update_->TargetFullRelease : nullptr;
    if (!asset || !vpkc_wait_exit_then_apply_updates(manager_, asset, false, restart, nullptr, 0)) {
        error = LastError();
        return false;
    }
    Log("Updater launched; waiting for Vortex to exit before installation and restart.");
    return true;
}
} // namespace vortex
