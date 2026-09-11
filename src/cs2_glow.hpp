#pragma once
#include "cs2_reader.hpp"
#include "local_memory.hpp"
#include "entity_filter.hpp"
#include <awareness/EffectsApi.hpp>
#include <bit>
#include <intrin.h>

namespace awareness::cs2 {
enum class ExchangeResult { Applied, Conflict, Unavailable };
// Only aligned 1/4-byte fields are written, with SEH and writable-page validation.
// No protections are changed. CAS prevents an intervening engine update being lost.
inline ExchangeResult ExchangeGlowField(void *, std::uintptr_t address, std::uint32_t expected, std::uint32_t desired,
                                        std::uint8_t size) noexcept {
    if (!address || (size != 1 && size != 4) || address % size)
        return ExchangeResult::Unavailable;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void *>(address), &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return ExchangeResult::Unavailable;
    const auto start = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    if (address < start || info.RegionSize < size || address - start > info.RegionSize - size)
        return ExchangeResult::Unavailable;
#if defined(_MSC_VER)
    __try {
        const std::uint32_t previous =
            size == 1
                ? static_cast<unsigned char>(_InterlockedCompareExchange8(reinterpret_cast<volatile char *>(address),
                                                                          static_cast<char>(desired),
                                                                          static_cast<char>(expected)))
                : static_cast<std::uint32_t>(_InterlockedCompareExchange(reinterpret_cast<volatile long *>(address),
                                                                         std::bit_cast<long>(desired),
                                                                         std::bit_cast<long>(expected)));
        return previous == expected ? ExchangeResult::Applied : ExchangeResult::Conflict;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return ExchangeResult::Unavailable;
    }
#else
    return ExchangeResult::Unavailable;
#endif
}
struct GlowAccess {
    Memory memory;
    ExchangeResult (*exchange)(void *, std::uintptr_t, std::uint32_t, std::uint32_t,
                               std::uint8_t) noexcept {ExchangeGlowField};
};
struct GlowIdentity {
    std::uintptr_t list{}, pawn{}, identity{};
    std::uint32_t handle{};
};
inline bool SameGlowEntity(const Memory &memory, const GlowIdentity &owner) noexcept {
    std::uintptr_t identity{};
    std::uint32_t handle{};
    return owner.pawn && EntityAt(memory, owner.list, owner.handle) == owner.pawn &&
           memory.Field(owner.pawn, offsets::Identity, identity) && identity == owner.identity &&
           memory.Field(identity, 0x10, handle) && handle == owner.handle;
}
inline bool ResolveGlowEntity(const Memory &memory, std::uintptr_t list, const EntitySnapshot &sample,
                              GlowIdentity &result) noexcept {
    result = {list, EntityAt(memory, list, sample.id)};
    std::uint8_t team{}, life{};
    std::int32_t health{};
    return result.pawn && memory.Field(result.pawn, offsets::Identity, result.identity) &&
           memory.Field(result.identity, 0x10, result.handle) && memory.Field(result.pawn, offsets::Team, team) &&
           team == sample.team && memory.Field(result.pawn, offsets::LifeState, life) && life == 0 &&
           memory.Field(result.pawn, offsets::Health, health) && health > 0 && health <= 10000 &&
           SameGlowEntity(memory, result);
}
inline constexpr std::array<std::uintptr_t, 11> GlowOffsets{
    offsets::GlowColor,    offsets::GlowColor + 4, offsets::GlowColor + 8, offsets::GlowOverride,
    offsets::GlowType,     offsets::GlowTeam,      offsets::GlowRange,     offsets::GlowRangeMin,
    offsets::GlowFlashing, offsets::GlowEligible,  offsets::GlowEnabled};
inline constexpr std::array<std::uint8_t, 11> GlowSizes{4, 4, 4, 4, 4, 4, 4, 4, 1, 1, 1};
inline std::array<std::uint32_t, 11> GlowValues(Color color, float alpha, float range) noexcept {
    const auto byte = [](float x) { return static_cast<std::uint32_t>(std::lround(std::clamp(x, 0.f, 1.f) * 255.f)); };
    const auto rgba = byte(color.r) | (byte(color.g) << 8) | (byte(color.b) << 16) | (byte(color.a * alpha) << 24);
    return {std::bit_cast<std::uint32_t>(color.r),
            std::bit_cast<std::uint32_t>(color.g),
            std::bit_cast<std::uint32_t>(color.b),
            rgba,
            3,
            0,
            static_cast<std::uint32_t>(std::clamp(range, 1.f, 10000000.f)),
            0,
            0,
            1,
            1};
}
struct GlowUpdate {
    std::uint32_t applied{}, failed{};
    bool restored{true};
};
class ModelHighlight {
    struct Field {
        std::uint32_t before{}, last{};
        bool owned{};
    };
    struct Entry {
        GlowIdentity owner;
        std::array<Field, 11> fields{};
        bool touched{};
    };
    std::array<Entry, MaxEntities> entries_{};
    static bool ReadField(const GlowAccess &access, const Entry &entry, std::size_t i, std::uint32_t &value) noexcept {
        if (GlowSizes[i] == 1) {
            std::uint8_t byte{};
            const auto ok = access.memory.Field(entry.owner.pawn, offsets::Glow + GlowOffsets[i], byte);
            value = byte;
            return ok;
        }
        return access.memory.Field(entry.owner.pawn, offsets::Glow + GlowOffsets[i], value);
    }
    static bool Restore(const GlowAccess &access, Entry &entry) noexcept {
        if (!entry.owner.pawn)
            return true;
        if (!SameGlowEntity(access.memory, entry.owner)) {
            entry = {};
            return true;
        }
        bool done = true;
        // Disable the activation fields before restoring color/mode.
        for (std::size_t i = entry.fields.size(); i-- > 0;) {
            auto &field = entry.fields[i];
            if (!field.owned)
                continue;
            std::uint32_t current{};
            if (!ReadField(access, entry, i, current)) {
                done = false;
                continue;
            }
            if (current != field.last) {
                field.owned = false;
                continue;
            }
            if (!SameGlowEntity(access.memory, entry.owner)) {
                entry = {};
                return true;
            }
            const auto result =
                access.exchange(access.memory.context, entry.owner.pawn + offsets::Glow + GlowOffsets[i], field.last,
                                field.before, GlowSizes[i]);
            if (result == ExchangeResult::Unavailable) {
                done = false;
                continue;
            }
            field.owned = false; // A conflict belongs to the engine; never overwrite it.
        }
        if (done)
            entry = {};
        return done;
    }
    static bool Apply(const GlowAccess &access, Entry &entry, const std::array<std::uint32_t, 11> &values) noexcept {
        if (!SameGlowEntity(access.memory, entry.owner))
            return false;
        for (std::size_t i = 0; i < entry.fields.size(); ++i) {
            std::uint32_t current{};
            if (!ReadField(access, entry, i, current))
                return false;
            auto &field = entry.fields[i];
            if (field.owned && current == field.last && current == values[i])
                continue;
            // Preserve the engine's most recent value for later restoration.
            if (!field.owned || current != field.last)
                field.before = current;
            if (!SameGlowEntity(access.memory, entry.owner))
                return false;
            const auto result =
                access.exchange(access.memory.context, entry.owner.pawn + offsets::Glow + GlowOffsets[i], current,
                                values[i], GlowSizes[i]);
            if (result != ExchangeResult::Applied)
                return false;
            field.last = values[i];
            field.owned = true;
        }
        return SameGlowEntity(access.memory, entry.owner);
    }

