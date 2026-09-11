#pragma once
#include "combat_reader.hpp"
namespace awareness::cs2 {
inline int ReadControllerPing(const Memory &m, std::uintptr_t controller) noexcept {
    std::uint32_t before{}, after{}, ping{};
    if (!controller || !FullHandle(m, controller, before) || !m.Field(controller, offsets::ControllerPing, ping) ||
        ping > 5000 || !FullHandle(m, controller, after) || before != after)
        return -1;
    return static_cast<int>(ping);
}
} // namespace awareness::cs2
