#pragma once
#include "Math.hpp"
#include <array>
#include <cstdint>
#include <optional>
namespace awareness {
// Verified against the installed build-14181 SAS and Phoenix agent skeletons.
// Index zero is root_motion, not pelvis. Custom rigs require their own mapping.
enum class TargetBone : int { Head = 7, Neck = 6, Chest = 4, Pelvis = 1 };
inline constexpr std::array TargetBones{TargetBone::Head, TargetBone::Neck, TargetBone::Chest, TargetBone::Pelvis};
inline constexpr const char *TargetBoneNames[]{"Head", "Neck", "Upper Chest", "Center Mass/Pelvis"};
inline constexpr bool ValidTargetBone(int id) noexcept {
    for (auto bone : TargetBones)
        if (id == static_cast<int>(bone))
            return true;
    return false;
}
inline constexpr int TargetBoneChoice(int id) noexcept {
    for (int i = 0; i < 4; ++i)
        if (id == static_cast<int>(TargetBones[i]))
            return i;
    return -1;
}
// Indexed by engine bone ID, not by dropdown position. World-space positions.
struct EntityBones {
    Vector3 positions[8]{};
    std::uint32_t validMask{};
};
inline std::optional<Vector3> ResolveTargetBone(const EntityBones &bones, TargetBone selected) noexcept {
    const int index = static_cast<int>(selected);
    if (!ValidTargetBone(index) || !(bones.validMask & (1u << index)) || !Finite(bones.positions[index]))
        return {};
    return bones.positions[index];
}
} // namespace awareness
