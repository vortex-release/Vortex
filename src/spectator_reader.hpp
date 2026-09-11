#pragma once
#include "combat_reader.hpp"
namespace awareness::cs2 {
// Schema snapshot build 14181: CBasePlayerController / CCSPlayerController,
// C_BasePlayerPawn and CPlayer_ObserverServices. Full handles protect slot reuse.
namespace spectator_layout {
inline constexpr std::uintptr_t Pawn = 0x6bc, ObserverPawn = 0x918, Connected = 0x6ec, SteamId = 0x780;
inline constexpr std::uintptr_t Services = 0x1220, Mode = 0x48, Target = 0x4c;
} // namespace spectator_layout
struct Spectator {
    std::uint32_t handle{};
    std::uint64_t steamId{};
    char name[64]{};
    std::uint8_t mode{};
};
struct SpectatorFrame {
    std::array<Spectator, MaxEntities> entries{};
    std::uint32_t count{};
    bool observing{};
};
inline std::uintptr_t CheckedEntity(const Memory &m, std::uintptr_t list, std::uint32_t h) noexcept {
    std::uint32_t current{};
    const auto entity = EntityAt(m, list, h);
    return h && h != 0xffffffff && FullHandle(m, entity, current) && current == h ? entity : 0;
}
inline std::uint32_t ObservedTarget(const Memory &m, std::uintptr_t list, std::uintptr_t pawn,
                                    std::uint8_t &mode) noexcept {
    std::uintptr_t services{};
    std::uint32_t target{};
    mode = 0;
    if (!m.Field(pawn, spectator_layout::Services, services) || !m.Field(services, spectator_layout::Mode, mode) ||
        (mode != 2 && mode != 3) || !m.Field(services, spectator_layout::Target, target) ||
        !CheckedEntity(m, list, target))
        return 0;
    return target;
}
inline std::uintptr_t ObserverPawn(const Memory &m, std::uintptr_t list, std::uintptr_t controller) noexcept {
    std::uint32_t h{};
    if (m.Field(controller, spectator_layout::Pawn, h))
        if (const auto pawn = CheckedEntity(m, list, h))
            return pawn;
    return m.Field(controller, spectator_layout::ObserverPawn, h) ? CheckedEntity(m, list, h) : 0;
}
inline bool ReadSpectators(const Memory &m, std::uintptr_t list, std::uintptr_t localController,
                           std::uintptr_t localPawn, SpectatorFrame &out) noexcept {
    out = {};
    if (!list || !localController)
        return false;
    std::uint32_t target{};
    FullHandle(m, localPawn, target);
    std::uint8_t mode{};
    if (const auto watched = ObservedTarget(m, list, ObserverPawn(m, list, localController), mode)) {
        target = watched;
        out.observing = true;
    }
    if (!CheckedEntity(m, list, target))
        return false;
    for (std::uint32_t slot = 1; slot <= MaxEntities; ++slot) {
        const auto controller = EntityAt(m, list, slot);
        std::uint32_t h{}, after{};
        if (!controller || controller == localController || !FullHandle(m, controller, h) ||
            (h & offsets::EntryMask) != slot)
            continue;
        const auto pawn = ObserverPawn(m, list, controller);
        if (ObservedTarget(m, list, pawn, mode) != target)
            continue;
        Spectator entry;
        entry.handle = h;
        entry.mode = mode;
        m.Field(controller, spectator_layout::SteamId, entry.steamId);
        std::array<char, 64> name{};
        if (!m.Field(controller, offsets::PlayerName, name) || !FullHandle(m, controller, after) || after != h)
            continue;
        std::memcpy(entry.name, name.data(), name.size());
        entry.name[63] = 0;
        for (auto &c : entry.name)
            if (static_cast<unsigned char>(c) < 32 && c != 0)
                c = ' ';
        if (!entry.name[0])
            std::memcpy(entry.name, "Player", 7);
        out.entries[out.count++] = entry;
    }
    return true;
}
} // namespace awareness::cs2
