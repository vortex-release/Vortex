#pragma once
#include "app_paths.hpp"
#include <atomic>
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string_view>
namespace vortex {
struct ReleaseVersion {
    std::array<unsigned, 3> numbers{};
    auto operator<=>(const ReleaseVersion &) const = default;
    static ReleaseVersion Parse(std::string_view value, bool tag = false);
};
struct ReleaseAsset { std::string name, url, digest; uint64_t size{}; };
struct GitHubRelease {
    std::string version, tag, notes;
    std::map<std::string, ReleaseAsset> assets;
    static GitHubRelease Parse(std::string_view json, const std::string &repositoryUrl);
    const ReleaseAsset &Asset(const std::string &name) const;
};
std::map<std::string, std::string> ParseChecksums(std::string_view text);
std::string Sha256(std::string_view bytes);
bool TrustedGitHubUrl(const std::string &url);
// Anonymous WinHTTP transport. No credential provider, cookies, or authentication headers.
std::string FetchGitHubText(const std::string &url, size_t limit, const std::atomic<bool> &cancel);
void DownloadGitHubAsset(const ReleaseAsset &asset, const std::string &sha256,
                         const std::filesystem::path &destination, const std::atomic<bool> &cancel,
                         const std::function<void(int)> &progress);
std::string ValidateReleaseFeed(const GitHubRelease &release, std::string_view feed,
                               const std::map<std::string, std::string> &checksums);
} // namespace vortex
