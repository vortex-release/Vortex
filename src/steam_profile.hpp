#pragma once
#include "image_texture.hpp"
#include <string>
#include <array>
namespace awareness {
struct SteamProfileApi {
    void *friends{}, *user{}, *utils{};
    const char *(__cdecl *persona)(void *){};
    std::uint64_t(__cdecl *steamId)(void *) {};
    int(__cdecl *avatar)(void *, std::uint64_t){};
    bool(__cdecl *imageSize)(void *, int, UINT *, UINT *){};
    bool(__cdecl *imageRgba)(void *, int, BYTE *, int){};
    bool(__cdecl *requestUser)(void *, std::uint64_t, bool){};
    explicit operator bool() const noexcept {
        return friends && user && utils && persona && steamId && avatar && imageSize && imageRgba;
    }
};
struct SteamProfileSample {
    char name[256]{};
    std::uint64_t id{};
    int avatar{};
    UINT width{}, height{};
};
bool ReadSteamProfile(const SteamProfileApi &, SteamProfileSample &) noexcept;
bool ReadSteamAvatar(const SteamProfileApi &, const SteamProfileSample &, ImagePixels &) noexcept;
class SteamAvatarCache {
    struct Entry {
        std::uint64_t id{};
        ULONGLONG used{}, nextPoll{};
        int image{};
        bool requested{};
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    };
    std::array<Entry, 64> entries_;
    ULONGLONG nextWork_{};
    ID3D11Device *device_{};

  public:
    ID3D11ShaderResourceView *Get(const SteamProfileApi &, ID3D11Device *, std::uint64_t, ULONGLONG) noexcept;
    void Clear() noexcept;
};
class SteamProfile {
    HMODULE module_{};
    SteamProfileApi api_;
    ULONGLONG nextPoll_{};
    std::uint64_t id_{};
    int image_{};
    std::string name_{"Steam user"};
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> avatar_;
    SteamAvatarCache avatars_;
    bool Connect() noexcept;

  public:
    ~SteamProfile();
    void Update(ID3D11Device *, ULONGLONG now = GetTickCount64()) noexcept;
    const char *Name() const noexcept { return name_.c_str(); }
    ID3D11ShaderResourceView *Avatar() const noexcept { return avatar_.Get(); }
    ID3D11ShaderResourceView *AvatarFor(ID3D11Device *device, std::uint64_t id, ULONGLONG now) noexcept {
        return id && id == id_ ? avatar_.Get() : avatars_.Get(api_, device, id, now);
    }
    bool Connected() const noexcept { return id_ != 0; }
};
} // namespace awareness
