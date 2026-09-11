#pragma once
#include "Math.hpp"
#include "TargetBone.hpp"
#include <cstdint>
#include <type_traits>
#if defined(_WIN32)
#include <Windows.h>
struct IDXGISwapChain;
#if defined(AWARENESS_EXPORTS)
#define AWARENESS_API extern "C" __declspec(dllexport)
#else
#define AWARENESS_API extern "C" __declspec(dllimport)
#endif
#endif

namespace awareness {
inline constexpr std::uint32_t ApiVersion = 1;
inline constexpr std::uint32_t FrameVersion = 3;
inline constexpr std::uint32_t MaxEntities = 64;
struct Color {
    float r{}, g{}, b{}, a{1.f};
};
enum class TeamFilter : std::uint32_t { All, OpponentsOnly, TeammatesOnly };
enum class HealthBar : std::uint32_t { Vertical, Horizontal };
struct Configuration {
    std::uint32_t size{sizeof(Configuration)}, version{ApiVersion};
    std::uint32_t enabled{1}, boxes{1}, lines{1}, healthBars{1}, names{1}, distances{1};
    std::uint32_t toggleKey{0x24}, alternateToggleKey{0}; // Home; Insert is reserved for the GUI
    TeamFilter teamFilter{TeamFilter::All};
    HealthBar healthBar{HealthBar::Vertical};
    std::uint32_t colorBoxesByHealth{0}, clearOverlayDepth{1};
    float boxThickness{1.5f}, lineThickness{1.f}, barThickness{5.f}, fontPixels{16.f};
    float worldUnitsPerMeter{1.f}, fadeStartMeters{60.f}, maxDistanceMeters{120.f};
    float opacity{.95f}, lowHealthThreshold{.25f}, mediumHealthThreshold{.60f};
    std::uint32_t staleFrameMilliseconds{1000}; // 0 disables the stale-data check
    Color teammate{.22f, .72f, 1.f, 1.f}, opponent{1.f, .38f, .30f, 1.f}, neutral{.85f, .85f, .85f, 1.f};
    Color healthy{.25f, .88f, .46f, 1.f}, medium{1.f, .75f, .22f, 1.f}, low{1.f, .22f, .22f, 1.f};
    Color text{.94f, .96f, 1.f, 1.f}, outline{.02f, .025f, .04f, .9f}, barBackground{.04f, .05f, .07f, .85f};
    char fontPath[260]{}; // UTF-8 .ttf/.otf path; empty selects the built-in font
};
struct EntitySnapshot {
    std::uint32_t id{}, valid{}, dormant{};
    std::int32_t team{}; // 0 = neutral / unknown
    float health{}, maxHealth{100.f};
    Vector3 origin{}, mins{}, maxs{}; // local axis-aligned mins/maxs translated by origin
    char name[64]{};                  // UTF-8, copied and terminated by SubmitFrame
};
struct FrameSnapshot {
    std::uint32_t size{sizeof(FrameSnapshot)}, version{FrameVersion};
    std::uint32_t entityCount{}, localEntityId{0xFFFFFFFFu};
    std::int32_t localTeam{};
    Vector3 cameraOrigin{};
    Matrix4x4 viewProjection{};
    Viewport viewport{}; // width = height = 0 means the entire current back buffer
    EntitySnapshot entities[MaxEntities]{};
    // Parallel to entities. Zero means unknown/non-firearm. Version-1 hosts remain supported.
    std::uint32_t weaponDefinitionIndices[MaxEntities]{};
    // Version 3: world-space skeletal nodes, indexed by actual engine bone ID.
    // Missing nodes skip tracking; they never fall back to the entity origin.
    EntityBones bones[MaxEntities]{};
};
struct Statistics {
    std::uint32_t size{sizeof(Statistics)}, version{ApiVersion};
    std::uint64_t presentCalls{}, renderedFrames{};
    std::uint32_t drawnEntities{}, enabled{};
    std::int32_t lastRenderResult{}; // HRESULT, including device loss from Present
};
static_assert(std::is_trivially_copyable_v<FrameSnapshot> && std::is_standard_layout_v<Configuration>);
} // namespace awareness

#if defined(_WIN32)
// Lifecycle calls must be on the Present thread, outside DllMain, with all threads
// calling the shared Present implementation stopped. See README for the unload contract.
AWARENESS_API HRESULT __cdecl AwarenessInitialize(IDXGISwapChain *, const awareness::Configuration *) noexcept;
// These copy data; the caller retains ownership. Do not race either against Shutdown.
AWARENESS_API HRESULT __cdecl AwarenessSubmitFrame(const awareness::FrameSnapshot *) noexcept;
AWARENESS_API HRESULT __cdecl AwarenessSetConfiguration(const awareness::Configuration *) noexcept;
AWARENESS_API HRESULT __cdecl AwarenessGetConfiguration(awareness::Configuration *) noexcept;
AWARENESS_API HRESULT __cdecl AwarenessGetStatistics(awareness::Statistics *) noexcept;
AWARENESS_API HRESULT __cdecl AwarenessShutdown() noexcept;
// Capture the next DX11 Present for a window in this process; no swap-chain argument needed.
AWARENESS_API HRESULT __cdecl AwarenessStartAutomatic(HWND, const awareness::Configuration *) noexcept;
AWARENESS_API HRESULT __cdecl AwarenessSetMenuVisible(BOOL visible) noexcept;
#endif
