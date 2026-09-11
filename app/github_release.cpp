#include "github_release.hpp"
#include "app_config.hpp"
#include <nlohmann/json.hpp>
#include <winhttp.h>
#include <bcrypt.h>
#include <charconv>
#include <chrono>
#include <cctype>
#include <sstream>
#include <vector>
namespace vortex {
namespace {
using Json = nlohmann::json;
bool IsHash(std::string_view s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c); });
}
std::string Lower(std::string s) { for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
struct Internet {
    HINTERNET value{};
    explicit Internet(HINTERNET v) : value(v) { if (!v) throw std::runtime_error("Could not open update connection."); }
    ~Internet() { WinHttpCloseHandle(value); }
    Internet(const Internet &) = delete;
};
struct Hasher {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    Hasher() {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("SHA-256 provider unavailable.");
        if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0); algorithm = nullptr;
            throw std::runtime_error("SHA-256 initialization failed.");
        }
    }
    ~Hasher() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    void Add(const char *data, DWORD count) {
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char *>(data)), count, 0) < 0)
            throw std::runtime_error("SHA-256 calculation failed.");
    }
    std::string Finish() {
        unsigned char digest[32]{};
        if (BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0) throw std::runtime_error("SHA-256 calculation failed.");
        static constexpr char hex[] = "0123456789abcdef";
        std::string result;
        for (auto c : digest) { result += hex[c >> 4]; result += hex[c & 15]; }
        return result;
    }
};
void Transfer(std::string url, uint64_t limit, uint64_t expected, const std::atomic<bool> &cancel,
              const std::function<void(const char *, DWORD)> &receive, const std::function<void(int)> &progress) {
    Internet session(WinHttpOpen(Wide(std::string("Vortex/") + AppVersion).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000)) throw std::runtime_error("Update timeout setup failed.");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    for (int redirects = 0; redirects <= 5; ++redirects) {
        if (cancel) throw std::runtime_error("Update cancelled.");
        if (!TrustedGitHubUrl(url)) throw std::runtime_error("Untrusted update redirect.");
        const auto wide = Wide(url);
        URL_COMPONENTS parts{sizeof(parts)};
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts)) throw std::runtime_error("Invalid update URL.");
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        Internet connection(WinHttpConnect(session.value, host.c_str(), parts.nPort, 0));
        Internet request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
        if (!WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)))
            throw std::runtime_error("Could not secure update request.");
        const wchar_t *headers = host == L"api.github.com" ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n" : L"Accept: application/octet-stream\r\n";
        if (!WinHttpSendRequest(request.value, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.value, nullptr)) throw std::runtime_error("Update connection failed or timed out. You can keep using Vortex.");
        DWORD status{}, bytes = sizeof(status);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX))
            throw std::runtime_error("Missing update HTTP status.");
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            wchar_t location[8192]{}; bytes = sizeof(location);
            if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                      location, &bytes, WINHTTP_NO_HEADER_INDEX)) throw std::runtime_error("Invalid update redirect.");
            url = Utf8(location); continue;
        }
        if (status != 200) throw std::runtime_error("Update service returned HTTP " + std::to_string(status) + ". Try again later.");
        wchar_t lengthText[32]{}; bytes = sizeof(lengthText);
        uint64_t contentLength{};
        if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                lengthText, &bytes, WINHTTP_NO_HEADER_INDEX)) {
            auto length = Utf8(lengthText);
            const auto parsed = std::from_chars(length.data(), length.data() + length.size(), contentLength);
            if (parsed.ec != std::errc{} || parsed.ptr != length.data()+length.size() || contentLength > limit ||
                (expected && contentLength != expected)) throw std::runtime_error("Incorrect update download size.");
        }
        uint64_t total{};
        char buffer[64 * 1024];
        for (;;) {
            if (cancel) throw std::runtime_error("Update cancelled.");
            if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Update download timed out.");
            DWORD read{};
            if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &read)) throw std::runtime_error("Update download interrupted.");
            if (!read) break;
            total += read;
            if (total > limit || (expected && total > expected)) throw std::runtime_error("Update download exceeds its declared size.");
            receive(buffer, read);
            if (progress && expected) progress(static_cast<int>(total * 100 / expected));
        }
        if (!total || (contentLength && total != contentLength) || (expected && total != expected))
            throw std::runtime_error("Incomplete update download.");
        return;
    }
    throw std::runtime_error("Too many update redirects.");
}
} // namespace
ReleaseVersion ReleaseVersion::Parse(std::string_view value, bool tag) {
    if (tag) { if (value.empty() || value.front() != 'v') throw std::runtime_error("Invalid release tag."); value.remove_prefix(1); }
    ReleaseVersion result;
    for (size_t i=0; i<3; ++i) {
        const auto separator = value.find('.');
        auto part = value.substr(0, separator);
        if (part.empty() || (part.size()>1 && part.front()=='0')) throw std::runtime_error("Invalid semantic version.");
        auto parsed = std::from_chars(part.data(), part.data()+part.size(), result.numbers[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != part.data()+part.size() || result.numbers[i]>65535 ||
            (i<2 && separator==std::string_view::npos) || (i==2 && separator!=std::string_view::npos))
            throw std::runtime_error("Invalid semantic version.");
        if (i<2) value.remove_prefix(separator+1);
    }
    return result;
}
bool TrustedGitHubUrl(const std::string &url) {
    if (url.empty() || url.size()>8192 || url.find_first_of("\r\n\t\\\"#") != std::string::npos) return false;
    try {
        auto wide = Wide(url); URL_COMPONENTS parts{sizeof(parts)};
        parts.dwHostNameLength=parts.dwUserNameLength=parts.dwPasswordLength=static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts) ||
            parts.nScheme != INTERNET_SCHEME_HTTPS || parts.nPort != 443 || parts.dwUserNameLength || parts.dwPasswordLength) return false;
        auto host=Lower(Utf8(std::wstring(parts.lpszHostName, parts.dwHostNameLength)));
        return host=="github.com" || host=="api.github.com" || host=="release-assets.githubusercontent.com" || host=="objects.githubusercontent.com";
    } catch (...) { return false; }
}
GitHubRelease GitHubRelease::Parse(std::string_view text, const std::string &repositoryUrl) {
    if (text.size()>2*1024*1024) throw std::runtime_error("Release metadata is too large.");
    auto json = Json::parse(text);
    if (!json.is_object() || json.at("draft").get<bool>() || json.at("prerelease").get<bool>()) throw std::runtime_error("Not a stable published release.");
    GitHubRelease release;
    release.tag=json.at("tag_name").get<std::string>();
    ReleaseVersion::Parse(release.tag, true); release.version=release.tag.substr(1);
    if (json.contains("body") && !json["body"].is_null()) release.notes=json.at("body").get<std::string>();
    if (release.notes.size()>32768) release.notes.resize(32768);
    const auto &assets=json.at("assets");
    if (!assets.is_array() || assets.size()>64) throw std::runtime_error("Invalid release asset list.");
    for (const auto &item: assets) {
        ReleaseAsset asset;
        asset.name=item.at("name").get<std::string>(); asset.url=item.at("browser_download_url").get<std::string>();
        if (!item.at("size").is_number_unsigned()) throw std::runtime_error("Invalid release asset size.");
        asset.size=item.at("size").get<uint64_t>();
        if (asset.name.empty() || asset.name.size()>200 || asset.name.find_first_of("/\\:\r\n\t")!=std::string::npos ||
            !asset.size || asset.size>512ull*1024*1024 || !TrustedGitHubUrl(asset.url) ||
            asset.url != repositoryUrl+"/releases/download/"+release.tag+"/"+asset.name || item.value("state", "")!="uploaded")
            throw std::runtime_error("Invalid release asset.");
        if (item.contains("digest") && !item["digest"].is_null()) {
            auto digest=item["digest"].get<std::string>();
            if (!digest.starts_with("sha256:") || !IsHash(digest.substr(7))) throw std::runtime_error("Invalid release asset digest.");
            asset.digest=Lower(digest.substr(7));
        }
        if (!release.assets.emplace(asset.name,asset).second) throw std::runtime_error("Duplicate release asset.");
    }
    release.Asset("VortexSetup.exe"); release.Asset("SHA256SUMS.txt"); release.Asset("releases.win.json");
    release.Asset(std::string(AppId)+"-"+release.version+"-full.nupkg");
    return release;
}
const ReleaseAsset &GitHubRelease::Asset(const std::string &name) const {
    auto i=assets.find(name); if (i==assets.end()) throw std::runtime_error("A required release asset is missing."); return i->second;
}
std::map<std::string,std::string> ParseChecksums(std::string_view text) {
    if (text.size()>65536) throw std::runtime_error("Checksum manifest is too large.");
    std::map<std::string,std::string> result; std::istringstream input{std::string(text)}; std::string line;
    while (std::getline(input,line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        if (line.empty()) continue;
        if (line.size()<67 || line.substr(64,2)!="  " || !IsHash(line.substr(0,64))) throw std::runtime_error("Invalid checksum manifest.");
        auto name=line.substr(66);
        if (name.empty() || name.find_first_of("/\\:\t\r\n")!=std::string::npos || !result.emplace(name,Lower(line.substr(0,64))).second)
            throw std::runtime_error("Invalid or duplicate checksum entry.");
    }
    return result;
}
std::string Sha256(std::string_view bytes) { Hasher h; h.Add(bytes.data(),static_cast<DWORD>(bytes.size())); return h.Finish(); }
std::string FetchGitHubText(const std::string &url, size_t limit, const std::atomic<bool> &cancel) {
    std::string result;
    Transfer(url,limit,0,cancel,[&](const char *data,DWORD size){ result.append(data,size); },{});
    return result;
}
void DownloadGitHubAsset(const ReleaseAsset &asset, const std::string &checksum, const std::filesystem::path &destination,
                         const std::atomic<bool> &cancel, const std::function<void(int)> &progress) {
    if (!IsHash(checksum) || (!asset.digest.empty() && asset.digest!=Lower(checksum))) throw std::runtime_error("Update checksum disagreement.");
    GUID guid{}; if (FAILED(CoCreateGuid(&guid))) throw std::runtime_error("Could not create download file identity.");
    wchar_t suffix[40]{}; StringFromGUID2(guid,suffix,40);
    auto temporary=destination; temporary+=std::wstring(L".")+suffix+L".partial";
    struct Cleanup { std::filesystem::path path; ~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);} } cleanup{temporary};
    {
        Handle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
        if (!file) throw std::runtime_error("Cannot create update download. Check disk space and permissions.");
        Hasher hash;
        Transfer(asset.url,asset.size,asset.size,cancel,[&](const char *data,DWORD count){
            DWORD written{};
            if (!WriteFile(file.value,data,count,&written,nullptr) || written!=count) throw std::runtime_error("Cannot save update. Check disk space and permissions.");
            hash.Add(data,count);
        },progress);
        if (hash.Finish()!=Lower(checksum)) throw std::runtime_error("Update SHA-256 verification failed.");
        if (!FlushFileBuffers(file.value)) throw std::runtime_error("Could not flush update to disk.");
    }
    if (cancel) throw std::runtime_error("Update cancelled.");
    // Destination is supplied by Velopack inside its managed package directory.
    if (!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Could not finalize the verified update.");
}
std::string ValidateReleaseFeed(const GitHubRelease &release,std::string_view feed,const std::map<std::string,std::string>&checksums) {
    const auto require=[&](const std::string &name) -> const std::string & {
        auto i=checksums.find(name); if(i==checksums.end()) throw std::runtime_error("Required update checksum missing.");
        const auto &asset=release.Asset(name);
        if(!asset.digest.empty() && asset.digest!=i->second) throw std::runtime_error("Release checksum mismatch.");
        return i->second;
    };
    require("VortexSetup.exe");
    if (Sha256(feed)!=require("releases.win.json")) throw std::runtime_error("Release feed checksum mismatch.");
    auto json=Json::parse(feed); const auto &assets=json.at("Assets");
    if(!assets.is_array() || assets.size()>256) throw std::runtime_error("Invalid update feed.");
    Json selected=Json::array();
    for(const auto &entry:assets) {
        if(entry.value("Type","")!="Full" || entry.value("Version","")!=release.version) continue;
        if(entry.at("PackageId").get<std::string>()!=AppId) throw std::runtime_error("Wrong update application identity.");
        auto name=entry.at("FileName").get<std::string>();
        if(name!=std::string(AppId)+"-"+release.version+"-full.nupkg" || !entry.at("Size").is_number_unsigned() ||
           entry.at("Size").get<uint64_t>()!=release.Asset(name).size || Lower(entry.at("SHA256").get<std::string>())!=require(name))
            throw std::runtime_error("Update package metadata does not match the release.");
        auto normalized = entry;
        normalized["SHA256"] = Lower(entry.at("SHA256").get<std::string>());
        selected.push_back(std::move(normalized));
    }
    if(selected.size()!=1) throw std::runtime_error("Expected exactly one matching update package.");
    return Json{{"Assets",selected}}.dump();
}
} // namespace vortex
