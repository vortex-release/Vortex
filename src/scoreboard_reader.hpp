#pragma once
#include "scoreboard.hpp"
#include "spectator_reader.hpp"
namespace awareness::cs2 {
// Schema fields from the current bundled client_dll.json (build14181).
namespace scoreboard_layout {
constexpr std::uintptr_t Weapons = 0x48, Armor = 0x1CA4, Helmet = 0x49;
}
enum class HudRead { Present, Gone, Unreadable };
struct ScoreboardHud {
    std::uintptr_t hud{}, panel{};
};
inline HudRead ReadScoreboardHud(const Memory &m, std::uintptr_t slot, ScoreboardHud &out) noexcept {
    out = {};
    std::uintptr_t after{}, panelAfter{};
    if (!m.Read(slot, out.hud))
        return HudRead::Unreadable;
    if (!out.hud)
        return m.Read(slot, after) && !after ? HudRead::Gone : HudRead::Unreadable;
    if (!m.Field(out.hud, 8, out.panel) || !m.Read(slot, after) || after != out.hud ||
        !m.Field(out.hud, 8, panelAfter) || panelAfter != out.panel)
        return HudRead::Unreadable;
    return out.panel ? HudRead::Present : HudRead::Gone;
}
inline bool ReadScoreboard(const Memory &m, std::uintptr_t list, scoreboard::Frame &out) noexcept {
    out = {};
    if (!list)
        return false;
    for (std::uint32_t slot = 1; slot <= out.players.size(); ++slot) {
        const auto controller = EntityAt(m, list, slot);
        scoreboard::Player p;
        std::uint32_t ch{}, ph{}, check{};
        std::uint8_t team{}, life{};
        int health{};
        if (!FullHandle(m, controller, ch) || (ch & offsets::EntryMask) != slot ||
            !m.Field(controller, offsets::ControllerPawn, ph))
            continue;
        const auto pawn = CheckedEntity(m, list, ph);
        if (!pawn || !m.Field(pawn, offsets::Team, team) || team < 2 || team > 3 ||
            !m.Field(pawn, offsets::LifeState, life) || life || !m.Field(pawn, offsets::Health, health) ||
            health <= 0 || health > 10000)
            continue;
        p.controller = slot;
        p.handle = ch;
        m.Field(controller, spectator_layout::SteamId, p.steamId);
        int armor{};
        std::uintptr_t items{}, services{};
        std::uint8_t flag{};
        p.armor = m.Field(pawn, scoreboard_layout::Armor, armor) && armor > 0 && armor <= 200;
        if (m.Field(pawn, offsets::ItemServices, items)) {
            p.helmet = m.Field(items, scoreboard_layout::Helmet, flag) && flag == 1;
            p.defuser = m.Field(items, offsets::HasDefuser, flag) && flag == 1;
        }
        if (m.Field(pawn, offsets::WeaponServices, services)) {
            // C_NetworkUtlVectorBase<CHandle<C_BasePlayerWeapon>>: int count, padding, data pointer.
            struct Handles {
                std::int32_t count{}, padding{};
                std::uintptr_t data{};
            } handles;
            std::uint32_t active{};
            if (m.Field(services, scoreboard_layout::Weapons, handles) && handles.count >= 0 && handles.count <= 64 &&
                (!handles.count || handles.data) && m.Field(services, offsets::ActiveWeapon, active)) {
                for (int i = 0; i < handles.count && p.count < p.items.size(); ++i) {
                    std::uint32_t h{}, owner{};
                    std::uint16_t definition{};
                    if (!m.Field(handles.data, static_cast<std::uintptr_t>(i) * 4, h))
                        break;
                    const auto weapon = CheckedEntity(m, list, h);
                    if (!weapon || !m.Field(weapon, offsets::EntityOwner, owner) || owner != ph ||
                        !m.Field(weapon, offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition,
                                 definition) ||
                        !scoreboard::Equipment(definition) || !FullHandle(m, weapon, check) || check != h)
                        continue;
                    p.items[p.count++] = {definition, h == active};
                }
            }
        }
        if (!FullHandle(m, pawn, check) || check != ph || !FullHandle(m, controller, check) || check != ch)
            continue;
        std::sort(p.items.begin(), p.items.begin() + p.count,
                  [](auto a, auto b) { return scoreboard::Order(a.definition) < scoreboard::Order(b.definition); });
        out.players[out.count++] = p;
    }
    return true;
}
} // namespace awareness::cs2
