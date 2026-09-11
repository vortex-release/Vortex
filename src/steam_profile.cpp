#include "steam_profile.hpp"
#include <cstring>
#include <algorithm>
namespace awareness {
namespace {
bool Sample(const SteamProfileApi *api, SteamProfileSample *out) noexcept {
    __try {
        out->id = api->steamId(api->user);
        const char *name = api->persona(api->friends);
        if (!out->id || !name)
            return false;
        unsigned i = 0;
        for (; i + 1 < sizeof(out->name) && name[i]; ++i)
            out->name[i] = static_cast<unsigned char>(name[i]) < 32 ? ' ' : name[i];
        out->name[i] = 0;
        out->avatar = api->avatar(api->friends, out->id);
        if (out->avatar > 0 && !api->imageSize(api->utils, out->avatar, &out->width, &out->height))
            out->avatar = -1;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool OtherAvatar(const SteamProfileApi *api, std::uint64_t id, bool request, SteamProfileSample *out) noexcept {
    __try {
        out->id = id;
        if (request && api->requestUser)
            api->requestUser(api->friends, id, false);
        out->avatar = api->avatar(api->friends, id);
        return out->avatar <= 0 || api->imageSize(api->utils, out->avatar, &out->width, &out->height);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool Pixels(const SteamProfileApi *api, int image, BYTE *pixels, int size) noexcept {
    __try {
        return api->imageRgba(api->utils, image, pixels, size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void *Interface(HMODULE module, const char *name) noexcept {
    const auto get = reinterpret_cast<void *(__cdecl *)()>(GetProcAddress(module, name));
    __try {
        return get ? get() : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}
} // namespace
bool ReadSteamProfile(const SteamProfileApi &api, SteamProfileSample &out) noexcept {
    out = {};
    if (!api || !Sample(&api, &out)) {
        out = {};
        return false;
    }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, out.name, -1, nullptr, 0))
        out.name[0] = 0;
    return true;
}
bool ReadSteamAvatar(const SteamProfileApi &api, const SteamProfileSample &sample, ImagePixels &out) noexcept {
    out = {};
    if (!api || sample.avatar <= 0 || !sample.width || !sample.height || sample.width > 256 || sample.height > 256)
        return false;
    try {
        ImagePixels next{sample.width, sample.height, std::vector<BYTE>(std::size_t(sample.width) * sample.height * 4)};
        if (!Pixels(&api, sample.avatar, next.rgba.data(), static_cast<int>(next.rgba.size())))
            return false;
        out = std::move(next);
        return true;
    } catch (...) {
        return false;
    }
}
void SteamAvatarCache::Clear() noexcept {
    for (auto &entry : entries_)
        entry = {};
    nextWork_ = 0;
    device_ = nullptr;
}
ID3D11ShaderResourceView *SteamAvatarCache::Get(const SteamProfileApi &api, ID3D11Device *device, std::uint64_t id,
                                                ULONGLONG now) noexcept {
    if (!api || !device || !id)
        return nullptr;
    if (device_ != device) {
        Clear();
        device_ = device;
    }
    Entry *entry{};
    for (auto &item : entries_)
        if (item.id == id) {
            entry = &item;
            break;
        }
    if (!entry) {
        entry = &*std::min_element(entries_.begin(), entries_.end(),
                                   [](const auto &a, const auto &b) { return a.used < b.used; });
        *entry = {};
        entry->id = id;
    }
    entry->used = now;
    // At most one avatar read/upload per 100 ms, independent of frame rate.
    if (now >= nextWork_ && now >= entry->nextPoll) {
        nextWork_ = now + 100;
        entry->nextPoll = now + 5000;
        SteamProfileSample sample;
        const bool success = OtherAvatar(&api, id, !entry->requested, &sample);
        entry->requested = true;
        if (success && sample.avatar == 0) {
            entry->image = 0;
            entry->view.Reset();
        } else if (success && sample.avatar > 0 && sample.avatar != entry->image) {
            ImagePixels pixels;
            if (ReadSteamAvatar(api, sample, pixels) && SUCCEEDED(UploadImage(device, pixels, entry->view)))
                entry->image = sample.avatar;
        } else if (!success || sample.avatar < 0)
            entry->nextPoll = now + 1000;
    }
    return entry->view.Get();
}
bool SteamProfile::Connect() noexcept {
    if (api_)
        return true;
    if (!module_ && !GetModuleHandleExW(0, L"steam_api64.dll", &module_))
        return false;
    for (const char *name : {"SteamAPI_SteamFriends_v018", "SteamAPI_SteamFriends_v017"})
        if ((api_.friends = Interface(module_, name)))
            break;
    for (const char *name : {"SteamAPI_SteamUser_v023", "SteamAPI_SteamUser_v022", "SteamAPI_SteamUser_v021"})
        if ((api_.user = Interface(module_, name)))
            break;
    for (const char *name : {"SteamAPI_SteamUtils_v010", "SteamAPI_SteamUtils_v009"})
        if ((api_.utils = Interface(module_, name)))
            break;
    api_.persona =
        reinterpret_cast<decltype(api_.persona)>(GetProcAddress(module_, "SteamAPI_ISteamFriends_GetPersonaName"));
    api_.steamId = reinterpret_cast<decltype(api_.steamId)>(GetProcAddress(module_, "SteamAPI_ISteamUser_GetSteamID"));
    api_.avatar = reinterpret_cast<decltype(api_.avatar)>(
        GetProcAddress(module_, "SteamAPI_ISteamFriends_GetMediumFriendAvatar"));
    api_.imageSize =
        reinterpret_cast<decltype(api_.imageSize)>(GetProcAddress(module_, "SteamAPI_ISteamUtils_GetImageSize"));
    api_.imageRgba =
        reinterpret_cast<decltype(api_.imageRgba)>(GetProcAddress(module_, "SteamAPI_ISteamUtils_GetImageRGBA"));
    api_.requestUser = reinterpret_cast<decltype(api_.requestUser)>(
        GetProcAddress(module_, "SteamAPI_ISteamFriends_RequestUserInformation"));
    return bool(api_);
}
SteamProfile::~SteamProfile() {
    if (module_)
        FreeLibrary(module_);
}
void SteamProfile::Update(ID3D11Device *device, ULONGLONG now) noexcept {
    if (now < nextPoll_)
        return;
    nextPoll_ = now + 2000;
    try {
        SteamProfileSample sample;
        if (!Connect() || !ReadSteamProfile(api_, sample)) {
            id_ = 0;
            image_ = 0;
            name_ = "Steam user";
            avatar_.Reset();
            api_ = {};
            avatars_.Clear();
            return;
        }
        if (sample.id != id_) {
            avatar_.Reset();
            image_ = 0;
        }
        id_ = sample.id;
        name_ = sample.name[0] ? sample.name : "Steam user";
        if (sample.avatar == 0) {
            avatar_.Reset();
            image_ = 0;
        } else if (sample.avatar > 0 && sample.avatar != image_) {
            ImagePixels pixels;
            if (ReadSteamAvatar(api_, sample, pixels) && SUCCEEDED(UploadImage(device, pixels, avatar_)))
                image_ = sample.avatar;
        }
    } catch (...) {
    }
}
} // namespace awareness
