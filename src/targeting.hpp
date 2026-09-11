#pragma once
#include <awareness/TargetBone.hpp>
namespace awareness {
// DLL-owned dropdown ordinal (0..3), accessed only on the render thread.
inline int g_ActiveTargetBone = 0;
inline TargetBone ActiveTargetBone() noexcept {
    return TargetBones[g_ActiveTargetBone >= 0 && g_ActiveTargetBone < 4 ? g_ActiveTargetBone : 0];
}
} // namespace awareness
