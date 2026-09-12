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
