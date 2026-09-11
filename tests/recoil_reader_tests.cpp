#include "combat_reader.hpp"
#include <map>
#include <vector>
#include <cstdio>
using namespace awareness;
using namespace awareness::cs2;
struct MemoryFixture {
    std::map<std::uintptr_t, std::vector<std::byte>> fields;
    template <class T> void Put(std::uintptr_t at, const T &value) {
        auto &bytes = fields[at];
        bytes.resize(sizeof(value));
        std::memcpy(bytes.data(), &value, sizeof(value));
    }
    static bool Read(void *context, std::uintptr_t at, void *out, std::size_t size) noexcept {
        auto &fields = static_cast<MemoryFixture *>(context)->fields;
        auto it = fields.upper_bound(at);
        if (it == fields.begin())
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
    int failures{};
    const auto check = [&](bool value, const char *why) {
        if (!value) {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", why);
        }
    };
    MemoryFixture f;
    constexpr std::uintptr_t client = 0x10000000, list = 0x20000000, chunk = 0x30000000, pawn = 0x40000000,
                             weapon = 0x50000000, scene = 0x60000000, services = 0x70000000, sensitivity = 0x71000000;
    const auto entity = [&](unsigned index, std::uintptr_t address) {
        const auto identity = chunk + index * offsets::EntityStride;
        f.Put(identity, address);
        f.Put(identity + 0x10, 0x80000u + index);
        f.Put(address + offsets::Identity, identity);
    };
    f.Put(list + offsets::EntityTable, chunk);
    entity(4, pawn);
    entity(5, weapon);
    f.Put(pawn + offsets::LifeState, std::uint8_t{0});
    f.Put(pawn + offsets::SceneNode, scene);
    f.Put(scene + offsets::Origin, Vector3{100, 200, 300});
    f.Put(pawn + offsets::ViewOffset, Vector3{0, 0, 64});
    f.Put(pawn + offsets::WeaponServices, services);
    f.Put(services + offsets::ActiveWeapon, 0x80005u);
    f.Put(weapon + offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, std::uint16_t{7});
    f.Put(pawn + offsets::ShotsFired, 3u);
    f.Put(weapon + offsets::RecoilIndex, 3.f);
    f.Put(weapon + offsets::LastShotTime, 100.f);
    combat::RecoilSample sample;
    check(ReadRecoilMetadata(f.memory, list, pawn, sample) && sample.weapon == 7 && sample.shots == 3 &&
              sample.weaponHandle == 0x80005,
          "active weapon resolves to its definition and current shot count");
    constexpr std::uintptr_t start = 0x80000000, end = 0x80000020;
    f.Put(services + trajectory_offsets::BulletPawn, pawn);
    // The real caller passes weapon services; bullet services is a distinct component.
    constexpr std::uintptr_t bulletServices = 0x72000000;
    f.Put(pawn + offsets::BulletServices, bulletServices);
    f.Put(bulletServices + trajectory_offsets::BulletPawn, pawn);
    f.Put(pawn + offsets::Team, std::uint8_t{2});
    f.Put(weapon + offsets::EntityOwner, 0x80004u);
    f.Put(start, Vector3{1, 2, 3});
    f.Put(end, Vector3{100, 200, 300});
    flight::Shot shot;
    check(ReadDirectShot(f.memory, list, services, weapon, start, end, shot) && shot.shooter == 4 && shot.team == 2 &&
              shot.end.x == 100,
          "queued shot captures exact endpoints through weapon services");
    check(!ReadDirectShot(f.memory, list, bulletServices, weapon, start, end, shot),
          "bullet services cannot impersonate the weapon-service callback");
    f.Put(weapon + offsets::EntityOwner, 0xffffffffu);
    check(ReadDirectShot(f.memory, list, services, weapon, start, end, shot),
          "active weapon validates a predicted shot whose owner handle is unset");
    f.Put(services + offsets::ActiveWeapon, 0x80006u);
    check(!ReadDirectShot(f.memory, list, services, weapon, start, end, shot),
          "unset-owner shot cannot borrow another active weapon");
    f.Put(services + offsets::ActiveWeapon, 0x80005u);
    f.Put(weapon + offsets::EntityOwner, 0x80004u);
    f.Put(pawn + offsets::WeaponServices, std::uintptr_t{0x1234});
    check(!ReadDirectShot(f.memory, list, services, weapon, start, end, shot),
          "stale weapon service cannot claim a reused pawn");
    f.Put(pawn + offsets::WeaponServices, services);
    f.Put(weapon + offsets::EntityOwner, 0x80006u);
    check(!ReadDirectShot(f.memory, list, services, weapon, start, end, shot),
          "unrelated weapon cannot produce an attributed tracer");
    f.Put(weapon + offsets::EntityOwner, 0x80004u);
    f.Put(chunk + 5 * offsets::EntityStride + 0x10, 0x100005u);
    check(!ReadRecoilMetadata(f.memory, list, pawn, sample), "reused weapon slot is rejected");
    f.Put(chunk + 5 * offsets::EntityStride + 0x10, 0x80005u);
    f.Put(weapon + offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, std::uint16_t{0});
    check(!ReadRecoilMetadata(f.memory, list, pawn, sample),
          "unknown weapon does not receive a guessed recoil profile");
    f.Put(weapon + offsets::AttributeManager + offsets::ItemView + offsets::ItemDefinition, std::uint16_t{7});
    f.Put(pawn + offsets::ShotsFired, 301u);
    check(!ReadRecoilMetadata(f.memory, list, pawn, sample), "implausible shots rejected");
    f.Put(client + offsets::Sensitivity, sensitivity);
    f.Put(sensitivity + offsets::SensitivityValue, 2.f);
    f.Put(pawn + offsets::MouseSensitivity, 0.f);
    f.Put(pawn + offsets::FovSensitivity, 1.f);
    float scale{};
    check(ReadMouseSensitivity(f.memory, client, pawn, scale) && scale == 2, "normal sensitivity uses cvar");
    f.Put(pawn + offsets::MouseSensitivity, .8f);
    f.Put(pawn + offsets::FovSensitivity, .4f);
    check(ReadMouseSensitivity(f.memory, client, pawn, scale) && scale == .8f,
          "scoped override already includes zoom multiplier");
    f.Put(pawn + offsets::FovSensitivity, -1.f);
    check(!ReadMouseSensitivity(f.memory, client, pawn, scale), "uninitialized camera sensitivity stops input");
    return failures ? 1 : 0;
}
