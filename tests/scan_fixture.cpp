#include "scan_fixture.hpp"
#include <awareness/Math.hpp>
#include <cstring>

// Synthetic bytes in a PE executable section. They are scanned but NEVER executed.
// The large section lets signatures cross both 64-KiB chunk and page boundaries.
#pragma section(".awtest", read, write)
__declspec(allocate(".awtest")) __declspec(align(4096)) unsigned char fixtureCode[196608]{};

namespace {
void Relative(std::size_t instruction, std::uintptr_t destination) noexcept {
    const auto source = reinterpret_cast<std::uintptr_t>(fixtureCode + instruction);
    const auto displacement = static_cast<std::int32_t>(static_cast<std::int64_t>(destination) -
        static_cast<std::int64_t>(source + 7));
    std::memcpy(fixtureCode + instruction + 3, &displacement, sizeof(displacement));
}
}
void __cdecl ConfigureScanFixture(FixtureMode mode, FixtureAddresses* expected) noexcept {
    std::memset(fixtureCode, 0, sizeof(fixtureCode));
    *expected = {};
    expected->protectionPage = reinterpret_cast<std::uintptr_t>(fixtureCode + 4096);
    if (mode == NoMatches) return;
    constexpr std::size_t entityOffset = 65536-5, viewOffset = 131072-8;
    expected->entityInstruction = reinterpret_cast<std::uintptr_t>(fixtureCode + entityOffset);
    expected->entitySlot = reinterpret_cast<std::uintptr_t>(fixtureCode + 150000);
    expected->entityList = reinterpret_cast<std::uintptr_t>(fixtureCode + 152000);
    expected->viewInstruction = reinterpret_cast<std::uintptr_t>(fixtureCode + viewOffset);
    expected->matrix = reinterpret_cast<std::uintptr_t>(fixtureCode + 512);
    const unsigned char entity[]{0x48,0x8B,0x0D,0,0,0,0,0x48,0x8D,0x94,0x24};
    const unsigned char view[]{0x48,0x8D,0x0D,0,0,0,0,0x48,0x8B,0xC8,0x48,0x8D,0x15,0,0,0,0,0xE8};
    std::memcpy(fixtureCode + entityOffset, entity, sizeof(entity));
    std::memcpy(fixtureCode + viewOffset, view, sizeof(view));
    Relative(entityOffset, expected->entitySlot); // Positive disp32.
    Relative(viewOffset, expected->matrix);     // Negative disp32.
    std::memcpy(reinterpret_cast<void*>(expected->entitySlot), &expected->entityList, sizeof(expected->entityList));
    const auto matrix = awareness::Matrix4x4::Identity();
    std::memcpy(reinterpret_cast<void*>(expected->matrix), &matrix, sizeof(matrix));
    if (mode == DuplicateEntity) {
        std::memcpy(fixtureCode + 20000, entity, sizeof(entity));
        Relative(20000, expected->entitySlot);
    }
    if (mode == DuplicateView) {
        std::memcpy(fixtureCode + 30000, view, sizeof(view));
        Relative(30000, expected->matrix);
    }
    if (mode == BadLea) fixtureCode[viewOffset+2] = 0x45;
    if (mode == NullList) std::memset(reinterpret_cast<void*>(expected->entitySlot), 0, sizeof(std::uintptr_t));
}
