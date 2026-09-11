#include "local_memory.hpp"
#include "cs2_reader.hpp"
#include <chrono>
#include <cstdio>
int main() {
    using namespace awareness::cs2;
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto page = system.dwPageSize;
    auto *bytes =
        static_cast<unsigned char *>(VirtualAlloc(nullptr, 3ull * page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!bytes)
        return 1;
    DWORD previous{};
    VirtualProtect(bytes + page, page, PAGE_READWRITE | PAGE_GUARD, &previous);
    VirtualProtect(bytes + 2 * page, page, PAGE_NOACCESS, &previous);
    LocalMemory local;
    Memory memory{&local, LocalMemory::Read};
    int checks{}, failures{};
    const auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", name);
        }
    };
    std::uint32_t expected = 42, value{};
    std::memcpy(bytes, &expected, sizeof(expected));
    check(memory.Read(reinterpret_cast<std::uintptr_t>(bytes), value) && value == 42, "read committed local memory");
    expected = 99;
    std::memcpy(bytes, &expected, sizeof(expected));
    check(memory.Read(reinterpret_cast<std::uintptr_t>(bytes), value) && value == 99,
          "next sample sees changed value without a timer");
    value = 42;
    check(!memory.Read(reinterpret_cast<std::uintptr_t>(bytes + page), value) && !value, "guarded memory rejected");
    MEMORY_BASIC_INFORMATION region{};
    VirtualQuery(bytes + page, &region, sizeof(region));
    check((region.Protect & PAGE_GUARD) != 0, "guard bit is preserved");
    value = 42;
    check(!memory.Read(reinterpret_cast<std::uintptr_t>(bytes + 2 * page), value) && !value,
          "inaccessible memory rejected");
    std::array<unsigned char, 8> crossing{};
    check(!memory.Read(reinterpret_cast<std::uintptr_t>(bytes + page - 4), crossing),
          "whole range checked across a protected boundary");
    check(!memory.Read((std::numeric_limits<std::uintptr_t>::max)() - 1, value),
          "overflow rejected before touching memory");
    constexpr int iterations = 50000;
    const auto measure = [&](auto read) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i)
            if (!read())
                ++failures;
        return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / iterations;
    };
    const auto localUs = measure([&] { return memory.Read(reinterpret_cast<std::uintptr_t>(bytes), value); });
    const auto processUs = measure([&] {
        SIZE_T count{};
        return ReadProcessMemory(GetCurrentProcess(), bytes, &value, sizeof(value), &count) && count == sizeof(value);
    });
    VirtualProtect(bytes, page, PAGE_NOACCESS, &previous);
    check(!memory.Read(reinterpret_cast<std::uintptr_t>(bytes), value),
          "SEH handles a page becoming inaccessible after validation");
    VirtualProtect(bytes, page, PAGE_READWRITE | PAGE_GUARD, &previous);
    check(!memory.Read(reinterpret_cast<std::uintptr_t>(bytes), value),
          "failed cached copy invalidates permissions before the next access");
    VirtualQuery(bytes, &region, sizeof(region));
    check((region.Protect & PAGE_GUARD) != 0, "a guard installed after a failed cached read remains untouched");
    VirtualProtect(bytes, page, PAGE_READWRITE, &previous);
    local.Reset();
    check(memory.Read(reinterpret_cast<std::uintptr_t>(bytes), value) && value == 99,
          "new sample resets permissions while retaining current memory values");
    VirtualFree(bytes, 0, MEM_RELEASE);
    std::printf("%d local-memory checks; %d failures. Cached local read %.3f us; ReadProcessMemory %.3f us (same "
                "address, %d copies).\n",
                checks, failures, localUs, processUs, iterations);
    return failures ? 1 : 0;
}
