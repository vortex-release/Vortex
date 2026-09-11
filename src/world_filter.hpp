#pragma once
#include "cs2_model_draw.hpp"
namespace awareness::cs2 {
inline bool Text(const Memory &m, std::uintptr_t at, char (&out)[260]) noexcept {
    // Read once when the complete resource name is accessible. Retain the
    // bounded byte path for names ending at a memory-page boundary.
    std::array<char, 260> text{};
    const bool block = m.Read(at, text);
    for (unsigned i = 0; i < 260; ++i) {
        char c{};
        if (block)
            c = text[i];
        else if (!m.Read(at + i, c))
            return false;
        if (!c) {
            out[i] = 0;
            return i > 10;
        }
        if (c < 32 || c > 126)
            return false;
        out[i] = c == '\\' ? '/' : c >= 'A' && c <= 'Z' ? c + 32 : c;
    }
    return false;
}
inline int ParticleType(std::string_view name) noexcept {
    if (!name.starts_with("particles/") || name.find(".vpcf") == std::string_view::npos)
        return 0;
    if (name.find("molotov") != name.npos || name.find("incendiary") != name.npos ||
        name.find("particles/inferno_fx/") == 0)
        return 1;
    if (name.find("smokegrenade") != name.npos || name.find("smoke_grenade") != name.npos)
        return 2;
    if (name.find("hegrenade") != name.npos || name.find("grenade_explosion") != name.npos)
        return 3;
    return 0;
}

inline bool ParticleResourceName(const Memory &m, std::uintptr_t collection, char (&path)[260]) noexcept {
    std::uintptr_t binding{}, descriptor{}, name{};
    // Collection initialization's error path reads binding->descriptor->path (3EC0A..3EC25).
    return m.Field(collection, trajectory_offsets::CollectionResource, binding) &&
           m.Field(binding, trajectory_offsets::ResourceDescriptor, descriptor) && m.Read(descriptor, name) &&
           Text(m, name, path);
}
inline void TintWorldPacket(model::Packet &packet, float darkness) noexcept {
    auto color = packet.Get<std::array<std::uint8_t, 4>>(offsets::PacketColor);
    const float gain = 1 - std::clamp(darkness, 0.f, .85f);
    for (int i = 0; i < 3; ++i)
        color[i] = static_cast<std::uint8_t>(color[i] * gain);
    packet.Set(offsets::PacketColor, color);
}
} // namespace awareness::cs2
