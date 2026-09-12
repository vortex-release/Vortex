#pragma once
#include "cs2_reader.hpp"
#include "skeleton.hpp"
namespace awareness::cs2 {
inline bool ReadSkeletonPose(const Memory &memory, std::uintptr_t scene, const EntitySnapshot &entity,
                             skeleton::Pose &out) noexcept {
    out = {};
    if constexpr (offsets::BoneLayoutBuild != offsets::ExpectedBuild)
        return false;
    if (!entity.valid || entity.dormant || entity.health <= 0 || !Finite(entity.origin))
        return false;
    std::uintptr_t array{}, after{};
    std::uint16_t count{}, afterCount{};
    if (!memory.Field(scene, offsets::ModelState + offsets::BoneArray, array) || array < 0x10000 || (array & 15) ||
        !memory.Field(scene, offsets::ModelState + offsets::BoneCount, count) || count < skeleton::JointCount ||
        count > 1024)
        return false;
    std::array<BoneTransform, skeleton::JointCount> joints;
    if (!memory.Read(array, joints))
        return false;
    skeleton::Pose candidate;
    candidate.entity = entity.id;
    for (unsigned i = 0; i < joints.size(); ++i) {
        const auto &joint = joints[i];
        float norm{};
        for (float q : joint.rotation)
            norm += q * q;
        if (!PlausiblePosition(joint.position) || Distance(joint.position, entity.origin) > 512 ||
            !std::isfinite(joint.scale) || joint.scale < .01f || joint.scale > 10 || !std::isfinite(norm) ||
            norm < .25f || norm > 4)
            continue;
        candidate.positions[i] = joint.position;
        candidate.validMask |= 1u << i;
    }
    if (!memory.Field(scene, offsets::ModelState + offsets::BoneArray, after) || after != array ||
        !memory.Field(scene, offsets::ModelState + offsets::BoneCount, afterCount) || afterCount != count)
        return false;
    unsigned segments{};
    for (auto link : skeleton::Links)
        segments += skeleton::Segment(candidate, link[0], link[1]);
    if (!segments)
        return false;
    out = candidate;
    return true;
}
} // namespace awareness::cs2
