#include <awareness/OverlayApi.hpp>
#include <awareness/EffectsApi.hpp>
#include <cstdio>
#include <cstring>
#include "configuration.hpp"

int main() {
    using namespace awareness;
    int checks{}, failures{};
    const auto check = [&](bool result, const char *message) {
        ++checks;
        if (!result) {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", message);
        }
    };
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    auto *pages = static_cast<unsigned char *>(
        VirtualAlloc(nullptr, 2ull * info.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!pages)
        return 1;
    DWORD oldProtection{};
    if (!VirtualProtect(pages + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &oldProtection)) {
        VirtualFree(pages, 0, MEM_RELEASE);
        return 1;
    }
    struct Header {
        std::uint32_t size, version;
    };
    auto *header = pages + info.dwPageSize - sizeof(Header);
    const Header oldLayout{sizeof(Header), ApiVersion};
    std::memcpy(header, &oldLayout, sizeof(oldLayout));
    check(AwarenessSetEffectsConfiguration(reinterpret_cast<const EffectsConfiguration *>(header)) == E_INVALIDARG,
          "effects configuration header rejected without reading body");
    check(AwarenessGetEffectsConfiguration(reinterpret_cast<EffectsConfiguration *>(header)) == E_INVALIDARG,
          "effects getter header rejected without writing body");
    check(AwarenessGetEffectsState(reinterpret_cast<EffectsState *>(header)) == E_INVALIDARG,
          "effects status header rejected without writing body");
    EffectsConfiguration effects;
    check(AwarenessSetEffectsConfiguration(&effects) == HRESULT_FROM_WIN32(ERROR_NOT_READY),
          "effects API checks lifecycle");
    effects.glowWidth = 50;
    check(AwarenessSetEffectsConfiguration(&effects) == E_INVALIDARG, "excessive effect radius rejected");
    // Any access beyond the supplied header faults into the protected next page.
    check(AwarenessSetConfiguration(reinterpret_cast<const Configuration *>(header)) == E_INVALIDARG,
          "old configuration layout rejected without reading its body");
    check(AwarenessSubmitFrame(reinterpret_cast<const FrameSnapshot *>(header)) == E_INVALIDARG,
          "old frame layout rejected without reading its body");
    check(AwarenessGetStatistics(reinterpret_cast<Statistics *>(header)) == E_INVALIDARG,
          "old statistics layout rejected without reading or writing its body");
    check(AwarenessGetConfiguration(reinterpret_cast<Configuration *>(header)) == E_INVALIDARG,
          "old getter layout rejected without writing its body");
    const Header wrongVersion{sizeof(Configuration), ApiVersion + 1};
    std::memcpy(header, &wrongVersion, sizeof(wrongVersion));
    check(AwarenessSetConfiguration(reinterpret_cast<const Configuration *>(header)) == E_INVALIDARG,
          "unknown configuration version rejected before accessing fields");
    check(AwarenessSetConfiguration(nullptr) == E_INVALIDARG, "null configuration rejected");
    check(AwarenessGetConfiguration(nullptr) == E_INVALIDARG, "null configuration output rejected");
    Configuration config;
    check(AwarenessGetConfiguration(&config) == HRESULT_FROM_WIN32(ERROR_NOT_READY), "getter observes lifecycle state");
    check(AwarenessSetConfiguration(&config) == HRESULT_FROM_WIN32(ERROR_NOT_READY),
          "valid configuration reaches lifecycle check");
    config.fontPath[0] = 'x';
    std::memset(config.fontPath, 'x', sizeof(config.fontPath));
    check(AwarenessSetConfiguration(&config) == E_INVALIDARG, "unterminated font path rejected");
    constexpr auto legacySize = offsetof(FrameSnapshot, weaponDefinitionIndices);
    {
        FrameSnapshot frame2;
        frame2.size = static_cast<std::uint32_t>(offsetof(FrameSnapshot, bones));
        frame2.version = 2;
        frame2.weaponDefinitionIndices[0] = 7;
        frame2.bones[0].validMask = 255;
        FrameSnapshot upgraded;
        check(CopySubmittedFrame(&frame2, upgraded) && upgraded.weaponDefinitionIndices[0] == 7 &&
                  !upgraded.bones[0].validMask && upgraded.version == 3,
              "version-2 frames retain weapon data and clear unsupported bones");
    }
    const auto legacyPages = (legacySize + info.dwPageSize - 1) / info.dwPageSize;
    auto *legacyAllocation = static_cast<unsigned char *>(
        VirtualAlloc(nullptr, (legacyPages + 1) * info.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!legacyAllocation)
        return 1;
    VirtualProtect(legacyAllocation + legacyPages * info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &oldProtection);
    auto *legacy = reinterpret_cast<FrameSnapshot *>(legacyAllocation + legacyPages * info.dwPageSize - legacySize);
    FrameSnapshot oldFrame;
    oldFrame.size = static_cast<std::uint32_t>(legacySize);
    oldFrame.version = ApiVersion;
    oldFrame.entityCount = 1;
    oldFrame.entities[0].id = 17;
    std::memcpy(legacy, &oldFrame, legacySize);
    FrameSnapshot converted;
    converted.weaponDefinitionIndices[0] = 7;
    check(CopySubmittedFrame(legacy, converted) && converted.version == FrameVersion &&
              converted.entities[0].id == 17 && converted.weaponDefinitionIndices[0] == 0,
          "version-1 frame converts without reading beyond its protected boundary");
    check(AwarenessSubmitFrame(legacy) == HRESULT_FROM_WIN32(ERROR_NOT_READY), "legacy host frames remain accepted");
    VirtualFree(legacyAllocation, 0, MEM_RELEASE);
    check(SUCCEEDED(AwarenessShutdown()), "bootstrap shuts down cleanly without initialization");
    VirtualFree(pages, 0, MEM_RELEASE);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
