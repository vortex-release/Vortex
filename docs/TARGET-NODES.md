# Skeletal target nodes — version 3.4

The target-node selector is integrated into EntityAwarenessOverlay.dll. Open **Insert > Tracking > Target**, select a node, enable tracking, close the menu and hold the configured key. The selected node participates in the existing angular FOV, team/alive filtering and frame-time-adjusted interpolation. Auto-save and the footer Save/Load controls persist the choice in OverlaySettings.ini beside the DLL.

| Dropdown label | Saved ordinal / g_ActiveTargetBone | TargetBone | Engine bone ID |
| --- | --- | --- | --- |
| Head | 0 | Head | 7 |
| Neck | 1 | Neck | 6 |
| Upper Chest | 2 | Chest | 4 |
| Center Mass/Pelvis | 3 | Pelvis | 1 |

These IDs were verified in the installed build-14181 SAS and Phoenix agent skeletons. Their first nodes are root_motion, pelvis, spine_0, spine_1, spine_2, spine_3, neck_0 and head_0. Index 0 is not pelvis, and index 6 is not head in these rigs. Custom or future rigs need a verified mapping. The mapping is deliberately explicit rather than assuming every Source-family game has identical IDs.

`g_ActiveTargetBone` is an inline DLL-owned integer in `src/targeting.hpp`, accessed on the render thread. The ImGui Combo binds directly to it. It is a contiguous dropdown ordinal, not the sparse engine ID. `ActiveTargetBone()` maps it through `TargetBones` to the enum. The settings mirror is `VisualOptions::activeTargetBone` and the serialized key is:

```ini
[Visual]
activeTargetBone=0
```

Use 0..3; older profiles without this key default to Head. Out-of-range values reject profile loading atomically. All other existing profile values are retained during deployment. The selected node does not enable tracking by itself.

## Reader implementation and provenance

`ReadFrame` passes the active enum to `ReadTargetBone` for each live, non-dormant pawn. The reader uses the scene skeleton's schema `m_modelState` plus the independently verified bone-array pointer/count offsets. The current array is an array of 32-byte position/scale/quaternion transforms, not a 3x4 matrix array:

```cpp
struct BoneTransform {
    awareness::Vector3 position; // World-space translation.
    float scale;
    std::array<float, 4> rotation;
};
static_assert(sizeof(BoneTransform) == 32);
```

The position is read from `array + static_cast<int>(selectedBone) * BoneStride`. It is already in world space; the entity origin is not added again. EntitySnapshot::origin remains the actual scene origin for bounding boxes and other rendering.

The reader checks array alignment, bounds/count, readable storage, finite transforms, plausible distance from the entity, and unchanged pointer/count after sampling. It clears node output before each read. Missing or invalid bones skip camera targeting while leaving the entity's HUD available. No root, collision midpoint or previous-frame pose is substituted. Pointer/count checks detect reallocation, but do not promise an atomic engine animation snapshot.

`Updated Offsets/bone-layout.json` contains the layout build, offsets, named IDs, model hashes and inspected client.dll hash/RVA. The generated `src/cs2_offsets.hpp` consumes both that file and the normal JSON/HPP schema snapshot. Static assertions keep the public enum and transform stride in agreement with the generated layout. `generate-offsets.ps1 -Check` validates provenance as well as the 36 consumed schema/global values.

On a new game build, refreshing schema offsets does not establish that the internal skeletal layout is still valid. Bone reading remains disabled while `BoneLayoutBuild != ExpectedBuild`. Verify the new binary and model skeletons, update the separate layout record and enum if necessary, then rebuild. The enum/storage contract must be revised together if supported IDs exceed the present eight-entry array.

The static inspection used the official [ValveResourceFormat CLI 20.0](https://github.com/ValveResourceFormat/ValveResourceFormat/releases/tag/20.0) to decode the installed model resources. This tool and extracted models are not runtime dependencies and are not included in the release.

## Version-3 host snapshots

FrameSnapshot version 3 appends `EntityBones bones[MaxEntities]` after the version-2 weapon array. Every bone entry corresponds to the entity at the same ordinal. Positions and mask bits are indexed by the **engine bone ID**, not the menu ordinal. A host should supply all supported nodes so users can change selection without a new host API. For each current pose:

```cpp
#include <awareness/OverlayApi.hpp>

void SetNode(awareness::FrameSnapshot& frame, unsigned entityIndex,
             awareness::TargetBone node, awareness::Vector3 worldPosition) {
    const int id = static_cast<int>(node);
    if (entityIndex >= frame.entityCount || entityIndex >= awareness::MaxEntities ||
        !awareness::ValidTargetBone(id))
        return;
    auto& bones = frame.bones[entityIndex];
    bones.validMask &= ~(1u << id);
    if (!awareness::Finite(worldPosition))
        return;
    bones.positions[id] = worldPosition;
    bones.validMask |= 1u << id;
}

// Start each pose with FrameSnapshot frame{} and fill current entities/matrix.
// SetNode(frame, i, TargetBone::Head, currentHeadWorldPosition);
// Repeat for Neck, Chest and Pelvis, then submit frame and matching CameraInput.
```

Use the host's same world-coordinate convention as its entities and camera. The automatic CS2 path performs its existing Z-up to portable camera conversion. Generic `CopyEntityList` adapters clear bone data; extend the host adapter to provide current bones after copying entities.

Version-1 and version-2 frames are still accepted at their original sizes, preserve their existing data, and clear absent fields. Their HUD remains usable. They cannot provide skeletal tracking until upgraded to version 3. Configuration, EntitySnapshot and the independent tracking/effects API layouts are unchanged.

## Verification limits

Release and Debug run the reader, camera, INI, ABI and actual DLL/ImGui smoke tests. The smoke proves selection changes camera output and survives automatic save plus GUI Load. Installed-file inspection establishes the current SAS/Phoenix IDs and array layout; the 3.6 read-only probe also produced live target nodes and preview poses. See VALIDATION.md for the complete results.
