#pragma once
#include "cs2_reader.hpp"

namespace awareness::cs2 {
struct KillSample {
    std::uintptr_t controller{}, services{}, identity{};
    std::uint32_t handle{};
    std::int32_t count{};
};
inline bool ReadKillSample(const Memory &memory, const Globals &globals, KillSample &out) noexcept {
    out = {};
    KillSample next;
    std::uintptr_t list{}, verifyController{}, verifyServices{};
    std::uint32_t verifyHandle{};
    if (!memory.Read(globals.localControllerSlot, next.controller) || !memory.Read(globals.entitySlot, list) ||
        !memory.Field(next.controller, offsets::Identity, next.identity) ||
        !memory.Field(next.identity, 0x10, next.handle) || next.handle == 0xffffffffu ||
        EntityAt(memory, list, next.handle) != next.controller ||
        !memory.Field(next.controller, offsets::ControllerActions, next.services) || !next.services ||
        !memory.Field(next.services, offsets::RoundKills, next.count) || next.count < 0 || next.count > 10000 ||
        !memory.Read(globals.localControllerSlot, verifyController) || verifyController != next.controller ||
        !memory.Field(next.controller, offsets::ControllerActions, verifyServices) || verifyServices != next.services ||
        !memory.Field(next.identity, 0x10, verifyHandle) || verifyHandle != next.handle)
        return false;
    out = next;
    return true;
}
class KillEvents {
    KillSample previous_{};
    bool valid_{};

  public:
    void Reset() noexcept {
        valid_ = false;
        previous_ = {};
    }
    unsigned Update(const KillSample &sample, bool valid, bool enabled) noexcept {
        if (!valid || !enabled || !sample.controller || !sample.services || sample.count < 0 || sample.count > 10000) {
            Reset();
            return 0;
        }
        const bool same = valid_ && sample.controller == previous_.controller &&
                          sample.identity == previous_.identity && sample.services == previous_.services &&
                          sample.handle == previous_.handle;
        const auto delta = same ? sample.count - previous_.count : 0;
        previous_ = sample;
        valid_ = true;
        // Initialization, reconnects and round resets only establish a baseline.
        // Drop implausible corrections rather than replaying a stale scoreboard.
        return delta > 0 && delta <= 8 ? static_cast<unsigned>(delta) : 0;
    }
};
} // namespace awareness::cs2
