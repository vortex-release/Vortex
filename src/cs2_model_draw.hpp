#pragma once
#include "cs2_glow.hpp"
#include <span>
#include <string_view>
#include <vector>

namespace awareness::cs2::model {
// Source 2's case-insensitive CUtlStringToken. Keep named passes here: using
// generic Forward alone silently excludes CS2's actual character draws.
constexpr std::uint32_t PassToken(std::string_view s) noexcept {
    constexpr std::uint32_t m = 0x5bd1e995;
    auto h = 0x31415926u ^ static_cast<std::uint32_t>(s.size());
    const auto byte = [&](std::size_t i) {
        auto c = static_cast<unsigned char>(s[i]);
        return std::uint32_t(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    };
    std::size_t i{};
    for (; i + 4 <= s.size(); i += 4) {
        auto k = byte(i) | (byte(i + 1) << 8) | (byte(i + 2) << 16) | (byte(i + 3) << 24);
        k *= m;
        k ^= k >> 24;
        k *= m;
        h = (h * m) ^ k;
    }
    if (i < s.size()) {
        std::uint32_t tail{};
        for (unsigned shift = 0; i < s.size(); ++i, shift += 8)
            tail |= byte(i) << shift;
        h = (h ^ tail) * m;
    }
    h ^= h >> 13;
    h *= m;
    return h ^ (h >> 15);
}
constexpr bool ColorPass(std::uint32_t pass) noexcept {
    return pass == PassToken("CsgoForward");
}
static_assert(PassToken("CsgoForward") == 0xbd79f698);
static_assert(PassToken("Depth") == 0x37233ba9);

struct alignas(8) Packet {
    std::array<std::byte, offsets::PacketStride> bytes{};
    template <class T> T Get(std::size_t offset) const noexcept {
        T value{};
        if (offset <= bytes.size() && sizeof(value) <= bytes.size() - offset)
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    }
    template <class T> void Set(std::size_t offset, T value) noexcept {
        if (offset <= bytes.size() && sizeof(value) <= bytes.size() - offset)
            std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }
};
static_assert(sizeof(Packet) == 0x70 && offsets::PacketColor == 0x50 && offsets::PacketMaterial == 0x20);

struct Target {
    std::uintptr_t scene{}, component{};
    GlowIdentity owner{};
    float opacity{};
};
inline constexpr std::size_t MaxModels = MaxEntities * 8;
struct Targets {
    std::array<Target, MaxModels> entries{};
    std::size_t count{};
};
// One immutable selection is shared by a complete engine draw callback.
struct Selection {
    std::vector<Target> entries;
    EffectsConfiguration config;
    bool shaded{};
    Selection(const Targets &targets, const EffectsConfiguration &effects, bool detail = false)
        : entries(targets.entries.begin(), targets.entries.begin() + targets.count), config(effects), shaded(detail) {
        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.scene < b.scene; });
    }
    const Target *Find(std::uintptr_t scene) const noexcept {
        const auto it = std::lower_bound(entries.begin(), entries.end(), scene,
                                         [](const Target &target, auto key) { return target.scene < key; });
        return it != entries.end() && it->scene == scene ? &*it : nullptr;
    }
};
inline bool HasScene(const Memory &memory, std::uintptr_t component, std::uintptr_t scene) noexcept {
    std::int32_t count{};
    std::uintptr_t array{};
    if (!component || !scene || !memory.Field(component, offsets::SceneUpdaterCount, count) || count < 1 ||
        count > 32 || !memory.Field(component, offsets::SceneUpdaterArray, array) || !array)
        return false;
    for (int i = 0; i < count; ++i) {
        std::uintptr_t updater{}, current{};
        if (memory.Field(array, sizeof(std::uintptr_t) * i, updater) &&
            memory.Field(updater, offsets::UpdaterSceneObject, current) && current == scene)
            return true;
    }
    return false;
}
inline Targets CollectTargets(const Memory &memory, std::uintptr_t list, const FrameSnapshot &frame,
                              const Configuration &config) noexcept {
    Targets result;
    for (std::uint32_t i = 0; i < (std::min)(frame.entityCount, MaxEntities); ++i) {
        const auto &entity = frame.entities[i];
        const float opacity = EntityOpacity(frame, entity, config);
        Target target;
        target.opacity = opacity;
        if (opacity <= 0 || !ResolveGlowEntity(memory, list, entity, target.owner) ||
            !memory.Field(target.owner.pawn, offsets::RenderComponent, target.component))
            continue;
        std::int32_t count{};
        std::uintptr_t array{};
        if (!memory.Field(target.component, offsets::SceneUpdaterCount, count) || count < 1 || count > 32 ||
            !memory.Field(target.component, offsets::SceneUpdaterArray, array) || !array)
            continue;
        for (int j = 0; j < count && result.count < result.entries.size(); ++j) {
            std::uintptr_t updater{};
            if (!memory.Field(array, sizeof(std::uintptr_t) * j, updater) ||
                !memory.Field(updater, offsets::UpdaterSceneObject, target.scene) || !target.scene)
                continue;
            bool duplicate{};
            for (std::size_t k = 0; k < result.count; ++k)
                duplicate |= result.entries[k].scene == target.scene;
            if (!duplicate)
                result.entries[result.count++] = target;
        }
    }
    return result;
}
inline bool StillOwned(const Memory &memory, const Target &target) noexcept {
    std::uintptr_t component{};
    std::int32_t health{};
    std::uint8_t life{};
    return SameGlowEntity(memory, target.owner) &&
           memory.Field(target.owner.pawn, offsets::RenderComponent, component) && component == target.component &&
           memory.Field(target.owner.pawn, offsets::Health, health) && health > 0 &&
           memory.Field(target.owner.pawn, offsets::LifeState, life) && life == 0 &&
           HasScene(memory, component, target.scene);
}
inline bool PlayerModel(const Memory &memory, const Packet &packet) noexcept {
    const auto mesh = packet.Get<std::uintptr_t>(0);
    std::uintptr_t handle{}, model{}, name{};
    std::array<char, 14> prefix{};
    return memory.Field(mesh, 8, handle) && memory.Read(handle, model) && memory.Field(model, 8, name) &&
           memory.Read(name, prefix) && std::memcmp(prefix.data(), "agents/models/", prefix.size()) == 0;
}
inline std::uint32_t PackColor(Color color, float opacity) noexcept {
    const auto byte = [](float x) { return static_cast<std::uint32_t>(std::lround(std::clamp(x, 0.f, 1.f) * 255.f)); };
    return byte(color.r) | byte(color.g) << 8 | byte(color.b) << 16 | byte(color.a * opacity) << 24;
}
// Original geometry is drawn between the hidden and visible layers so visible
// alpha blends over the original material, rather than over the hidden color.
// The packet belongs to the engine: only copies are ever modified.
template <class Draw>
void DrawLayers(const Packet &packet, const Target &target, const EffectsConfiguration &config,
                std::uintptr_t visibleMaterial, std::uintptr_t hiddenMaterial, Draw &&draw) {
    auto copy = packet;
    const auto hidden = config.visibility == EffectVisibility::TwoColor ? config.glowColor : config.materialColor;
    if (hidden.a * target.opacity > 0) {
        copy.Set(offsets::PacketMaterial, hiddenMaterial);
        copy.Set(offsets::PacketColor, PackColor(hidden, target.opacity));
        draw(copy);
    }
    draw(packet);
    if (config.visibility == EffectVisibility::TwoColor && config.materialColor.a * target.opacity > 0) {
        copy.Set(offsets::PacketMaterial, visibleMaterial);
        copy.Set(offsets::PacketColor, PackColor(config.materialColor, target.opacity));
        draw(copy);
    }
}
struct DrawItem {
    Packet packet;
    float opacity{};
    std::uint32_t sourceIndex{};
};
class DrawBuffer {
    std::array<DrawItem, 128> local_;
    std::vector<DrawItem> overflow_;
    std::size_t count_{};

