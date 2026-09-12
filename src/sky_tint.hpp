#pragma once
#include "cs2_glow.hpp"
#include "visual_styles.hpp"
namespace awareness::cs2 {
struct SkyStatus {
    unsigned applied{}, failed{};
};
class SkyTint {
    struct Field {
        std::uint32_t before{}, last{};
        bool owned{};
    };
    struct Entry {
        GlowIdentity owner;
        std::array<Field, 4> fields{};
    };
    std::array<Entry, 16> entries_{};
    std::uintptr_t list_{};
    unsigned cursor_{1};
    double nextScan_{}, previous_{};
    static std::uintptr_t Offset(unsigned i) { return i < 3 ? offsets::SkyTint + i : offsets::SkyBrightness; }
    static std::uint8_t Size(unsigned i) { return i < 3 ? 1 : 4; }
    static bool Read(const GlowAccess &a, const Entry &e, unsigned i, std::uint32_t &out) {
        if (i < 3) {
            std::uint8_t v{};
            const bool ok = a.memory.Field(e.owner.pawn, Offset(i), v);
            out = v;
            return ok;
        }
        return a.memory.Field(e.owner.pawn, Offset(i), out);
    }
    static bool Restore(const GlowAccess &a, Entry &e) {
        if (!e.owner.pawn)
            return true;
        if (!SameGlowEntity(a.memory, e.owner)) {
            e = {};
            return true;
        }
        bool done = true;
        for (unsigned i = 0; i < 4; ++i) {
            auto &field = e.fields[i];
            if (!field.owned)
                continue;
            std::uint32_t current{};
            if (!Read(a, e, i, current)) {
                done = false;
                continue;
            }
            if (current != field.last) {
                field.owned = false;
                continue;
            }
            if (!SameGlowEntity(a.memory, e.owner)) {
                e = {};
                return true;
            }
            const auto result =
                a.exchange(a.memory.context, e.owner.pawn + Offset(i), field.last, field.before, Size(i));
            if (result == ExchangeResult::Unavailable) {
                done = false;
                continue;
            }
            field.owned = false;
        }
        if (done)
            e = {};
        return done;
    }
    static bool Apply(const GlowAccess &a, Entry &e, const styling::Sky &s) {
        const float channels[]{s.tint.r, s.tint.g, s.tint.b};
        for (unsigned i = 0; i < 4; ++i) {
            if (!SameGlowEntity(a.memory, e.owner))
                return false;
            std::uint32_t current{};
            if (!Read(a, e, i, current))
                return false;
            auto &field = e.fields[i];
            if (!field.owned || current != field.last)
                field.before = current;
            std::uint32_t desired{};
            if (i < 3)
                desired = static_cast<std::uint32_t>(std::lround(field.before * channels[i]));
            else {
                const float original = std::bit_cast<float>(field.before);
                if (!std::isfinite(original) || original < 0 || original > 100)
                    return false;
                desired = std::bit_cast<std::uint32_t>(original * s.brightness);
            }
            if (field.owned && current == field.last && current == desired)
                continue;
            if (a.exchange(a.memory.context, e.owner.pawn + Offset(i), current, desired, Size(i)) !=
                ExchangeResult::Applied)
                return false;
            field.last = desired;
            field.owned = true;
        }
        return true;
    }

  public:
    bool Clear(const GlowAccess &a) {
        bool done = true;
        for (auto &e : entries_)
            done = Restore(a, e) && done;
        cursor_ = 1;
        nextScan_ = previous_ = 0;
        list_ = 0;
        return done;
    }
    SkyStatus Update(const GlowAccess &a, std::uintptr_t list, const styling::Sky &s, double now, bool ready) {
        SkyStatus result;
        if (!ready || !list || !s.enabled || !styling::Valid(s) || !std::isfinite(now)) {
            result.failed = Clear(a) ? 0 : 1;
            return result;
        }
        if (list_ != list) {
            if (!Clear(a))
                ++result.failed;
            list_ = list;
        }
        for (auto &e : entries_)
            if (e.owner.pawn && !SameGlowEntity(a.memory, e.owner))
                e = {};
        if (now < previous_) {
            cursor_ = 1;
            nextScan_ = 0;
        }
        previous_ = now;
        // HighestEntity excludes client-only entities, including live env_sky records.
        // Walk allocated chunks with a fixed slot budget on the worker; sparse gaps
        // cost only one pointer read per missing chunk and no per-slot probing.
        if (now >= nextScan_) {
            unsigned budget{};
            while (cursor_ <= offsets::EntryMask && budget < 128) {
                std::uintptr_t chunk{};
                const auto end = (cursor_ | 511u) + 1;
                if (!a.memory.Field(list, offsets::EntityTable + sizeof(std::uintptr_t) * (cursor_ >> 9), chunk) ||
                    !chunk) {
                    cursor_ = end;
                    continue;
                }
                while (cursor_ < end && budget < 128) {
                    const auto index = cursor_++;
                    ++budget;
                    std::uintptr_t entity{};
                    if (!a.memory.Field(chunk, offsets::EntityStride * (index & 511), entity) || !entity)
                        continue;
                    GlowIdentity owner{list, entity};
                    std::uintptr_t name{};
                    std::array<char, sizeof("env_sky")> type{};
                    if (!a.memory.Field(entity, offsets::Identity, owner.identity) ||
                        !a.memory.Field(owner.identity, 0x10, owner.handle) || owner.handle == 0xffffffff ||
                        (owner.handle & offsets::EntryMask) != index ||
                        !a.memory.Field(owner.identity, offsets::DesignerName, name) || !a.memory.Read(name, type) ||
                        std::memcmp(type.data(), "env_sky", sizeof("env_sky")) || !SameGlowEntity(a.memory, owner))
                        continue;
                    bool found = false;
                    for (const auto &e : entries_)
                        found |= e.owner.list == list && e.owner.pawn == entity && e.owner.handle == owner.handle;
                    if (!found)
                        for (auto &e : entries_)
                            if (!e.owner.pawn) {
                                e.owner = owner;
                                break;
                            }
                }
            }
            if (cursor_ > offsets::EntryMask) {
                cursor_ = 1;
                nextScan_ = now + 1;
            }
        }
        for (auto &entry : entries_)
            if (entry.owner.pawn) {
                // A previous map can remain allocated during a transition. Keep
                // retrying restoration there, never reapply the new map's tint.
                if (entry.owner.list != list) {
                    if (!Restore(a, entry))
                        ++result.failed;
                    continue;
                }
                if (Apply(a, entry, s))
                    ++result.applied;
                else {
                    ++result.failed;
                    Restore(a, entry);
                }
            }
        return result;
    }
};
} // namespace awareness::cs2
