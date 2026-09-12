#pragma once
#include "cosmetics_options.hpp"
#include "economy_catalog.hpp"
#include <cstdint>
#include <Windows.h>

namespace awareness::cosmetics {
enum class NativeState : std::uint8_t {
    Disabled,
    UnsupportedBuild,
    LoadingCatalog,
    CatalogError,
    WaitingPlayer,
    WaitingItem,
    LoadingModel,
    Ready,
    Applying,
    InvalidSelection,
    NativeFault,
    Restoring
};
struct NativeStatus {
    NativeState state{NativeState::Disabled};
    bool weapons{}, knives{}, gloves{}, agents{};
    std::uint32_t tracked{}, applied{}, restores{}, faults{};
};
const char *Name(NativeState state) noexcept;
bool Initialize(std::uintptr_t client) noexcept;
void Configure(const Options &) noexcept;
// Invoked only by the centrally verified FrameStageNotify dispatcher after original(PostDataEnd=7).
void Tick(int stage) noexcept;
// Requests restore on the next game-thread dispatch; never calls game APIs on the render thread.
void Restore() noexcept;
HRESULT StopNative() noexcept;
// Called after restore has drained and the game-thread dispatcher is quiescent.
void Shutdown() noexcept;
NativeStatus Status() noexcept;
CatalogController &CatalogRuntime() noexcept;
} // namespace awareness::cosmetics
