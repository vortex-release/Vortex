#pragma once
#include <array>
#include <cstdint>
// Pinned build14181 client ABI; see docs/cosmetics-native.md and inspect-cosmetics.py.
namespace awareness::cosmetics::abi {
inline constexpr std::uint32_t Timestamp = 0x6AA1AE5E, ImageSize = 0x27DE000;
inline constexpr std::uintptr_t ItemView = 0x50;           // C_AttributeContainer::m_Item
inline constexpr std::uintptr_t AttributeManager = 0x11A8; // C_EconEntity::m_AttributeManager
inline constexpr std::uintptr_t ItemDefinition = 0x1BA;    // C_EconItemView::m_iItemDefinitionIndex
inline constexpr std::uintptr_t ItemID = 0x1C8;            // C_EconItemView::m_iItemID
inline constexpr std::uintptr_t IDHigh = 0x1D0;            // C_EconItemView::m_iItemIDHigh
inline constexpr std::uintptr_t IDLow = 0x1D4;             // C_EconItemView::m_iItemIDLow
inline constexpr std::uintptr_t Account = 0x1D8;           // C_EconItemView::m_iAccountID
inline constexpr std::uintptr_t Initialized = 0x1E8;       // C_EconItemView::m_bInitialized
inline constexpr std::uintptr_t DisallowSOC = 0x1E9;       // C_EconItemView::m_bDisallowSOC
inline constexpr std::uintptr_t RestoreMaterial = 0x1B8;   // C_EconItemView::m_bRestoreCustomMaterialAfterPrecache
inline constexpr std::uintptr_t Name = 0x2F8;              // C_EconItemView::m_szCustomName
inline constexpr std::uintptr_t Paint = 0x1680;            // C_EconEntity::m_nFallbackPaintKit
inline constexpr std::uintptr_t Seed = 0x1684;             // C_EconEntity::m_nFallbackSeed
inline constexpr std::uintptr_t Wear = 0x1688;             // C_EconEntity::m_flFallbackWear
inline constexpr std::uintptr_t StatTrak = 0x168C;         // C_EconEntity::m_nFallbackStatTrak
inline constexpr std::uintptr_t MyWeapons = 0x48;          // CPlayer_WeaponServices::m_hMyWeapons
inline constexpr std::uintptr_t Owner = 0x520;             // C_BaseEntity::m_hOwnerEntity
inline constexpr std::uintptr_t Subclass = 0x380;          // C_BaseEntity::m_nSubclassID
inline constexpr std::uintptr_t ModelState = 0x140;        // CSkeletonInstance::m_modelState
inline constexpr std::uintptr_t ModelName = 0xA8;          // CModelState::m_ModelName
inline constexpr std::uintptr_t MeshMask = 0x208;          // CModelState::m_MeshGroupMask
inline constexpr std::uintptr_t Gloves = 0x1690;           // C_CSPlayerPawn::m_EconGloves
inline constexpr std::uintptr_t ReapplyGloves = 0x168D;    // C_CSPlayerPawn::m_bNeedToReApplyGloves
inline constexpr std::uintptr_t SteamID = 0x780;           // CBasePlayerController::m_steamID
inline constexpr std::uintptr_t HudArms = 0x1B84;          // C_CSPlayerPawn::m_hHudModelArms
inline constexpr std::uintptr_t Child = 0x40;              // CGameSceneNode::m_pChild
inline constexpr std::uintptr_t NextSibling = 0x48;        // CGameSceneNode::m_pNextSibling
inline constexpr std::uintptr_t SceneOwner = 0x30;         // CGameSceneNode::m_pOwner
inline constexpr std::uintptr_t AttributeList = 0x208;     // C_EconItemView::m_AttributeList
inline constexpr std::uintptr_t AttributeDef = 0x30;       // CEconItemAttribute::m_iAttributeDefinitionIndex
inline constexpr std::uintptr_t AttributeValue = 0x34;     // CEconItemAttribute::m_flValue
// Current RemoveAttribute machine code confirms vector offsets and CEconItemAttribute stride.
inline constexpr std::uintptr_t AttributeCount = 0x210, AttributeData = 0x218, AttributeStride = 0x48;
inline constexpr std::uintptr_t CompositeOwner = 0x608;         // current 0x7DFF87 and 0x803DA2 callsites
inline constexpr std::uintptr_t ModelHandle = 0xA0;             // CModelState::m_hModel; SetModel reads scene+0x1E0
inline constexpr std::uintptr_t AttributesInitialized = 0x11A0; // C_EconEntity::m_bAttributesInitialized
struct Function {
    std::uintptr_t rva;
    std::array<unsigned char, 24> bytes;
};
inline constexpr Function SetAttributeFn{0x1123210,
                                         {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x81, 0xC1,
                                          0x08, 0x02, 0x00, 0x00, 0xE8, 0x1B, 0xFC, 0xFF, 0xFF, 0x48, 0x8B, 0xCB}};
inline constexpr Function RemoveAttributeFn{0x11214A0,
                                            {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0x81, 0x10, 0x02, 0x00,
                                             0x00, 0x44, 0x0F, 0xB7, 0xCA, 0x33, 0xD2, 0x48, 0x8B, 0xD9, 0x85, 0xC0}};
inline constexpr Function InvalidateDescriptionFn{0x111ED20, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
                                                              0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
                                                              0x8D, 0xB9, 0x00, 0x02, 0x00, 0x00, 0x48, 0x8B}};
inline constexpr Function SetModelFn{0x939940,
                                     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x4C, 0x8B, 0xC2,
                                      0x48, 0x8B, 0x0D, 0xD5, 0xE9, 0xA8, 0x01, 0x48, 0x8D, 0x54, 0x24, 0x40}};
inline constexpr Function SetMaskFn{0xA85840, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48,
                                               0x83, 0xEC, 0x20, 0x48, 0x8D, 0x99, 0x40, 0x01, 0x00, 0x00, 0x48, 0x8B}};
inline constexpr Function UpdateViewModelFn{0xAC4E20,
                                            {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0x72, 0x27,
                                             0x74, 0xFF, 0x48, 0x83, 0xBB, 0x88, 0x03, 0x00, 0x00, 0x00, 0x74, 0x17}};
inline constexpr Function UpdateCompositeFn{0x143F020,
                                            {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89,
                                             0x74, 0x24, 0x20, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20}};
inline constexpr Function UpdateSkinFn{0x7DE490,
                                       {0x40, 0x55, 0x53, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x00, 0xFE, 0xFF,
                                        0xFF, 0x48, 0x81, 0xEC, 0x00, 0x03, 0x00, 0x00, 0x44, 0x0F, 0xB6, 0xFA}};
} // namespace awareness::cosmetics::abi