  public:
    void Push(const Packet &packet, float opacity, std::uint32_t sourceIndex = 0) {
        if (count_ < local_.size())
            local_[count_] = {packet, opacity, sourceIndex};
        else {
            if (overflow_.empty()) {
                overflow_.reserve(local_.size() * 2);
                overflow_.insert(overflow_.end(), local_.begin(), local_.end());
            }
            overflow_.push_back({packet, opacity, sourceIndex});
        }
        ++count_;
    }
    std::span<const DrawItem> Items() const noexcept {
        return {overflow_.empty() ? local_.data() : overflow_.data(), count_};
    }
};
// Submit whole color layers, not hidden/original/visible for each mesh piece.
// The original batch is submitted exactly once, in its original order.
template <class Draw, class Original>
void DrawBatch(std::span<const DrawItem> items, const EffectsConfiguration &config, std::uintptr_t visibleMaterial,
               std::uintptr_t hiddenMaterial, Draw &&draw, Original &&original) {
    std::array<Packet, 128> packets;
    const auto layer = [&](Color color, std::uintptr_t material) {
        if (color.a <= 0)
            return;
        std::size_t count{};
        for (const auto &item : items) {
            if (item.opacity <= 0)
                continue;
            auto &packet = packets[count++];
            packet = item.packet;
            packet.Set(offsets::PacketMaterial, material);
            packet.Set(offsets::PacketColor, PackColor(color, item.opacity));
            if (count == packets.size()) {
                draw(std::span<const Packet>{packets.data(), count});
                count = 0;
            }
        }
        if (count)
            draw(std::span<const Packet>{packets.data(), count});
    };
    layer(config.visibility == EffectVisibility::TwoColor ? config.glowColor : config.materialColor, hiddenMaterial);
    original();
    if (config.visibility == EffectVisibility::TwoColor)
        layer(config.materialColor, visibleMaterial);
}
inline bool CanCompose(std::span<const DrawItem> items, const EffectsConfiguration &config, bool shaded) noexcept {
    if (config.visibility == EffectVisibility::OccludedOnly || shaded)
        return true;
    return std::all_of(items.begin(), items.end(),
                       [&](auto &item) { return config.materialColor.a * item.opacity >= .999f; });
}
inline Packet VisiblePacket(const Packet &original, float opacity, const EffectsConfiguration &config,
                            std::uintptr_t material, bool shaded) noexcept {
    auto copy = original;
    if (config.visibility == EffectVisibility::OccludedOnly)
        return copy;
    if (shaded) {
        auto rgba = copy.Get<std::array<std::uint8_t, 4>>(offsets::PacketColor);
        const auto c = config.materialColor;
        const float mix = std::clamp(c.a * opacity, 0.f, 1.f);
        const float rgb[]{c.r, c.g, c.b};
        for (int i = 0; i < 3; ++i)
            rgba[i] = static_cast<std::uint8_t>(std::lround(rgba[i] * (1 - mix) + 255 * rgb[i] * mix));
        copy.Set(offsets::PacketColor, rgba);
    } else {
        copy.Set(offsets::PacketMaterial, material);
        copy.Set(offsets::PacketColor, PackColor(config.materialColor, opacity));
    }
    return copy;
}
} // namespace awareness::cs2::model
