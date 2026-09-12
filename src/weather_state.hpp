#pragma once
#include "weather_options.hpp"
#include <awareness/OverlayApi.hpp>
#include <array>
#include <cmath>

namespace awareness::weather {
inline constexpr std::size_t MaxEmitters = 3;
inline constexpr std::uint32_t InvalidIndex = 0xFFFFFFFFu;
inline constexpr const char *Paths[]{"particles/rain_fx/rain.vpcf", "particles/rain_fx/snow.vpcf",
                                     "particles/rain_fx/ash.vpcf"};
struct Token {
    std::uint64_t epoch{};
    std::uintptr_t descriptor{};
    std::uint32_t index{InvalidIndex}, hint{};
    bool placed{};
    explicit operator bool() const noexcept { return epoch && index != InvalidIndex; }
};
struct Context {
    std::uint64_t epoch{};
    Vector3 origin{};
    bool managerValid{}, originValid{};
};
// Normal updates follow network completion. Cleanup must also progress in menus,
// where the game still dispatches frames but no network-completion stage occurs.
inline constexpr bool NeedsTick(bool networkEnd, bool cleanup, bool quiescent) noexcept {
    return cleanup ? !quiescent : networkEnd;
}
enum class Residency { Pending, Ready, Failed };
enum class Ownership { Owned, Gone, Unreadable };
inline bool PositionValid(Vector3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::abs(v.x) < 1e7f &&
           std::abs(v.y) < 1e7f && std::abs(v.z) < 1e7f;
}
// Fixed storage and time-based budgets; the backend owns the actual native API.
// Every operation on an existing effect must prove its full ownership token.
class Controller {
    struct Slot {
        Token token;
        Vector3 position{};
    };
    std::array<Slot, MaxEmitters> slots_{};
    std::uint64_t epoch_{};
    std::uint32_t kind_{InvalidIndex};
    double lastTime_{}, nextCreate_{}, nextMove_{};
    bool clock_{};
    Diagnostics diagnostics_;
    template <class Backend> void Retire(Slot &slot, Backend &backend) noexcept {
        if (!slot.token)
            return;
        const auto ownership = backend.Owns(slot.token);
        if (ownership == Ownership::Unreadable)
            return;
        if (ownership == Ownership::Owned && !backend.Destroy(slot.token))
            return;
        if (ownership == Ownership::Owned)
            ++diagnostics_.retired;
        slot = {};
    }
    void Count() noexcept {
        diagnostics_.active = 0;
        for (const auto &slot : slots_)
            diagnostics_.active += !!slot.token;
    }

  public:
    const Diagnostics &Report() const noexcept { return diagnostics_; }
    bool Empty() const noexcept {
        for (const auto &slot : slots_)
            if (slot.token)
                return false;
        return true;
    }
    template <class Backend>
    void Update(const Context &context, Options options, double now, Backend &backend) noexcept {
        if (!std::isfinite(now))
            return;
        if (clock_ && now < lastTime_)
            nextCreate_ = nextMove_ = now;
        lastTime_ = now;
        clock_ = true;
        if (context.epoch != epoch_) {
            // The engine owns effects discarded at a map/manager generation change.
            // Never destroy an old ID in a replacement manager, even if IDs repeat.
            slots_ = {};
            epoch_ = context.epoch;
            kind_ = InvalidIndex;
            nextCreate_ = nextMove_ = now;
        }
        if (!Valid(options))
            options = {};
        const bool enabled =
            options.enabled && context.managerValid && context.originValid && PositionValid(context.origin);
        if (!enabled || kind_ != options.kind) {
            for (auto &slot : slots_)
                Retire(slot, backend);
            Count();
            if (!Empty()) {
                diagnostics_.status = Status::Stopping;
                return;
            }
            kind_ = options.kind;
            if (!enabled) {
                diagnostics_.status = options.enabled ? Status::WaitingForScene : Status::Disabled;
                return;
            }
        }
        const auto desired = static_cast<std::size_t>(options.density + 1);
        for (std::size_t i = desired; i < slots_.size(); ++i)
            Retire(slots_[i], backend);
        // Changes in vector order and engine-side expiry do not imply ownership.
        for (std::size_t i = 0; i < desired; ++i)
            if (slots_[i].token && backend.Owns(slots_[i].token) == Ownership::Gone)
                slots_[i] = {};
        const auto ready = backend.Asset(options.kind, now);
        diagnostics_.status = ready == Residency::Failed ? Status::Retrying : Status::Preparing;
        const bool updatePositions = now >= nextMove_;
        if (updatePositions)
            nextMove_ = now + 1.0 / 30.0;
        for (std::size_t i = 0; i < desired; ++i) {
            // These stock assets use a transformed sphere at CP0. Keep one small
            // local volume; no screen-space layers, camera fill, or respawn grid.
            const auto position = context.origin;
            auto &slot = slots_[i];
            if (!slot.token && ready == Residency::Ready && now >= nextCreate_) {
                nextCreate_ = now + .15; // At most one native allocation per tick.
                Token created;
                if (backend.Create(options.kind, position, created) && created && created.epoch == epoch_) {
                    slot = {created, position};
                    ++diagnostics_.created;
                } else {
                    nextCreate_ = now + 2;
                    ++diagnostics_.failures;
                    diagnostics_.status = Status::Retrying;
                }
            }
            if (!slot.token || !updatePositions)
                continue;
            const auto delta = position - slot.position;
            const float length = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
            if ((!slot.token.placed || length > .0625f) && backend.Owns(slot.token) == Ownership::Owned) {
                if (backend.Move(slot.token, position))
                    slot.position = position;
                else {
                    Retire(slot, backend);
                    ++diagnostics_.failures;
                    nextCreate_ = now + 2;
                }
            }
        }
        Count();
        bool allPlaced = diagnostics_.active == desired;
        for (std::size_t i = 0; i < desired; ++i)
            allPlaced &= slots_[i].token.placed;
        if (allPlaced)
            diagnostics_.status = Status::Active;
    }
};
} // namespace awareness::weather