  public:
    bool Clear(const GlowAccess &access) noexcept {
        bool done = true;
        for (auto &entry : entries_)
            if (!Restore(access, entry))
                done = false;
        return done;
    }
    GlowUpdate Update(const GlowAccess &access, std::uintptr_t list, const FrameSnapshot &frame,
                      const Configuration &config, const EffectsConfiguration &effects, bool ready) noexcept {
        GlowUpdate result;
        for (auto &entry : entries_)
            entry.touched = false;
        const bool enabled =
            ready && config.enabled && (effects.materialEnabled || effects.glowEnabled) && effects.materialColor.a > 0;
        if (enabled && frame.entityCount <= MaxEntities)
            for (std::uint32_t i = 0; i < frame.entityCount; ++i) {
                const auto &sample = frame.entities[i];
                const float alpha = EntityOpacity(frame, sample, config);
                if (alpha <= 0)
                    continue;
                GlowIdentity owner;
                if (!ResolveGlowEntity(access.memory, list, sample, owner)) {
                    ++result.failed;
                    continue;
                }
                Entry *found{};
                for (auto &entry : entries_)
                    if (entry.owner.pawn == owner.pawn && entry.owner.identity == owner.identity &&
                        entry.owner.handle == owner.handle) {
                        found = &entry;
                        break;
                    }
                if (!found)
                    for (auto &entry : entries_)
                        if (entry.owner.pawn && !SameGlowEntity(access.memory, entry.owner))
                            entry = {};
                if (!found)
                    for (auto &entry : entries_)
                        if (!entry.owner.pawn) {
                            entry.owner = owner;
                            found = &entry;
                            break;
                        }
                if (!found) {
                    ++result.failed;
                    continue;
                }
                found->touched = true;
                if (Apply(
                        access, *found,
                        GlowValues(effects.materialColor, alpha, config.maxDistanceMeters * config.worldUnitsPerMeter)))
                    ++result.applied;
                else {
                    ++result.failed;
                    if (!Restore(access, *found))
                        result.restored = false;
                }
            }
        for (auto &entry : entries_)
            if (!entry.touched && !Restore(access, entry))
                result.restored = false;
        return result;
    }
};
} // namespace awareness::cs2
