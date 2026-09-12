#pragma once
#include "cosmetics_options.hpp"
#include <algorithm>
#include <span>

namespace awareness::cosmetics {
// Exact client-side values, never an inventory or server account representation.
struct ItemState {
    std::uint16_t definition{};
    std::uint64_t itemId{};
    std::uint32_t high{}, low{}, account{};
    std::uint8_t initialized{}, disallow{}, restoreMaterial{};
    std::array<char, 161> name{};
    std::int32_t paint{}, seed{}, statTrak{};
    float wear{};
    bool operator==(const ItemState &) const = default;
};
inline ItemState Desired(const ItemState &original, const Finish &f, std::uint16_t definition,
                         std::uint32_t account) noexcept {
    auto out = original;
    out.definition = definition;
    out.high = 0xffffffffu;
    out.low = 0;
    out.itemId = 0xffffffff00000000ull;
    out.account = account;
    out.initialized = 1;
    out.disallow = 1;
    out.restoreMaterial = 1;
    out.paint = static_cast<std::int32_t>(f.paintKit);
    out.seed = static_cast<std::int32_t>(f.seed);
    out.wear = f.wear;
    out.statTrak = f.statTrak ? static_cast<std::int32_t>(f.kills) : -1;
    if (f.name[0]) {
        out.name = {};
        std::copy_n(f.name.begin(), std::strlen(f.name.data()), out.name.begin());
    }
    return out;
}
struct AttributeState {
    bool present{};
    float value{};
    bool operator==(const AttributeState &) const = default;
};
using PaintAttributes = std::array<AttributeState, 3>;
inline PaintAttributes DesiredAttributes(const Finish &finish) noexcept {
    return {
        {{true, static_cast<float>(finish.paintKit)}, {true, static_cast<float>(finish.seed)}, {true, finish.wear}}};
}
using ReadPaintAttributes = bool (*)(void *, PaintAttributes &) noexcept;
using WritePaintAttribute = bool (*)(void *, unsigned, AttributeState) noexcept;
inline bool RestoreAttributes(void *context, const PaintAttributes &original, const PaintAttributes &applied,
                              ReadPaintAttributes read, WritePaintAttribute write) noexcept {
    PaintAttributes current;
    if (!read(context, current))
        return false;
    for (unsigned i = 0; i < current.size(); ++i) {
        if (current[i] != applied[i] || current[i] == original[i])
            continue;
        if (!write(context, i, original[i]))
            return false;
        PaintAttributes after;
        if (!read(context, after) || after[i] != original[i])
            return false;
    }
    return true;
}
inline bool CommitAttributes(void *context, const PaintAttributes &before, const PaintAttributes &after,
                             ReadPaintAttributes read, WritePaintAttribute write) noexcept {
    PaintAttributes applied = before;
    for (unsigned i = 0; i < before.size(); ++i) {
        if (before[i] == after[i])
            continue;
        // Track the attempted value too: an engine call may partially succeed before reporting failure.
        applied[i] = after[i];
        if (!write(context, i, after[i])) {
            RestoreAttributes(context, before, applied, read, write);
            return false;
        }
    }
    PaintAttributes observed;
    if (read(context, observed) && observed == after)
        return true;
    RestoreAttributes(context, before, applied, read, write);
    return false;
}
// Initialization/SOC/precache flags and network identity are not material inputs.
// Their drift must never tear down an otherwise unchanged composite material.
struct MaterialSignature {
    std::uint16_t definition{};
    std::int32_t paint{}, seed{}, statTrak{};
    float wear{};
    std::array<char, 161> name{};
    bool operator==(const MaterialSignature &) const = default;
};
inline MaterialSignature Signature(const ItemState &item) noexcept {
    return {item.definition, item.paint, item.seed, item.statTrak, item.wear, item.name};
}
using ModelPath = std::array<char, 260>;
enum class ModelObservation { None, Pending, Confirmed, Superseded };
// Issued resource requests are distinct from the last model confirmed resident.
// The same transition is used for application and restoration.
struct ModelRequest {
    ModelPath source{}, target{};
    bool active{};
    void Begin(const ModelPath &from, const ModelPath &to) noexcept {
        source = from;
        target = to;
        active = true;
    }
    ModelObservation Observe(const ModelPath &resident) noexcept {
        if (!active)
            return ModelObservation::None;
        if (resident == target) {
            active = false;
            return ModelObservation::Confirmed;
        }
        if (resident == source)
            return ModelObservation::Pending;
        active = false;
        return ModelObservation::Superseded;
    }
};
// Cosmetic identity is validated on the native game thread. A brief gap in the
// independent 2D snapshot must not restore and reapply every cosmetic material.
struct FrameFreshness {
    std::uint64_t lastFresh{};
    bool seen{};
    bool Allow(bool wanted, bool fresh, std::uint64_t now) noexcept {
        if (!wanted || (seen && now < lastFresh)) {
            seen = false;
            return false;
        }
        if (fresh) {
            lastFresh = now;
            seen = true;
        }
        return seen && now - lastFresh <= 2000;
    }
};
struct MaterialRequest {
    MaterialSignature signature;
    bool selected{}, pending{};
    unsigned attempts{};
    std::uint64_t retryAt{};
    bool Select(const ItemState &item) noexcept {
        const auto next = Signature(item);
        if (selected && signature == next)
            return false;
        signature = next;
        selected = pending = true;
        attempts = 0;
        retryAt = 0;
        return true;
    }
    bool Ready(std::uint64_t now) const noexcept { return pending && attempts < 3 && now >= retryAt; }
    void Attempt(std::uint64_t now) noexcept {
        ++attempts;
        retryAt = now + 250;
    }
    void Complete() noexcept {
        pending = false;
        retryAt = 0;
    }
};
struct FieldPatch {
    std::uintptr_t address{};
    std::size_t size{};
    std::array<unsigned char, 161> before{}, after{};
};
struct PatchSet {
    using CompareWrite = bool (*)(void *, std::uintptr_t, const void *, const void *, std::size_t) noexcept;
    std::array<FieldPatch, 24> fields{};
    std::size_t count{};
    template <typename T> void Add(std::uintptr_t address, const T &before, const T &after) noexcept {
        static_assert(sizeof(T) <= 161);
        if (std::memcmp(&before, &after, sizeof(T)) == 0 || count == fields.size())
            return;
        auto &p = fields[count++];
        p.address = address;
        p.size = sizeof(T);
        std::memcpy(p.before.data(), &before, sizeof(T));
        std::memcpy(p.after.data(), &after, sizeof(T));
    }
    bool Commit(void *context, CompareWrite write) const noexcept {
        std::size_t i{};
        for (; i < count; ++i) {
            const auto &p = fields[i];
            if (!write(context, p.address, p.before.data(), p.after.data(), p.size))
                break;
        }
        if (i == count)
            return true;
        // Roll back only our writes. A network update or another owner always wins.
        while (i) {
            const auto &p = fields[--i];
            write(context, p.address, p.after.data(), p.before.data(), p.size);
        }
        return false;
    }
    unsigned RestoreMatching(void *context, CompareWrite write) const noexcept {
        unsigned restored{};
        for (const auto &p : std::span(fields.data(), count))
            restored += write(context, p.address, p.after.data(), p.before.data(), p.size) ? 1 : 0;
        return restored;
    }
};
} // namespace awareness::cosmetics
