#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

namespace awareness::cs2 {
// Read-only in-process copies. Region permissions are cached only for one sample.
// Validate the entire range first; SEH covers memory being released during the copy.
class LocalMemory {
    struct Region {
        std::uintptr_t first{}, last{};
    };
    std::array<Region, 128> regions_{};
    std::size_t count_{}, replace_{}, recent_{};
    bool Range(std::uintptr_t first, std::size_t length) noexcept {
        if (!first || !length || first > (std::numeric_limits<std::uintptr_t>::max)() - length)
            return false;
        const auto last = first + length;
        while (first < last) {
            std::uintptr_t end{};
            if (count_ && first >= regions_[recent_].first && first < regions_[recent_].last)
                end = regions_[recent_].last;
            for (std::size_t i = 0; !end && i < count_; ++i)
                if (first >= regions_[i].first && first < regions_[i].last) {
                    recent_ = i;
                    end = regions_[i].last;
                    break;
                }
            if (!end) {
                MEMORY_BASIC_INFORMATION info{};
                if (!VirtualQuery(reinterpret_cast<void *>(first), &info, sizeof(info)) || info.State != MEM_COMMIT ||
                    (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
                    !(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                      PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
                    return false;
                const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
                if (base > (std::numeric_limits<std::uintptr_t>::max)() - info.RegionSize)
                    return false;
                end = base + info.RegionSize;
                const auto index = count_ < regions_.size() ? count_++ : replace_++ % regions_.size();
                regions_[index] = {base, end};
                recent_ = index;
            }
            if (end <= first)
                return false;
            first = end;
        }
        return true;
    }
    static bool Copy(std::uintptr_t address, void *target, std::size_t length) noexcept {
#if defined(_MSC_VER)
        __try {
            std::memcpy(target, reinterpret_cast<const void *>(address), length);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
#else
        SIZE_T copied{};
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address), target, length,
                                 &copied) &&
               copied == length;
#endif
    }

  public:
    void Reset() noexcept { count_ = replace_ = recent_ = 0; }
    static bool Read(void *context, std::uintptr_t address, void *target, std::size_t length) noexcept {
        if (!context || !target)
            return false;
        auto &memory = *static_cast<LocalMemory *>(context);
        if (!memory.Range(address, length))
            return false;
        if (Copy(address, target, length))
            return true;
        // A mapping changed after validation. Do not repeatedly fault against the
        // old cached permissions for the remainder of this sample.
        memory.Reset();
        return false;
    }
};
} // namespace awareness::cs2
