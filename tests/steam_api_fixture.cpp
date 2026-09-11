#include <Windows.h>
#include <cstdint>
static int token, reads;
extern "C" {
__declspec(dllexport) void *SteamAPI_SteamFriends_v018() {
    return &token;
}
__declspec(dllexport) void *SteamAPI_SteamUser_v023() {
    return &token;
}
__declspec(dllexport) void *SteamAPI_SteamUtils_v010() {
    return &token;
}
__declspec(dllexport) const char *SteamAPI_ISteamFriends_GetPersonaName(void *) {
    return "Observer Demo";
}
__declspec(dllexport) std::uint64_t SteamAPI_ISteamUser_GetSteamID(void *) {
    return 76561198000000001ull;
}
__declspec(dllexport) int SteamAPI_ISteamFriends_GetMediumFriendAvatar(void *, std::uint64_t) {
    return 4;
}
__declspec(dllexport) bool SteamAPI_ISteamUtils_GetImageSize(void *, int image, UINT *width, UINT *height) {
    *width = *height = 64;
    return image == 4;
}
__declspec(dllexport) bool SteamAPI_ISteamUtils_GetImageRGBA(void *, int image, BYTE *pixels, int size) {
    if (image != 4 || size != 64 * 64 * 4)
        return false;
    ++reads;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const auto at = (y * 64 + x) * 4;
            const bool ring = (x - 32) * (x - 32) + (y - 32) * (y - 32) > 12 * 12 &&
                              (x - 32) * (x - 32) + (y - 32) * (y - 32) < 22 * 22;
            pixels[at] = ring ? 150 : 25;
            pixels[at + 1] = ring ? 245 : 45;
            pixels[at + 2] = ring ? 255 : 65;
            pixels[at + 3] = 255;
        }
    return true;
}
__declspec(dllexport) int FixtureAvatarReads() {
    return reads;
}
}
