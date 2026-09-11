#pragma once
#include <algorithm>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace awareness::scan {
inline constexpr std::string_view EntityListSignature = "48 8B 0D ? ? ? ? 48 8D 94 24";
inline constexpr std::string_view ViewMatrixSignature = "48 8D ? ? ? ? ? 48 8B ? 48 8D ? ? ? ? ? E8";
using Pattern = std::vector<std::int16_t>; // -1 is one wildcard byte.

inline std::optional<Pattern> ParsePattern(std::string_view text) {
    Pattern result;
    bool hasLiteral = false;
    const auto hex = [](char c) noexcept -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    while (!text.empty()) {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
        if (text.empty()) break;
        std::size_t length = 0;
        while (length < text.size() && !std::isspace(static_cast<unsigned char>(text[length]))) ++length;
        const auto token = text.substr(0, length);
        text.remove_prefix(length);
        if (token == "?" || token == "??") result.push_back(-1);
        else {
            if (token.size() != 2 || hex(token[0]) < 0 || hex(token[1]) < 0) return std::nullopt;
            result.push_back(static_cast<std::int16_t>((hex(token[0]) << 4) | hex(token[1])));
            hasLiteral = true;
        }
        if (result.size() > 512) return std::nullopt;
    }
    if (result.empty() || !hasLiteral) return std::nullopt;
    return result;
}

template<class Visitor>
void ForEachMatch(std::span<const std::uint8_t> bytes, const Pattern& pattern, Visitor visitor) {
    if (pattern.empty() || bytes.size() < pattern.size()) return;
    const auto literal=std::find_if(pattern.begin(),pattern.end(),[](auto byte){return byte>=0;});
    const auto anchor=static_cast<std::size_t>(literal-pattern.begin());
    const auto last=bytes.size()-pattern.size();
    for (std::size_t offset = 0; offset <= bytes.size() - pattern.size(); ++offset) {
        if(literal!=pattern.end()) {
            const auto end=bytes.begin()+static_cast<std::ptrdiff_t>(last+anchor+1);
            const auto found=std::find(bytes.begin()+static_cast<std::ptrdiff_t>(offset+anchor),end,static_cast<std::uint8_t>(*literal));
            if(found==end) return;
            offset=static_cast<std::size_t>(found-bytes.begin())-anchor;
        }
        bool matches = true;
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            if (pattern[i] >= 0 && bytes[offset + i] != pattern[i]) { matches = false; break; }
        }
        if (matches) visitor(offset);
    }
}

inline std::optional<std::uintptr_t> ResolveRelative32(std::uintptr_t instructionAddress,
    std::span<const std::uint8_t> instruction, std::size_t displacementOffset,
    std::size_t instructionLength) noexcept {
    if (instructionLength > instruction.size() || displacementOffset > instructionLength ||
        instructionLength - displacementOffset < sizeof(std::int32_t)) return std::nullopt;
    const auto limit = (std::numeric_limits<std::uintptr_t>::max)();
    if (instructionAddress > limit - instructionLength) return std::nullopt;
    const auto* p = instruction.data() + displacementOffset;
    const std::uint32_t bits = static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
    const std::int64_t displacement = std::bit_cast<std::int32_t>(bits);
    const auto nextInstruction = instructionAddress + instructionLength;
    // Sign extension is essential: disp32 can point backward in the module.
    if (displacement < 0) {
        const auto magnitude = static_cast<std::uintptr_t>(-displacement);
        if (nextInstruction < magnitude) return std::nullopt;
        return nextInstruction - magnitude;
    }
    const auto magnitude = static_cast<std::uintptr_t>(displacement);
    if (nextInstruction > limit - magnitude) return std::nullopt;
    return nextInstruction + magnitude;
}

inline bool IsRipRelative7(std::span<const std::uint8_t> instruction, std::uint8_t opcode) noexcept {
    // REX.W + MOV/LEA + ModR/M(mod=00,r/m=101) + signed disp32.
    // In particular, the wildcarded LEA must not be a stack/base-register operand.
    return instruction.size() >= 7 && instruction[0] == 0x48 && instruction[1] == opcode &&
        (instruction[2] & 0xC7) == 0x05;
}
} // namespace awareness::scan
