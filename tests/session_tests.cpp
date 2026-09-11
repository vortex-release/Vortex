#include "spectator_reader.hpp"
#include "session_tools.hpp"
#include <map>
#include <vector>
#include <cstdio>
using namespace awareness;
using namespace awareness::cs2;
struct Fixture {
    std::map<std::uintptr_t, std::vector<std::byte>> fields;
    template <class T> void Put(std::uintptr_t at, const T &v) {
        auto &b = fields[at];
        b.resize(sizeof(v));
        std::memcpy(b.data(), &v, sizeof(v));
    }
    static bool Read(void *p, std::uintptr_t at, void *out, std::size_t size) noexcept {
        auto &f = static_cast<Fixture *>(p)->fields;
        auto it = f.upper_bound(at);
        if (it == f.begin())
            return false;
        --it;
        const auto offset = at - it->first;
        if (offset > it->second.size() || size > it->second.size() - offset)
            return false;
        std::memcpy(out, it->second.data() + offset, size);
        return true;
    }
    Memory memory{this, Read};
};
int main() {
    unsigned failed{};
    auto check = [&](bool ok, const char *why) {
        if (!ok) {
            ++failed;
            std::fprintf(stderr, "FAIL: %s\n", why);
        }
    };
    session::ReadyGate gate;
    check(!gate.Poll(false, 1, 100, 20, 0, 0), "disabled cannot accept");
    check(!gate.Poll(true, 1, 100, 0, 0, 0), "absent countdown cannot accept");
    check(!gate.Poll(true, 1, 100, 1000, 0, 0), "corrupt countdown cannot accept");
    check(!gate.Poll(true, 1, 100, 20, 2, 0), "already accepted cannot accept again");
    check(!gate.Poll(true, 1, 100, 20, 0, 10), "ready popup first observed");
    check(!gate.Poll(true, 1, 100, 20, 0, 900), "delay avoids immediate accidental activation");
    check(gate.Poll(true, 1, 100, 19, 0, 1010), "pending match accepts after one second");
    check(!gate.Poll(true, 1, 100, 18, 0, 3000), "repeated callbacks do not repeat acceptance");
    check(!gate.Poll(true, 1, 101, 20, 0, 4000) && gate.Poll(true, 1, 101, 19, 0, 5000),
          "new match with reused object is accepted once");
    check(!gate.Poll(true, 1, 101, 20, 0, 1), "clock rollback starts a new observation interval");
    Fixture f;
    constexpr std::uintptr_t list = 0x10000000, chunk = 0x20000000, me = 0x30000000, viewer = 0x31000000,
                             alive = 0x40000000, observer = 0x41000000, services = 0x50000000;
    f.Put(list + offsets::EntityTable, chunk);
    const auto entity = [&](unsigned index, std::uintptr_t address) {
        const auto id = chunk + index * offsets::EntityStride;
        f.Put(id, address);
        f.Put(id + 0x10, 0x80000u + index);
        f.Put(address + offsets::Identity, id);
    };
    entity(1, me);
    entity(2, viewer);
    entity(100, alive);
    entity(101, observer);
    f.Put(me + spectator_layout::Pawn, 0x80064u);
    f.Put(viewer + spectator_layout::Pawn, 0x80065u);
    f.Put(observer + spectator_layout::Services, services);
    f.Put(services + spectator_layout::Mode, std::uint8_t{2});
    f.Put(services + spectator_layout::Target, 0x80064u);
    std::array<char, 64> name{};
    std::memcpy(name.data(), "Viewer\nName", 11);
    f.Put(viewer + offsets::PlayerName, name);
    f.Put(viewer + spectator_layout::SteamId, std::uint64_t{76561198000000001ull});
    SpectatorFrame out;
    check(ReadSpectators(f.memory, list, me, alive, out) && out.count == 1 &&
              std::strcmp(out.entries[0].name, "Viewer Name") == 0 && out.entries[0].steamId == 76561198000000001ull,
          "live observer is named and sanitized");
    f.Put(services + spectator_layout::Mode, std::uint8_t{3});
    check(ReadSpectators(f.memory, list, me, alive, out) && out.count == 1, "chase camera is included");
    f.Put(services + spectator_layout::Mode, std::uint8_t{4});
    check(ReadSpectators(f.memory, list, me, alive, out) && out.count == 0, "free camera is excluded");
    f.Put(services + spectator_layout::Mode, std::uint8_t{2});
    f.Put(services + spectator_layout::Target, 0x100064u);
    check(ReadSpectators(f.memory, list, me, alive, out) && out.count == 0, "recycled target handle is rejected");
    f.Put(services + spectator_layout::Target, 0x80064u);
    f.Put(me + spectator_layout::Pawn, 0x80065u);
    check(ReadSpectators(f.memory, list, me, 0, out) && out.observing && out.count == 1,
          "local spectator sees viewers of the watched player");
    check(!ReadSpectators(f.memory, 0, me, alive, out) && out.count == 0, "disconnect clears stale names");
    return failed ? 1 : 0;
}
