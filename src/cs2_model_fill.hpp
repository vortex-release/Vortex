#pragma once
#include <awareness/EffectsApi.hpp>

namespace awareness::cs2 {
namespace model {
struct Targets;
}
// Internal singleton, matching the overlay's single runtime. No exported ABI change.
// Update runs on Present; Draw runs on Source 2's render workers.
HRESULT StartModelFill() noexcept;
void PauseModelFill() noexcept;
bool UpdateModelFill(const FrameSnapshot &, const Configuration &, const EffectsConfiguration &, bool verified,
                     EffectsState &, bool shaded = true, const model::Targets *cached = nullptr) noexcept;
struct ModelFillDiagnostics {
    unsigned selectedObjects{}, callbacks{}, selectedCallbacks{}, rejectedOwnership{}, rejectedModels{},
        generatedPackets{};
    bool flatReady{}, litReady{}, hiddenBridgeReady{};
    unsigned hiddenQueued{}, hiddenMatched{}, hiddenCaptured{}, hiddenDropped{};
};
ModelFillDiagnostics GetModelFillDiagnostics() noexcept;
HRESULT StopModelFill() noexcept;
} // namespace awareness::cs2
