#pragma once
#include <Windows.h>
#include <cstdint>
struct FixtureAddresses {
    std::uintptr_t entityInstruction{}, entitySlot{}, entityList{}, viewInstruction{}, matrix{}, protectionPage{};
};
enum FixtureMode : unsigned { Normal, NoMatches, DuplicateEntity, DuplicateView, BadLea, NullList };
#ifdef SCAN_FIXTURE_EXPORTS
#define FIXTURE_API extern "C" __declspec(dllexport)
#else
#define FIXTURE_API extern "C" __declspec(dllimport)
#endif
FIXTURE_API void __cdecl ConfigureScanFixture(FixtureMode, FixtureAddresses*) noexcept;
