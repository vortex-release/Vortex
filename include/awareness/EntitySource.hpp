#pragma once
#include "OverlayApi.hpp"
#include <algorithm>

namespace awareness {
// Implement these two methods using your engine SDK / observer entity list.
// Hold the engine's read lock across CopyEntityList, or call on its simulation
// thread. The DLL never retains engine pointers or calls back into this object.
class IEntitySource {
  public:
    virtual ~IEntitySource() = default;
    virtual std::uint32_t SlotCount() const noexcept = 0;
    virtual bool ReadEntity(std::uint32_t slot, EntitySnapshot &output) const noexcept = 0;
};
inline void CopyEntityList(const IEntitySource &source, FrameSnapshot &frame) noexcept {
    std::fill(std::begin(frame.weaponDefinitionIndices), std::end(frame.weaponDefinitionIndices), 0u);
    std::fill(std::begin(frame.bones), std::end(frame.bones), EntityBones{});
    frame.entityCount = (std::min)(source.SlotCount(), MaxEntities);
    for (std::uint32_t i = 0; i < frame.entityCount; ++i) {
        frame.entities[i] = {};
        if (!source.ReadEntity(i, frame.entities[i]))
            frame.entities[i] = {};
        frame.entities[i].name[sizeof(frame.entities[i].name) - 1] = '\0';
    }
    for (std::uint32_t i = frame.entityCount; i < MaxEntities; ++i)
        frame.entities[i] = {};
}
} // namespace awareness
