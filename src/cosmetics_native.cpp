#include "cosmetics_native.hpp"
#include "cosmetics_abi.hpp"
#include "cosmetics_state.hpp"
#include "assist_reader.hpp"
#include "build_verification.hpp"
#include "runtime_support.hpp"
#include <Windows.h>
#include <Psapi.h>
#include <atomic>
#include <mutex>

// Lifecycle adapted from Lefrizzel Ai (MIT) and user-provided Velocity sources.
// Calls are pinned independently. No inventory services or coordinator messages.
namespace awareness::cosmetics {
namespace {
using namespace cs2;
bool ReadLocal(void *, std::uintptr_t at, void *out, std::size_t bytes) noexcept {
    if (at < 0x10000 || at > 0x00007fffffffffffULL - bytes)
        return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void *>(at), bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool CompareWrite(void *, std::uintptr_t at, const void *before, const void *after, std::size_t bytes) noexcept {
    if (at < 0x10000 || at > 0x00007fffffffffffULL - bytes)
        return false;
    __try {
        auto *where = reinterpret_cast<void *>(at);
        if (std::memcmp(where, before, bytes))
            return false;
        std::memcpy(where, after, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
const Memory memory{nullptr, ReadLocal};
struct Model {
    std::uintptr_t entity{}, scene{};
    std::uint32_t handle{};
    std::uint64_t originalMask{}, appliedMask{};
    ModelPath original{}, applied{};
    ModelRequest request;
    bool changed{}, pending{};
    ULONGLONG retryAt{};
};
struct Weapon {
    std::uintptr_t entity{};
    std::uint32_t handle{}, owner{}, originalSubclass{}, appliedSubclass{};
    ItemState original{}, applied{};
    Model model;
    PaintAttributes attributes{}, appliedAttributes{};
    MaterialRequest material;
    bool used{}, changed{}, seen{};
    ULONGLONG retryAt{};
};
struct Glove {
    std::uintptr_t pawn{};
    std::uint32_t handle{};
    ItemState original{}, applied{};
    std::array<AttributeState, 3> attributes{}, appliedAttributes{};
    bool changed{};
};
struct Runtime {
    std::uintptr_t client{};
    std::mutex optionsMutex;
    Options options;
    std::array<Weapon, 64> weapons{};
    Glove glove;
    Model agent, view;
    std::uint32_t viewWeapon{};
    CatalogController catalog;
    std::shared_ptr<const Catalog> cachedCatalog;
    std::atomic<bool> ready{}, restoring{};
    std::atomic<NativeState> phase{NativeState::Disabled};
    std::atomic<unsigned> applied{}, restores{}, faults{}, tracked{}, inFlight{}, materialRefreshes{};
    std::atomic<DWORD> gameThread{};
} runtime;
bool SameEntity(std::uintptr_t list, std::uintptr_t entity, std::uint32_t handle) noexcept {
    std::uint32_t current{};
    return handle && EntityAt(memory, list, handle) == entity && FullHandle(memory, entity, current) &&
           current == handle;
}
bool Function(const abi::Function &fn) noexcept {
    std::array<unsigned char, 24> actual{};
    return memory.Read(runtime.client + fn.rva, actual) && actual == fn.bytes;
}
bool ValidImage(std::uintptr_t client) noexcept {
    const auto engine = GetModuleHandleW(L"engine2.dll");
    MODULEINFO info{}, engineInfo{};
    if (!engine || !GetModuleInformation(GetCurrentProcess(), engine, &engineInfo, sizeof(engineInfo)) ||
        !GetModuleInformation(GetCurrentProcess(), reinterpret_cast<HMODULE>(client), &info, sizeof(info)))
        return false;
    const auto build = VerifyEngineBuild(memory, reinterpret_cast<std::uintptr_t>(engine), engineInfo.SizeOfImage);
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (build.check != BuildCheck::Verified || build.build != offsets::ExpectedBuild || !memory.Read(client, dos) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 || dos.e_lfanew > 4096 ||
        !memory.Read(client + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != abi::Timestamp || nt.OptionalHeader.SizeOfImage != abi::ImageSize ||
        info.SizeOfImage != abi::ImageSize)
        return false;
    return Function(abi::GetPaintFn) && Function(abi::SetAttributeFn) && Function(abi::RemoveAttributeFn) &&
           Function(abi::InvalidateDescriptionFn) && Function(abi::SetModelFn) && Function(abi::SetMaskFn) &&
           Function(abi::UpdateViewModelFn) && Function(abi::UpdateCompositeFn) && Function(abi::UpdateSkinFn);
}
void Fault() noexcept {
    runtime.faults.fetch_add(1);
    runtime.phase = NativeState::NativeFault;
    runtime.restoring = true;
}
bool CallOne(const abi::Function &fn, std::uintptr_t object) noexcept {
    __try {
        reinterpret_cast<void (*)(void *)>(runtime.client + fn.rva)(reinterpret_cast<void *>(object));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Fault();
        return false;
    }
}
bool CallFlag(const abi::Function &fn, std::uintptr_t object, bool value) noexcept {
    __try {
        reinterpret_cast<void (*)(void *, bool)>(runtime.client + fn.rva)(reinterpret_cast<void *>(object), value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Fault();
        return false;
    }
}
bool SetModel(std::uintptr_t object, const char *model) noexcept {
    __try {
        reinterpret_cast<void (*)(void *, const char *)>(runtime.client +
                                                         abi::SetModelFn.rva)(reinterpret_cast<void *>(object), model);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Fault();
        return false;
    }
}
bool SetMask(std::uintptr_t scene, std::uint64_t mask) noexcept {
    __try {
        reinterpret_cast<void (*)(void *, std::uint64_t)>(runtime.client +
                                                          abi::SetMaskFn.rva)(reinterpret_cast<void *>(scene), mask);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Fault();
        return false;
    }
}
bool SetAttribute(std::uintptr_t view, unsigned slot, AttributeState attr) noexcept {
    static constexpr const char *names[]{"set item texture prefab", "set item texture seed", "set item texture wear"};
    __try {
        if (attr.present)
            reinterpret_cast<void (*)(void *, const char *, float)>(runtime.client + abi::SetAttributeFn.rva)(
                reinterpret_cast<void *>(view), names[slot], attr.value);
        else
            reinterpret_cast<void (*)(void *, int)>(runtime.client + abi::RemoveAttributeFn.rva)(
                reinterpret_cast<void *>(view), 6 + slot);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Fault();
        return false;
    }
}
bool ConstructedView(std::uintptr_t view) noexcept {
    std::uintptr_t vtable{}, entry{};
    return memory.Read(view, vtable) && vtable >= runtime.client && vtable < runtime.client + abi::ImageSize &&
           memory.Read(vtable, entry) && entry >= runtime.client && entry < runtime.client + abi::ImageSize;
}
bool ReadItem(std::uintptr_t entity, bool glove, ItemState &s) noexcept {
    const auto view = glove ? entity + abi::Gloves : entity + abi::AttributeManager + abi::ItemView;
    if (!ConstructedView(view) || !memory.Field(view, abi::ItemDefinition, s.definition) ||
        !memory.Field(view, abi::ItemID, s.itemId) || !memory.Field(view, abi::IDHigh, s.high) ||
        !memory.Field(view, abi::IDLow, s.low) || !memory.Field(view, abi::Account, s.account) ||
        !memory.Field(view, abi::Initialized, s.initialized) || !memory.Field(view, abi::DisallowSOC, s.disallow) ||
        !memory.Field(view, abi::RestoreMaterial, s.restoreMaterial) || !memory.Field(view, abi::Name, s.name) ||
        s.initialized > 1 || s.disallow > 1 || s.restoreMaterial > 1)
        return false;
    if (glove)
        return true;
    return memory.Field(entity, abi::Paint, s.paint) && memory.Field(entity, abi::Seed, s.seed) &&
           memory.Field(entity, abi::Wear, s.wear) && memory.Field(entity, abi::StatTrak, s.statTrak) &&
           std::isfinite(s.wear) && s.wear >= 0 && s.wear <= 1 && s.paint >= 0 && s.paint <= 100000;
}
PatchSet ItemPatch(std::uintptr_t entity, bool glove, const ItemState &from, const ItemState &to) noexcept {
    PatchSet patches;
    const auto view = glove ? entity + abi::Gloves : entity + abi::AttributeManager + abi::ItemView;
    patches.Add(view + abi::ItemDefinition, from.definition, to.definition);
    patches.Add(view + abi::ItemID, from.itemId, to.itemId);
    patches.Add(view + abi::IDHigh, from.high, to.high);
    patches.Add(view + abi::IDLow, from.low, to.low);
    patches.Add(view + abi::Account, from.account, to.account);
    patches.Add(view + abi::Initialized, from.initialized, to.initialized);
    patches.Add(view + abi::DisallowSOC, from.disallow, to.disallow);
    patches.Add(view + abi::RestoreMaterial, from.restoreMaterial, to.restoreMaterial);
    patches.Add(view + abi::Name, from.name, to.name);
    if (!glove) {
        patches.Add(entity + abi::Paint, from.paint, to.paint);
        patches.Add(entity + abi::Seed, from.seed, to.seed);
        patches.Add(entity + abi::Wear, from.wear, to.wear);
        patches.Add(entity + abi::StatTrak, from.statTrak, to.statTrak);
    }
    return patches;
}
bool ReadModel(std::uintptr_t entity, Model &out) noexcept {
    std::uintptr_t name{}, binding{}, resource{};
    Model model;
    model.entity = entity;
    if (!FullHandle(memory, entity, model.handle) || !memory.Field(entity, offsets::SceneNode, model.scene) ||
        !memory.Field(model.scene, abi::ModelState + abi::ModelName, name) ||
        !memory.Field(model.scene, abi::ModelState + abi::ModelHandle, binding) || !memory.Read(binding, resource) ||
        !resource || !memory.Field(model.scene, abi::ModelState + abi::MeshMask, model.originalMask))
        return false;
    bool ended = false;
    for (std::size_t i = 0; i < model.original.size(); ++i) {
        if (!memory.Read(name + i, model.original[i]))
            return false;
        if (!model.original[i]) {
            ended = true;
            break;
        }
        if (static_cast<unsigned char>(model.original[i]) < 32)
            return false;
    }
    const std::string_view path(model.original.data(), ended ? std::strlen(model.original.data()) : 0);
    if (!ended || path.empty() || path.find("..") != path.npos || path.find(':') != path.npos ||
        !path.ends_with(".vmdl"))
        return false;
    model.applied = model.original;
    model.appliedMask = model.originalMask;
    out = model;
    return true;
}
bool PendingModel(Model &model) noexcept {
    model.pending = true;
    model.retryAt = GetTickCount64() + 250;
    runtime.phase = NativeState::LoadingModel;
    return false;
}
ModelObservation ObserveModel(Model &model, const Model &current) noexcept {
    const auto result = model.request.Observe(current.original);
    if (result == ModelObservation::Confirmed) {
        model.applied = model.request.target;
        model.pending = false;
        model.retryAt = 0;
    }
    return result;
}
bool RestoreModel(std::uintptr_t list, Model &model) noexcept {
    if (!model.changed) {
        model = {};
        return true;
    }
    if (!SameEntity(list, model.entity, model.handle)) {
        model = {};
        return true;
    }
    if (model.pending && GetTickCount64() < model.retryAt)
        return false;
    Model current;
    if (!ReadModel(model.entity, current))
        return PendingModel(model);
    auto observed = ObserveModel(model, current);
    if (observed == ModelObservation::Pending)
        return PendingModel(model);
    if (observed == ModelObservation::Superseded) {
        model = {}; // A distinct externally supplied model supersedes our ownership.
        return true;
    }
    bool ownModel = current.original == model.applied;
    if (ownModel && model.original != model.applied) {
        model.request.Begin(current.original, model.original);
        if (!SetModel(model.entity, model.original.data()))
            return false;
        if (!ReadModel(model.entity, current))
            return PendingModel(model);
        observed = ObserveModel(model, current);
        if (observed == ModelObservation::Pending)
            return PendingModel(model);
        if (observed == ModelObservation::Superseded) {
            model = {};
            return true;
        }
        ownModel = current.original == model.applied;
    }
    if (ownModel && current.originalMask == model.appliedMask && model.originalMask != model.appliedMask) {
        if (!SetMask(current.scene, model.originalMask))
            return false;
        std::uint64_t mask{};
        if (!memory.Field(current.scene, abi::ModelState + abi::MeshMask, mask) || mask != model.originalMask)
            return PendingModel(model);
    }
    model = {};
    return true;
}
bool ApplyModel(Model &model, std::string_view path, std::uint64_t mask) noexcept {
    if (path.size() >= model.applied.size())
        return false;
    if (model.pending && GetTickCount64() < model.retryAt)
        return false;
    Model current;
    if (!ReadModel(model.entity, current))
        return PendingModel(model);
    auto observed = ObserveModel(model, current);
    if (observed == ModelObservation::Pending)
        return PendingModel(model);
    if (observed == ModelObservation::Superseded) {
        model.original = model.applied = current.original;
        model.originalMask = model.appliedMask = current.originalMask;
        model.changed = false;
    }
    if (path != std::string_view(current.original.data())) {
        ModelPath next{};
        std::copy(path.begin(), path.end(), next.begin());
        model.request.Begin(current.original, next);
        model.changed = true;
        if (!SetModel(model.entity, next.data()))
            return false;
        if (!ReadModel(model.entity, current))
            return PendingModel(model);
        observed = ObserveModel(model, current);
        if (observed == ModelObservation::Pending)
            return PendingModel(model);
        if (observed == ModelObservation::Superseded) {
            model.original = model.applied = current.original;
            model.originalMask = model.appliedMask = current.originalMask;
            model.changed = false;
            return PendingModel(model);
        }
    }
    model.scene = current.scene;
    if (mask != current.originalMask) {
        model.appliedMask = mask;
        model.changed = true;
        if (!SetMask(current.scene, mask))
            return false;
        std::uint64_t applied{};
        if (!memory.Field(current.scene, abi::ModelState + abi::MeshMask, applied) || applied != mask)
            return PendingModel(model);
    }
    model.applied = {};
    std::copy(path.begin(), path.end(), model.applied.begin());
    model.appliedMask = mask;
    model.pending = false;
    model.retryAt = 0;
    return true;
}
bool WeaponReady(std::uintptr_t entity) noexcept {
    std::uint8_t initialized{}, attributes{};
    std::uintptr_t subclass{}, value{};
    const auto view = entity + abi::AttributeManager + abi::ItemView;
    if (!ConstructedView(view) || !memory.Field(view, abi::Initialized, initialized) || initialized != 1 ||
        !memory.Field(entity, abi::AttributesInitialized, attributes) || attributes != 1 ||
        !memory.Field(entity, abi::Subclass + 8, subclass) || !memory.Read(subclass, value))
        return false;
    for (const auto offset : {std::uintptr_t(0x4A0), std::uintptr_t(0x4B8)}) {
        std::int32_t count{};
        std::uintptr_t data{};
        if (!memory.Field(entity, abi::CompositeOwner + offset, count) || count < 0 || count > 1024 ||
            !memory.Field(entity, abi::CompositeOwner + offset + 8, data))
            return false;
        if (count && (!memory.Read(data, value) || !memory.Read(data + (count - 1) * sizeof(std::uintptr_t), value)))
            return false;
    }
    return true;
}
bool PaintMatches(std::uintptr_t view, int expected) noexcept {
    __try {
        return reinterpret_cast<int (*)(void *)>(runtime.client +
                                                 abi::GetPaintFn.rva)(reinterpret_cast<void *>(view)) == expected;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Fault();
        return false;
    }
}
bool RefreshWeapon(std::uintptr_t entity) noexcept {
    if (!WeaponReady(entity)) {
        runtime.phase = NativeState::WaitingItem;
        return false;
    }
    runtime.materialRefreshes.fetch_add(1, std::memory_order_relaxed);
    // Current engine callsites pass entity+0x608, true. No guessed PostDataUpdate slot.
    return CallOne(abi::InvalidateDescriptionFn, entity + abi::AttributeManager + abi::ItemView) &&
           CallFlag(abi::UpdateCompositeFn, entity + abi::CompositeOwner, true) &&
           CallFlag(abi::UpdateSkinFn, entity, true);
}
bool ReadAttributes(std::uintptr_t view, std::array<AttributeState, 3> &out) noexcept {
    out = {};
    if (!ConstructedView(view))
        return false;
    std::int32_t count{};
    std::uintptr_t data{};
    if (!memory.Field(view, abi::AttributeCount, count) || count < 0 || count > 256 ||
        !memory.Field(view, abi::AttributeData, data) || (count && !data))
        return false;
    for (int i = 0; i < count; ++i) {
        const auto at = data + static_cast<std::size_t>(i) * abi::AttributeStride;
        std::uint16_t id{};
        float value{};
        if (!memory.Field(at, abi::AttributeDef, id))
            return false;
        if (id < 6 || id > 8)
            continue;
        if (out[id - 6].present || !memory.Field(at, abi::AttributeValue, value) || !std::isfinite(value))
            return false;
        out[id - 6] = {true, value};
    }
    std::int32_t afterCount{};
    std::uintptr_t afterData{};
    return memory.Field(view, abi::AttributeCount, afterCount) && afterCount == count &&
           memory.Field(view, abi::AttributeData, afterData) && afterData == data;
}
bool ReadPaint(void *context, PaintAttributes &out) noexcept {
    return ReadAttributes(reinterpret_cast<std::uintptr_t>(context), out);
}
bool WritePaint(void *context, unsigned slot, AttributeState value) noexcept {
    return SetAttribute(reinterpret_cast<std::uintptr_t>(context), slot, value);
}
bool RestoreWeapon(std::uintptr_t list, Weapon &w) noexcept {
    if (!w.used)
        return true;
    if (!SameEntity(list, w.entity, w.handle)) {
        w = {};
        return true;
    }
    if (runtime.viewWeapon == w.handle) {
        if (!RestoreModel(list, runtime.view))
            return false;
        runtime.viewWeapon = 0;
    }
    if (w.changed) {
        if (!WeaponReady(w.entity)) {
            runtime.phase = NativeState::WaitingItem;
            return false;
        }
        if (!RestoreAttributes(reinterpret_cast<void *>(w.entity + abi::AttributeManager + abi::ItemView), w.attributes,
                               w.appliedAttributes, ReadPaint, WritePaint))
            return false;
        ItemPatch(w.entity, false, w.original, w.applied).RestoreMatching(nullptr, CompareWrite);
        std::uint32_t current{};
        if (w.originalSubclass != w.appliedSubclass && memory.Field(w.entity, abi::Subclass, current) &&
            current == w.appliedSubclass) {
            CompareWrite(nullptr, w.entity + abi::Subclass, &current, &w.originalSubclass, sizeof(current));
            CallOne(abi::UpdateViewModelFn, w.entity);
        }
        if (!RestoreModel(list, w.model) || !RefreshWeapon(w.entity))
            return false;
        runtime.restores.fetch_add(1);
    }
    w = {};
    return true;
}
bool RestoreGlove(std::uintptr_t list) noexcept {
    auto &g = runtime.glove;
    if (!g.changed)
        return true;
    if (!SameEntity(list, g.pawn, g.handle)) {
        g = {};
        return true;
    }
    const auto view = g.pawn + abi::Gloves;
    std::array<AttributeState, 3> current;
    if (!ReadAttributes(view, current))
        return false;
    for (unsigned i = 0; i < 3; ++i)
        if (current[i] == g.appliedAttributes[i] && !SetAttribute(view, i, g.attributes[i]))
            return false;
    ItemPatch(g.pawn, true, g.original, g.applied).RestoreMatching(nullptr, CompareWrite);
    std::uint8_t flag{}, yes = 1;
    if (memory.Field(g.pawn, abi::ReapplyGloves, flag))
        CompareWrite(nullptr, g.pawn + abi::ReapplyGloves, &flag, &yes, 1);
    if (!CallOne(abi::InvalidateDescriptionFn, view))
        return false;
    g = {};
    runtime.restores.fetch_add(1);
    return true;
}
void RestoreAll(std::uintptr_t list) noexcept {
    RestoreModel(list, runtime.view);
    runtime.viewWeapon = 0;
    RestoreGlove(list);
    RestoreModel(list, runtime.agent);
    for (auto &w : runtime.weapons)
        RestoreWeapon(list, w);
}
struct Local {
    std::uintptr_t list{}, pawn{}, services{};
    std::uint32_t handle{}, active{}, account{};
    std::uint8_t team{};
};
bool ReadLocalPlayer(Local &out) noexcept {
    std::uintptr_t controller{};
    std::uint64_t steam{};
    std::uint32_t owned{}, controllerHandle{};
    return memory.Read(runtime.client + offsets::EntityList, out.list) &&
           memory.Read(runtime.client + offsets::LocalPawn, out.pawn) &&
           memory.Read(runtime.client + offsets::LocalController, controller) &&
           AssistPawn(memory, out.pawn, out.handle, out.team) && SameEntity(out.list, out.pawn, out.handle) &&
           FullHandle(memory, controller, controllerHandle) && SameEntity(out.list, controller, controllerHandle) &&
           memory.Field(controller, offsets::ControllerPawn, owned) && owned == out.handle &&
           memory.Field(controller, abi::SteamID, steam) && (out.account = static_cast<std::uint32_t>(steam)) &&
           memory.Field(out.pawn, offsets::WeaponServices, out.services) &&
           memory.Field(out.services, offsets::ActiveWeapon, out.active);
}
void UpdateView(const Local &local, const Weapon &weapon, const Definition &definition, bool legacy) noexcept {
    if (local.active != weapon.handle)
        return;
    if (runtime.viewWeapon != weapon.handle) {
        if (!RestoreModel(local.list, runtime.view))
            return;
        runtime.viewWeapon = weapon.handle;
    }
    if (runtime.view.entity && !SameEntity(local.list, runtime.view.entity, runtime.view.handle))
        runtime.view = {};
    if (!runtime.view.entity) {
        std::uint32_t armsHandle{};
        std::uintptr_t arms{}, scene{}, child{};
        if (!memory.Field(local.pawn, abi::HudArms, armsHandle) || !(arms = EntityAt(memory, local.list, armsHandle)) ||
            !SameEntity(local.list, arms, armsHandle) || !memory.Field(arms, offsets::SceneNode, scene) ||
            !memory.Field(scene, abi::Child, child))
            return;
        for (unsigned i = 0; child && i < 32; ++i) {
            std::uintptr_t entity{}, next{};
            std::uint32_t owner{};
            Model candidate;
            if (!memory.Field(child, abi::SceneOwner, entity) || !memory.Field(child, abi::NextSibling, next))
                break;
            if (memory.Field(entity, abi::Owner, owner) && owner == weapon.handle && ReadModel(entity, candidate) &&
                candidate.scene == child && SameEntity(local.list, entity, candidate.handle) &&
                std::string_view(candidate.original.data()).starts_with("weapons/")) {
                runtime.view = candidate;
                break;
            }
            if (next == child)
                break;
            child = next;
        }
    }
    if (runtime.view.entity && SameEntity(local.list, runtime.view.entity, runtime.view.handle)) {
        std::uint32_t owner{};
        if (!memory.Field(runtime.view.entity, abi::Owner, owner) || owner != weapon.handle) {
            RestoreModel(local.list, runtime.view);
            return;
        }
        const auto mask = definition.kind == ItemKind::Knife ? (legacy ? 1ull : 2ull) : (legacy ? 2ull : 1ull);
        ApplyModel(runtime.view,
                   definition.kind == ItemKind::Knife ? definition.model
                                                      : std::string_view(runtime.view.original.data()),
                   mask);
    }
}
void UpdateWeapons(const Local &local, const Options &options, unsigned &budget) noexcept {
    for (auto &w : runtime.weapons)
        w.seen = false;
    std::int32_t count{};
    std::uintptr_t data{};
    if (!memory.Field(local.services, abi::MyWeapons, count) || count < 0 || count > 64 ||
        !memory.Field(local.services, abi::MyWeapons + 8, data) || (count && !data))
        return;
    std::array<std::uint32_t, 64> handles{};
    if (count && !ReadLocal(nullptr, data, handles.data(), count * sizeof(std::uint32_t)))
        return;
    for (int i = 0; i < count; ++i) {
        if (runtime.restoring.load(std::memory_order_acquire))
            return;
        const auto handle = handles[i];
        const auto entity = EntityAt(memory, local.list, handle);
        std::uint32_t owner{};
        if (!SameEntity(local.list, entity, handle) || !memory.Field(entity, abi::Owner, owner) ||
            owner != local.handle)
            continue;
        Weapon *tracked{};
        for (auto &w : runtime.weapons)
            if (w.used && w.handle == handle) {
                tracked = &w;
                break;
            }
        if (tracked)
            tracked->seen = true;
        ItemState current;
        PaintAttributes attributes;
        const auto view = entity + abi::AttributeManager + abi::ItemView;
        if (!ReadItem(entity, false, current) || !WeaponReady(entity) || !ReadAttributes(view, attributes))
            continue;
        const auto original = tracked ? tracked->original.definition : current.definition;
        const auto *definition = runtime.cachedCatalog->Find(original);
        if (!definition)
            continue;
        const bool knife = definition->kind == ItemKind::Knife;
        const Finish *finish = ForWeapon(options, original);
        Finish plain;
        if (knife && options.knife.enabled) {
            definition = runtime.cachedCatalog->Find(options.knife.definition);
            if (!definition || definition->kind != ItemKind::Knife) {
                runtime.phase = NativeState::InvalidSelection;
                continue;
            }
            finish = options.knife.finish.enabled ? &options.knife.finish : &plain;
        }
        if (!finish) {
            if (tracked && budget) {
                --budget;
                RestoreWeapon(local.list, *tracked);
            }
            continue;
        }
        if (!runtime.cachedCatalog->Supports(definition->id, finish->paintKit)) {
            runtime.phase = NativeState::InvalidSelection;
            continue;
        }
        if (!tracked) {
            for (auto &w : runtime.weapons)
                if (!w.used) {
                    tracked = &w;
                    break;
                }
            if (!tracked || !ReadModel(entity, tracked->model) ||
                !memory.Field(entity, abi::Subclass, tracked->originalSubclass))
                continue;
            tracked->appliedSubclass = tracked->originalSubclass;
            tracked->entity = entity;
            tracked->handle = handle;
            tracked->owner = owner;
            tracked->original = current;
            tracked->applied = current;
            tracked->attributes = attributes;
            tracked->appliedAttributes = attributes;
            tracked->used = true;
        }
        tracked->seen = true;
        const auto desired = Desired(tracked->original, *finish, definition->id, local.account);
        const auto desiredAttributes = DesiredAttributes(*finish);
        const auto *paint = runtime.cachedCatalog->Paint(finish->paintKit);
        const bool legacy = paint && paint->legacy;
        const auto now = GetTickCount64();
        const bool newSelection = tracked->material.Select(desired);
        if (newSelection)
            tracked->retryAt = 0;
        auto repair = desired;
        // This is an engine lifecycle flag, not a user preference or a persistent material signature.
        const bool preserveLifecycleFlag = !newSelection && tracked->changed;
        if (preserveLifecycleFlag)
            repair.restoreMaterial = current.restoreMaterial;
        const bool changed = current != repair || attributes != desiredAttributes;
        const bool refreshReady = tracked->material.Ready(now);
        if (tracked->material.pending)
            runtime.phase = tracked->model.pending ? NativeState::LoadingModel : NativeState::WaitingItem;
        if ((changed || refreshReady) && budget && now >= tracked->retryAt &&
            (!tracked->material.pending || refreshReady)) {
            --budget;
            runtime.phase = NativeState::Applying;
            tracked->retryAt = now + 250;
            if (!ItemPatch(entity, false, current, repair).Commit(nullptr, CompareWrite))
                continue;
            const auto ownedRestoreMaterial = tracked->applied.restoreMaterial;
            tracked->applied = repair;
            if (preserveLifecycleFlag)
                tracked->applied.restoreMaterial = ownedRestoreMaterial;
            tracked->changed = true;
            tracked->appliedAttributes = desiredAttributes;
            if (!CommitAttributes(reinterpret_cast<void *>(view), attributes, desiredAttributes, ReadPaint,
                                  WritePaint)) {
                runtime.phase = NativeState::WaitingItem;
                continue;
            }
            if (knife && current.definition != desired.definition) {
                std::uint32_t before{}, after = SubclassToken(desired.definition);
                if (!memory.Field(entity, abi::Subclass, before) ||
                    !CompareWrite(nullptr, entity + abi::Subclass, &before, &after, sizeof(after)))
                    continue;
                tracked->appliedSubclass = after;
                if (!CallOne(abi::UpdateViewModelFn, entity))
                    continue;
            }
            const auto mask = knife ? (legacy ? 1ull : 2ull) : (legacy ? 2ull : 1ull);
            if (!ApplyModel(tracked->model,
                            knife ? definition->model : std::string_view(tracked->model.original.data()), mask))
                continue;
            // Network repair preserves the already rendered material. Only a new selection or a
            // bounded initialization retry may invoke the expensive composite teardown/build.
            if (refreshReady) {
                if (!WeaponReady(entity) || !PaintMatches(view, desired.paint)) {
                    runtime.phase = NativeState::WaitingItem;
                    continue;
                }
                tracked->material.Attempt(now);
                if (!RefreshWeapon(entity))
                    continue;
                tracked->material.Complete();
                runtime.applied.fetch_add(1);
            }
        }
        if (tracked->changed && !tracked->material.pending)
            UpdateView(local, *tracked, *definition, legacy);
    }
    for (auto &w : runtime.weapons)
        if (w.used && !w.seen && budget) {
            --budget;
            RestoreWeapon(local.list, w);
        }
    if (runtime.viewWeapon && runtime.viewWeapon != local.active) {
        RestoreModel(local.list, runtime.view);
        runtime.viewWeapon = 0;
    }
}
void UpdateGlove(const Local &local, const Options &options, unsigned &budget) noexcept {
    if (runtime.restoring.load(std::memory_order_acquire))
        return;
    auto &g = runtime.glove;
    if (g.changed && (g.pawn != local.pawn || g.handle != local.handle))
        RestoreGlove(local.list);
    if (!options.glove.enabled) {
        if (g.changed && budget) {
            --budget;
            RestoreGlove(local.list);
        }
        return;
    }
    const auto *definition = runtime.cachedCatalog->Find(options.glove.definition);
    Finish plain;
    const auto &finish = options.glove.finish.enabled ? options.glove.finish : plain;
    if (!definition || definition->kind != ItemKind::Glove ||
        !runtime.cachedCatalog->Supports(definition->id, finish.paintKit)) {
        runtime.phase = NativeState::InvalidSelection;
        return;
    }
    ItemState current;
    std::array<AttributeState, 3> attributes;
    if (!ReadItem(local.pawn, true, current) || !ReadAttributes(local.pawn + abi::Gloves, attributes))
        return;
    const auto desired = Desired(g.changed ? g.original : current, finish, definition->id, local.account);
    const std::array<AttributeState, 3> desiredAttributes{
        {{true, static_cast<float>(finish.paintKit)}, {true, static_cast<float>(finish.seed)}, {true, finish.wear}}};
    auto currentComparable = current;
    currentComparable.paint = desired.paint;
    currentComparable.seed = desired.seed;
    currentComparable.wear = desired.wear;
    currentComparable.statTrak = desired.statTrak;
    if (currentComparable == desired && attributes == desiredAttributes)
        return;
    if (!budget)
        return;
    --budget;
    if (!g.changed) {
        g.pawn = local.pawn;
        g.handle = local.handle;
        g.original = current;
        g.attributes = attributes;
    }
    if (!ItemPatch(local.pawn, true, current, desired).Commit(nullptr, CompareWrite))
        return;
    g.applied = desired;
    g.changed = true;
    g.appliedAttributes = desiredAttributes;
    for (unsigned i = 0; i < 3; ++i)
        if (attributes[i] != desiredAttributes[i] && !SetAttribute(local.pawn + abi::Gloves, i, desiredAttributes[i]))
            return;
    std::uint8_t flag{}, yes = 1;
    if (memory.Field(local.pawn, abi::ReapplyGloves, flag))
        CompareWrite(nullptr, local.pawn + abi::ReapplyGloves, &flag, &yes, 1);
    if (CallOne(abi::InvalidateDescriptionFn, local.pawn + abi::Gloves))
        runtime.applied.fetch_add(1);
}
void UpdateAgent(const Local &local, const Options &options, unsigned &budget) noexcept {
    if (runtime.restoring.load(std::memory_order_acquire))
        return;
    auto &agent = runtime.agent;
    if (agent.entity && (agent.entity != local.pawn || agent.handle != local.handle))
        RestoreModel(local.list, agent);
    const auto selected = options.agents[local.team == 3 ? 1 : 0];
    if (!selected) {
        if (agent.entity && budget) {
            --budget;
            RestoreModel(local.list, agent);
        }
        return;
    }
    const auto *definition = runtime.cachedCatalog->Find(selected);
    if (!definition || definition->kind != ItemKind::Agent || (definition->team && definition->team != local.team)) {
        runtime.phase = NativeState::InvalidSelection;
        return;
    }
    Model current;
    if (!ReadModel(local.pawn, current)) {
        if (agent.pending)
            runtime.phase = NativeState::LoadingModel;
        return;
    }
    if (!agent.entity)
        agent = current;
    if (definition->model == std::string_view(current.original.data())) {
        const bool wasPending = agent.pending;
        const auto observed = ObserveModel(agent, current);
        if (observed == ModelObservation::Pending) {
            PendingModel(agent);
            return;
        }
        if (observed == ModelObservation::Superseded)
            agent = current;
        if (wasPending) {
            agent.pending = false;
            agent.retryAt = 0;
            runtime.applied.fetch_add(1);
        }
        return;
    }
    // Preserve a new server-provided original (spawn/team change), not the old team's model.
    if (current.original != agent.applied && !agent.pending)
        agent = current;
    if (budget) {
        --budget;
        if (ApplyModel(agent, definition->model, agent.originalMask))
            runtime.applied.fetch_add(1);
    }
}
} // namespace
const char *Name(NativeState state) noexcept {
    switch (state) {
    case NativeState::Disabled:
        return "Off";
    case NativeState::UnsupportedBuild:
        return "Unsupported game build";
    case NativeState::LoadingCatalog:
        return "Reading game catalog";
    case NativeState::CatalogError:
        return "Catalog unavailable";
    case NativeState::WaitingPlayer:
        return "Waiting for player";
    case NativeState::WaitingItem:
        return "Waiting for item initialization";
    case NativeState::LoadingModel:
        return "Waiting for model resource";
    case NativeState::Ready:
        return "Ready";
    case NativeState::Applying:
        return "Applying appearance";
    case NativeState::InvalidSelection:
        return "Unsupported item / finish";
    case NativeState::NativeFault:
        return "Native call failed";
    case NativeState::Restoring:
        return "Restoring originals";
    }
    return "Unavailable";
}
bool Initialize(std::uintptr_t client) noexcept {
    if (runtime.ready)
        return true;
    runtime.client = client;
    runtime.restoring = false;
    if (!ValidImage(client)) {
        runtime.phase = NativeState::UnsupportedBuild;
        runtime.client = 0;
        return false;
    }
    try {
        std::array<wchar_t, 32768> path{};
        const auto count =
            GetModuleFileNameW(reinterpret_cast<HMODULE>(client), path.data(), static_cast<DWORD>(path.size()));
        if (!count || count >= path.size())
            return false;
        const auto directory = std::filesystem::path(path.data()).parent_path().parent_path().parent_path();
        if (!runtime.catalog.Start(directory))
            return false;
        runtime.ready = true;
        runtime.phase = NativeState::LoadingCatalog;
        OverlayLog("Cosmetics: verified build14181 adapter; local catalog discovery started.");
        return true;
    } catch (...) {
        runtime.phase = NativeState::CatalogError;
        return false;
    }
}
void Configure(const Options &options) noexcept {
    if (!Valid(options))
        return;
    std::lock_guard lock(runtime.optionsMutex);
    runtime.options = options;
}
void Tick(int stage) noexcept {
    if (stage != 7 || !runtime.ready)
        return;
    runtime.inFlight.fetch_add(1, std::memory_order_acq_rel);
    struct Guard {
        ~Guard() { runtime.inFlight.fetch_sub(1, std::memory_order_acq_rel); }
    } guard;
    if (!runtime.ready)
        return;
    runtime.gameThread = GetCurrentThreadId();
    try {
        Options options;
        {
            std::unique_lock lock(runtime.optionsMutex, std::try_to_lock);
            if (!lock.owns_lock())
                return;
            options = runtime.options;
        }
        std::uintptr_t list{};
        memory.Read(runtime.client + cs2::offsets::EntityList, list);
        if (runtime.restoring || !options.enabled) {
            runtime.phase = runtime.restoring ? NativeState::Restoring : NativeState::Disabled;
            RestoreAll(list);
        } else {
            if (!runtime.cachedCatalog)
                runtime.cachedCatalog = runtime.catalog.Snapshot();
            if (!runtime.cachedCatalog) {
                runtime.phase = runtime.catalog.Busy() ? NativeState::LoadingCatalog : NativeState::CatalogError;
                return;
            }
            Local local;
            if (!ReadLocalPlayer(local)) {
                RestoreAll(list);
                runtime.phase = NativeState::WaitingPlayer;
            } else {
                runtime.phase = NativeState::Ready;
                unsigned budget = 2;
                UpdateWeapons(local, options, budget);
                UpdateGlove(local, options, budget);
                UpdateAgent(local, options, budget);
            }
        }
        unsigned tracked = runtime.glove.changed ? 1 : 0;
        tracked += runtime.agent.changed ? 1 : 0;
        tracked += runtime.view.changed ? 1 : 0;
        for (const auto &w : runtime.weapons)
            tracked += w.used ? 1 : 0;
        runtime.tracked = tracked;
    } catch (...) {
        Fault();
    }
}
void Restore() noexcept {
    runtime.restoring = true;
}
HRESULT StopNative() noexcept {
    runtime.restoring.store(true, std::memory_order_release);
    if (!runtime.ready)
        return S_OK;
    if (!runtime.inFlight.load(std::memory_order_acquire) && runtime.gameThread.load() == GetCurrentThreadId())
        Tick(7);
    if (runtime.inFlight.load(std::memory_order_acquire) || runtime.tracked.load(std::memory_order_acquire))
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    runtime.ready.store(false, std::memory_order_release);
    return S_OK;
}
void Shutdown() noexcept {
    runtime.ready = false;
    runtime.catalog.Stop();
    runtime.cachedCatalog.reset();
    runtime.client = 0;
    // Owner drains Restore while dispatch remains active. Never call game APIs here.
    for (auto &weapon : runtime.weapons)
        weapon = {};
    runtime.glove = {};
    runtime.agent = {};
    runtime.view = {};
    runtime.viewWeapon = 0;
    runtime.tracked = 0;
}
NativeStatus Status() noexcept {
    NativeStatus s;
    s.state = runtime.phase.load();
    s.weapons = s.knives = s.gloves = s.agents = runtime.ready.load();
    s.tracked = runtime.tracked.load();
    s.applied = runtime.applied.load();
    s.restores = runtime.restores.load();
    s.faults = runtime.faults.load();
    s.materialRefreshes = runtime.materialRefreshes.load();
    return s;
}
CatalogController &CatalogRuntime() noexcept {
    return runtime.catalog;
}
} // namespace awareness::cosmetics
