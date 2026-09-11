#include "weapon_catalog.hpp"
#include <cstdio>
#include <cstring>
#include <set>
int main() {
    using namespace awareness;
    int failures{};
    std::set<std::uint32_t> ids;
    for (const auto &icon : WeaponIcons) {
        if (!ids.insert(icon.id).second || !icon.name[0] || icon.u0 < 0 || icon.v0 < 0 || icon.u1 > 1 || icon.v1 > 1 ||
            icon.u0 >= icon.u1 || icon.v0 >= icon.v1)
            ++failures;
    }
    if (ids.size() != 34 || FindWeaponIcon(0) || FindWeaponIcon(42) || FindWeaponIcon(99999))
        ++failures;
    if (std::strcmp(FindWeaponIcon(7)->name, "AK-47") || std::strcmp(FindWeaponIcon(9)->name, "AWP") ||
        std::strcmp(FindWeaponIcon(61)->name, "USP-S"))
        ++failures;
    std::printf("34 firearm mappings, atlas bounds and non-firearm fallbacks; %d failures.\n", failures);
    return failures ? 1 : 0;
}
