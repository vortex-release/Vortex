#pragma once
#include "cs2_glow.hpp"
#include <atomic>
#include <memory>
#include <span>
#include <utility>
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
struct DrawItem {
    Packet packet;
    float opacity{};
    std::uint32_t sourceIndex{};
};
inline constexpr std::size_t MaxDrawPackets = 8192;
inline constexpr std::size_t SmallDrawBatch = 128;
// Validated against build 14181 scenesystem+0x70520 and +0x363B0.
// Counts are independent: overflowing does not replace or extend fixed storage.
inline constexpr std::size_t MaxGeneratedPrimitives = 1u << 20;
struct PrimitiveBuffer {
    std::uintptr_t fixed{};
    std::int32_t capacity{}, count{}, overflowCount{}, reserved{};
    std::uintptr_t overflow{};
    std::int32_t overflowCapacity{}, allocationFlags{};
};
static_assert(sizeof(PrimitiveBuffer) == 0x28 && offsetof(PrimitiveBuffer, count) == 0xc &&
              offsetof(PrimitiveBuffer, overflow) == 0x18);
inline bool ValidPrimitiveBuffer(const PrimitiveBuffer &b) noexcept {
    return b.count >= 0 && b.overflowCount >= 0 && b.capacity >= b.count && b.overflowCapacity >= b.overflowCount &&
           std::uint64_t(b.count) + b.overflowCount <= MaxGeneratedPrimitives && (!b.count || b.fixed) &&
           (!b.overflowCount || b.overflow);
}
inline bool ReadPrimitiveBuffer(const Memory &memory, std::uintptr_t address, PrimitiveBuffer &out) noexcept {
    return memory.Read(address, out) && ValidPrimitiveBuffer(out);
}
// Read the current buffer after the original generator: its overflow allocation
// may have moved. Never reuse an old pointer, visit old packets, or write counts.
template <class Visit>
bool ForEachAppended(const PrimitiveBuffer &before, const PrimitiveBuffer &after, Visit &&visit) {
    if (!ValidPrimitiveBuffer(before) || !ValidPrimitiveBuffer(after) || before.count > after.count ||
        before.overflowCount > after.overflowCount || (before.count && before.fixed != after.fixed))
        return false;
    const auto added = after.count - before.count + after.overflowCount - before.overflowCount;
    const auto addressFits = [](std::uintptr_t base, std::int32_t count) {
        return base <= (std::numeric_limits<std::uintptr_t>::max)() - sizeof(Packet) * static_cast<std::size_t>(count);
    };
    if (added > 512 || !addressFits(after.fixed, after.count) || !addressFits(after.overflow, after.overflowCount))
        return false;
    for (auto i = before.count; i < after.count; ++i)
        visit(after.fixed + sizeof(Packet) * static_cast<std::size_t>(i));
    for (auto i = before.overflowCount; i < after.overflowCount; ++i)
        visit(after.overflow + sizeof(Packet) * static_cast<std::size_t>(i));
    return true;
}
// Only engine-owned primitive memory from the active callback is passed here.
// SEH contains concurrent teardown without propagating into an engine worker.
inline bool WritePrimitive(std::uintptr_t address, const Packet &packet) noexcept {
    __try {
        std::memcpy(reinterpret_cast<void *>(address), &packet, sizeof(packet));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
class DrawBuffer {
    std::array<DrawItem, SmallDrawBatch> local_;
    std::vector<DrawItem> overflow_;
    std::size_t count_{};

  public:
    void Clear() noexcept {
        count_ = 0;
        overflow_.clear();
    }
    bool Push(const Packet &packet, float opacity, std::uint32_t sourceIndex = 0) {
        if (count_ >= MaxDrawPackets)
            return false;
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
        return true;
    }
    std::span<const DrawItem> Items() const noexcept {
        return {overflow_.empty() ? local_.data() : overflow_.data(), count_};
    }
};
// Scratch belongs to the hook owner, not thread-local storage: cached allocations
// are released only after callbacks have drained, before the DLL is unloaded.
class DrawScratch {
    std::array<Packet, SmallDrawBatch> local_;
    std::vector<Packet> large_;

  public:
    DrawBuffer selected;
    std::span<Packet> Batch(std::size_t count) {
        if (!count || count > MaxDrawPackets)
            return {};
        if (count <= local_.size())
            return {local_.data(), count};
        if (count > large_.capacity()) {
            std::size_t capacity = SmallDrawBatch * 2;
            while (capacity < count)
                capacity *= 2;
            large_.reserve(capacity);
        }
        large_.resize(count);
        return large_;
    }
};
template <std::size_t Slots = 8> class ScratchPool {
    struct Slot {
        std::atomic_bool busy{};
        std::unique_ptr<DrawScratch> data;
    };
    std::array<Slot, Slots> slots_;

  public:
    class Lease {
        Slot *slot_{};
        friend class ScratchPool;
        explicit Lease(Slot *slot) noexcept : slot_(slot) {}

      public:
        Lease() = default;
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        Lease(Lease &&other) noexcept : slot_(std::exchange(other.slot_, nullptr)) {}
        ~Lease() {
            if (slot_)
                slot_->busy.store(false, std::memory_order_release);
        }
        explicit operator bool() const noexcept { return slot_ != nullptr; }
        DrawScratch *operator->() noexcept { return slot_->data.get(); }
    };
    Lease Acquire() noexcept {
        for (auto &slot : slots_) {
            bool expected{};
            if (!slot.busy.compare_exchange_strong(expected, true, std::memory_order_acquire))
                continue;
            try {
                if (!slot.data)
                    slot.data = std::make_unique<DrawScratch>();
                slot.data->selected.Clear();
                return Lease(&slot);
            } catch (...) {
                slot.busy.store(false, std::memory_order_release);
                return {};
            }
        }
        // Busy render workers must never wait for an overlay buffer.
        return {};
    }
    // Call only after the hook has been disabled and all callbacks have drained.
    void Clear() noexcept {
        for (auto &slot : slots_)
            slot.data.reset();
    }
};
inline bool HasVisibleTint(const EffectsConfiguration &config) noexcept {
    return config.materialEnabled && config.visibility != EffectVisibility::OccludedOnly && config.materialColor.a > 0;
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
    if (!HasVisibleTint(config) || !std::isfinite(opacity) || opacity <= 0 || (!shaded && !material))
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
// The input is an owned copy of the complete engine batch. Validate all indices
// before changing any packet, then retain its exact count, order and opaque data.
inline bool ComposeVisible(std::span<Packet> batch, std::span<const DrawItem> selected,
                           const EffectsConfiguration &config, std::uintptr_t material, bool shaded) noexcept {
    if (batch.empty() || batch.size() > MaxDrawPackets || !HasVisibleTint(config) || (!shaded && !material))
        return false;
    for (const auto &item : selected)
        if (item.sourceIndex >= batch.size() || !std::isfinite(item.opacity))
            return false;
    for (const auto &item : selected)
        batch[item.sourceIndex] = VisiblePacket(item.packet, item.opacity, config, material, shaded);
    return true;
}
// Only a depth-tested visible layer is submitted. No unverified Z-disabled
// material or primitive-buffer header is changed to approximate hidden faces.
template <class Draw>
unsigned DrawVisible(std::span<const DrawItem> selected, const EffectsConfiguration &config, std::uintptr_t material,
                     std::span<Packet> scratch, Draw &&draw) {
    if (!HasVisibleTint(config) || !material || scratch.empty())
        return 0;
    scratch = scratch.first(std::min(scratch.size(), SmallDrawBatch));
    std::size_t used{};
    unsigned submitted{};
    for (const auto &item : selected) {
        if (!std::isfinite(item.opacity) || item.opacity <= 0 || !(PackColor(config.materialColor, item.opacity) >> 24))
            continue;
        scratch[used++] = VisiblePacket(item.packet, item.opacity, config, material, false);
        ++submitted;
        if (used == scratch.size()) {
            draw(std::span<const Packet>{scratch.data(), used});
            used = 0;
        }
    }
    if (used)
        draw(std::span<const Packet>{scratch.data(), used});
    return submitted;
}
} // namespace awareness::cs2::model
