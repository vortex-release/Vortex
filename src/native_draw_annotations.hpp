#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace awareness::native_mask {
// Non-owning frame-local command tags. No command/entity/GPU object is retained.
template <std::size_t Size = 16384> class Annotations {
    static_assert(Size && !(Size & (Size - 1)));
    struct Entry {
        std::atomic_flag lock{};
        std::atomic<std::uintptr_t> key{};
        std::uint64_t generation{};
        std::uint32_t color{};
        std::array<unsigned, 5> args{};
    };
    std::array<Entry, Size> entries_;
    std::atomic<std::uint64_t> generation_{1};
    static std::size_t Bucket(std::uintptr_t p) noexcept { return ((p >> 3) ^ (p >> 17)) & (Size - 1); }

  public:
    std::uint64_t Generation() const noexcept { return generation_.load(std::memory_order_acquire); }
    void Advance() noexcept { generation_.fetch_add(1, std::memory_order_acq_rel); }
    bool Put(std::uintptr_t command, const std::array<unsigned, 5> &args, std::uint32_t color) noexcept {
        if (!command || !(color >> 24))
            return false;
        const auto generation = generation_.load(std::memory_order_acquire);
        for (std::size_t probe = 0; probe < 8; ++probe) {
            auto &entry = entries_[(Bucket(command) + probe) & (Size - 1)];
            if (entry.lock.test_and_set(std::memory_order_acquire))
                continue;
            const bool available = !entry.color || entry.generation != generation || entry.key == command;
            const bool inserted = available && generation == generation_.load(std::memory_order_acquire);
            if (inserted) {
                entry.key.store(command, std::memory_order_relaxed);
                entry.args = args;
                entry.color = color;
                entry.generation = generation;
            }
            entry.lock.clear(std::memory_order_release);
            if (inserted)
                return true;
        }
        return false;
    }
    std::uint32_t Take(std::uintptr_t command, const std::array<unsigned, 5> &args) noexcept {
        if (!command)
            return 0;
        const auto generation = generation_.load(std::memory_order_acquire);
        for (std::size_t probe = 0; probe < 8; ++probe) {
            auto &entry = entries_[(Bucket(command) + probe) & (Size - 1)];
            if (entry.key.load(std::memory_order_relaxed) != command ||
                entry.lock.test_and_set(std::memory_order_acquire))
                continue;
            std::uint32_t result{};
            if (entry.key == command && entry.generation == generation &&
                generation == generation_.load(std::memory_order_acquire)) {
                if (entry.args == args)
                    result = entry.color;
                entry.color = 0; // A reused command with different arguments invalidates the old tag.
            }
            entry.lock.clear(std::memory_order_release);
            return result;
        }
        return 0;
    }
};
} // namespace awareness::native_mask
