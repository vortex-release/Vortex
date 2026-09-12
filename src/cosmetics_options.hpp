#pragma once
#include "weapon_catalog.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace awareness::cosmetics {
inline constexpr std::size_t WeaponCount = std::size(WeaponIcons);
struct Finish {
    std::uint32_t enabled{};
    std::uint32_t paintKit{}, seed{};
    float wear{0.01f};
    std::uint32_t statTrak{}, kills{};
    std::array<char, 64> name{};
    bool operator==(const Finish &) const = default;
};
struct Appearance {
    std::uint32_t enabled{}, definition{};
    Finish finish;
    bool operator==(const Appearance &) const = default;
};
struct Options {
    std::uint32_t enabled{};
    std::array<Finish, WeaponCount> weapons{};
    Appearance knife, glove;
    // Index 0 = Terrorist, index 1 = Counter-Terrorist; 0 keeps the original model.
    std::array<std::uint32_t, 2> agents{};
    bool operator==(const Options &) const = default;
};
inline bool ValidName(const std::array<char, 64> &name) noexcept {
    const auto end = static_cast<const char *>(std::memchr(name.data(), 0, name.size()));
    if (!end)
        return false;
    for (auto p = name.data(); p != end;) {
        const auto c = static_cast<unsigned char>(*p++);
        if (c < 0x20 || c == 0x7f)
            return false;
        if (c < 0x80)
            continue;
        unsigned count{}, cp{}, min{};
        if ((c & 0xe0) == 0xc0) {
            count = 1;
            cp = c & 31;
            min = 0x80;
        } else if ((c & 0xf0) == 0xe0) {
            count = 2;
            cp = c & 15;
            min = 0x800;
        } else if ((c & 0xf8) == 0xf0) {
            count = 3;
            cp = c & 7;
            min = 0x10000;
        } else
            return false;
        while (count--) {
            if (p == end || (static_cast<unsigned char>(*p) & 0xc0) != 0x80)
                return false;
            cp = (cp << 6) | (static_cast<unsigned char>(*p++) & 63);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            return false;
    }
    return true;
}
inline bool Valid(const Finish &f) noexcept {
    return f.enabled <= 1 && f.paintKit <= 100000 && f.seed <= 1000 && std::isfinite(f.wear) && f.wear >= 0 &&
           f.wear <= 1 && f.statTrak <= 1 && f.kills <= 999999 && ValidName(f.name);
}
inline bool Valid(const Appearance &a) noexcept {
    return a.enabled <= 1 && a.definition <= 65535 && Valid(a.finish);
}
inline bool Valid(const Options &o) noexcept {
    if (o.enabled > 1 || !Valid(o.knife) || !Valid(o.glove) || o.agents[0] > 65535 || o.agents[1] > 65535)
        return false;
    for (const auto &w : o.weapons)
        if (!Valid(w))
            return false;
    return true;
}
inline const Finish *ForWeapon(const Options &o, std::uint16_t definition) noexcept {
    if (!o.enabled)
        return nullptr;
    for (std::size_t i = 0; i < WeaponCount; ++i)
        if (WeaponIcons[i].id == definition)
            return o.weapons[i].enabled ? &o.weapons[i] : nullptr;
    return nullptr;
}
inline std::uint32_t SubclassToken(std::uint16_t definition) noexcept {
    // Source 2 hashes the decimal definition, not a weapon class name.
    char text[8]{};
    unsigned n{}, value = definition;
    do {
        text[n++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value);
    for (unsigned i = 0; i < n / 2; ++i) {
        const auto c = text[i];
        text[i] = text[n - i - 1];
        text[n - i - 1] = c;
    }
    constexpr std::uint32_t m = 0x5bd1e995;
    auto hash = 0x31415926u ^ n;
    unsigned at{};
    while (n - at >= 4) {
        auto k = static_cast<std::uint32_t>(static_cast<unsigned char>(text[at])) |
                 (static_cast<std::uint32_t>(static_cast<unsigned char>(text[at + 1])) << 8) |
                 (static_cast<std::uint32_t>(static_cast<unsigned char>(text[at + 2])) << 16) |
                 (static_cast<std::uint32_t>(static_cast<unsigned char>(text[at + 3])) << 24);
        k *= m;
        k ^= k >> 24;
        k *= m;
        hash *= m;
        hash ^= k;
        at += 4;
    }
    switch (n - at) {
    case 3:
        hash ^= static_cast<unsigned char>(text[at + 2]) << 16;
        [[fallthrough]];
    case 2:
        hash ^= static_cast<unsigned char>(text[at + 1]) << 8;
        [[fallthrough]];
    case 1:
        hash ^= static_cast<unsigned char>(text[at]);
        hash *= m;
    }
    hash ^= hash >> 13;
    hash *= m;
    hash ^= hash >> 15;
    return hash;
}
} // namespace awareness::cosmetics
