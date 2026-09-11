#include "kill_events.hpp"
#include "local_memory.hpp"
#include <cstdio>
int main() {
    using namespace awareness::cs2;
    unsigned failures{}, checks{};
    const auto check = [&](bool ok, const char *label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("FAIL: %s\n", label);
        }
    };
    KillEvents events;
    KillSample sample{100, 200, 300, 0x18001, 6};
    check(events.Update(sample, true, true) == 0, "initial score establishes baseline");
    check(events.Update(sample, true, true) == 0, "unchanged score stays quiet");
    ++sample.count;
    check(events.Update(sample, true, true) == 1, "one local kill produces one event");
    sample.count += 2;
    check(events.Update(sample, true, true) == 2, "multiple kills between frames are retained");
    sample.count = 0;
    check(events.Update(sample, true, true) == 0, "round reset stays quiet");
    sample.count = 1;
    check(events.Update(sample, true, true) == 1, "first kill of next round plays");
    events.Update(sample, true, false);
    sample.count = 4;
    check(events.Update(sample, true, true) == 0, "enabling skips prior kills");
    events.Update(sample, false, true);
    sample.count = 5;
    check(events.Update(sample, true, true) == 0, "data reconnect does not replay kills");
    sample.handle += 0x8000;
    sample.count = 20;
    check(events.Update(sample, true, true) == 0, "controller generation change resets baseline");
    sample.services = 201;
    sample.count = 21;
    check(events.Update(sample, true, true) == 0, "new action service resets baseline");
    sample.count += 100;
    check(events.Update(sample, true, true) == 0, "implausible scoreboard correction is quiet");
    auto *bytes =
        static_cast<unsigned char *>(VirtualAlloc(nullptr, 0x20000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!bytes)
        return 2;
    const auto addr = [&](unsigned offset) { return reinterpret_cast<std::uintptr_t>(bytes + offset); };
    const auto put = []<typename T>(std::uintptr_t at, T value) {
        std::memcpy(reinterpret_cast<void *>(at), &value, sizeof(value));
    };
    const auto list = addr(0x1000), chunk = addr(0x2000), identity = chunk + offsets::EntityStride,
               controller = addr(0x10000), actions = addr(0x18000);
    Globals globals{addr(0x100), 0, addr(0x108), 0};
    put(globals.entitySlot, list);
    put(globals.localControllerSlot, controller);
    put(list + offsets::EntityTable, chunk);
    put(identity, controller);
    put(identity + 0x10, std::uint32_t(0x18001));
    put(controller + offsets::Identity, identity);
    put(controller + offsets::ControllerActions, actions);
    put(actions + offsets::RoundKills, std::int32_t(3));
    LocalMemory local;
    Memory memory{&local, LocalMemory::Read};
    check(ReadKillSample(memory, globals, sample) && sample.count == 3 && sample.handle == 0x18001,
          "reads the verified local controller action counter");
    put(actions + offsets::RoundKills, std::int32_t(-1));
    check(!ReadKillSample(memory, globals, sample) && !sample.controller, "invalid counter clears output");
    put(actions + offsets::RoundKills, std::int32_t(4));
    put(identity, addr(0x11000));
    check(!ReadKillSample(memory, globals, sample), "identity pointer mismatch is rejected");
    put(identity, controller);
    put(controller + offsets::ControllerActions, std::uintptr_t(0));
    check(!ReadKillSample(memory, globals, sample), "missing action service is rejected");
    VirtualFree(bytes, 0, MEM_RELEASE);
    std::printf("%u kill-event checks; %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
