#include "cs2_model_draw.hpp"
#include "native_draw_annotations.hpp"
#include <cstdio>
#include <vector>
using namespace awareness;
using namespace awareness::cs2;
int main() {
    unsigned failures{}, checks{};
    const auto check = [&](bool ok, const char *message) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("FAIL %s\n", message);
        }
    };
    check(model::ColorPass(0xbd79f698), "captured CS2 main pass accepted");
    check(model::PassToken("CSGOFORWARD") == model::PassToken("CsgoForward"), "engine token is case insensitive");
    check(!model::ColorPass(model::PassToken("Depth")) &&
              !model::ColorPass(model::PassToken("FirstpersonLegsPrepass")) &&
              !model::ColorPass(model::PassToken("Forward")),
          "depth, first-person and generic forward passes stay unchanged");
    auto *base = static_cast<unsigned char *>(VirtualAlloc(nullptr, 0x80000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!base)
        return 2;
    const auto address = [&](std::size_t offset) { return reinterpret_cast<std::uintptr_t>(base) + offset; };
    const auto put = []<class T>(std::uintptr_t where, T value) {
        std::memcpy(reinterpret_cast<void *>(where), &value, sizeof(value));
    };
    const auto list = address(0x1000), chunk = address(0x2000);
    put(list + offsets::EntityTable, chunk);
    FrameSnapshot frame;
    frame.localTeam = 2;
    frame.localEntityId = 2;
    frame.entityCount = 3;
    for (unsigned i = 0; i < 3; ++i) {
        const auto pawn = address(0x20000 + i * 0x4000), identity = chunk + offsets::EntityStride * (i + 2);
        const auto component = address(0x40000 + i * 0x1000), array = component + 0x100, updater = component + 0x200;
        const auto scene = component + 0x300;
        put(identity, pawn);
        put(identity + 0x10, std::uint32_t(0x18000 + i + 2));
        put(pawn + offsets::Identity, identity);
        put(pawn + offsets::Health, std::int32_t(100));
        put(pawn + offsets::Team, std::uint8_t(i == 2 ? 3 : 2));
        put(pawn + offsets::RenderComponent, component);
        put(component + offsets::SceneUpdaterCount, std::int32_t(2));
        put(component + offsets::SceneUpdaterArray, array);
        put(array, updater);
        put(array + 8, updater); // Duplicates must not double-draw.
        put(updater + offsets::UpdaterSceneObject, scene);
        auto &entity = frame.entities[i];
        entity.id = i + 2;
        entity.valid = 1;
        entity.health = 100;
        entity.team = i == 2 ? 3 : 2;
        entity.origin = {5, 0, 0};
        // An unrelated value in the old owner location must never affect matching.
        put(scene + 0xc0, std::uint32_t(0xdeadbeef));
    }
    LocalMemory local;
    Memory memory{&local, LocalMemory::Read};
    Configuration config;
    config.opacity = 1;
    auto targets = model::CollectTargets(memory, list, frame, config);
    check(targets.count == 2, "all-teams excludes local pawn and deduplicates scene entries");
    config.teamFilter = TeamFilter::OpponentsOnly;
    targets = model::CollectTargets(memory, list, frame, config);
    check(targets.count == 1 && targets.entries[0].owner.handle == 0x18004, "opponents retain full current handle");
    const auto target = targets.entries[0];
    check(model::StillOwned(memory, target), "current render component owns the scene");
    put(target.owner.identity + 0x10, std::uint32_t(0x28004));
    check(!model::StillOwned(memory, target), "recycled identity generation is rejected");
    put(target.owner.identity + 0x10, std::uint32_t(0x18004));
    put(target.component + 0x200 + offsets::UpdaterSceneObject, address(0x70000));
    check(!model::StillOwned(memory, target), "replaced scene updater is rejected");
    put(target.component + 0x200 + offsets::UpdaterSceneObject, target.scene);
    put(target.owner.pawn + offsets::Health, std::int32_t(0));
    check(!model::StillOwned(memory, target), "death between sample and draw is rejected");
    put(target.owner.pawn + offsets::Health, std::int32_t(100));
    put(target.component + offsets::SceneUpdaterCount, std::int32_t(100000));
    check(model::CollectTargets(memory, list, frame, config).count == 0, "invalid component counts are bounded");
    put(target.component + offsets::SceneUpdaterCount, std::int32_t(2));
    frame.entities[2].dormant = 1;
    check(model::CollectTargets(memory, list, frame, config).count == 0, "dormant players do not enter draw set");
    frame.entities[2].dormant = 0;
    config.enabled = 0;
    check(model::CollectTargets(memory, list, frame, config).count == 0, "master toggle clears selection");
    model::Packet packet;
    for (std::size_t i = 0; i < packet.bytes.size(); ++i)
        packet.bytes[i] = static_cast<std::byte>(i);
    const auto mesh = address(0x60000), handle = mesh + 0x100, model = mesh + 0x200, name = mesh + 0x300;
    packet.Set(0, mesh);
    put(mesh + 8, handle);
    put(handle, model);
    put(model + 8, name);
    std::memcpy(reinterpret_cast<void *>(name), "agents/models/ctm_sas/ctm_sas.vmdl", 33);
    check(cs2::model::PlayerModel(memory, packet), "actual player model prefix recognized");
    std::memcpy(reinterpret_cast<void *>(name), "weapons/models/knife/test.vmdl", 30);
    check(!cs2::model::PlayerModel(memory, packet), "weapon model rejected even when component is eligible");
    EffectsConfiguration effects;
    effects.materialEnabled = 0;
    effects.glowEnabled = 1;
    check(!cs2::model::HasVisibleTint(effects), "glow alone never enables a native material replacement");
    effects.materialEnabled = 1;
    effects.visibility = EffectVisibility::TwoColor;
    effects.materialColor = {1, 1, 1, .5f};
    effects.glowColor = {1, 0, 1, .75f};
    std::vector<cs2::model::DrawItem> selected(300, {packet, .5f});
    std::array<cs2::model::Packet, 128> layerScratch;
    unsigned callbacks{}, emitted{};
    bool preserved = true;
    const auto rendered =
        cs2::model::DrawVisible(selected, effects, 0x100, layerScratch, [&](std::span<const cs2::model::Packet> group) {
            ++callbacks;
            emitted += static_cast<unsigned>(group.size());
            check(group.size() <= 128, "visible layer chunks have a bounded packet count");
            for (const auto &copy : group) {
                check(copy.Get<std::uintptr_t>(offsets::PacketMaterial) == 0x100,
                      "all emitted packets use only the verified depth-tested material");
                check(copy.Get<std::uint32_t>(offsets::PacketColor) == 0x40ffffff,
                      "visible alpha includes distance opacity");
                for (auto i = 0u; i < copy.bytes.size(); ++i)
                    if (!(i >= 0x20 && i < 0x28) && !(i >= 0x50 && i < 0x54))
                        preserved &= copy.bytes[i] == packet.bytes[i];
            }
        });
    check(rendered == 300 && emitted == 300 && callbacks == 3,
          "300 transparent pieces require three bounded visible passes");
    check(preserved, "animation pointers, pass material at 0x28, flags and bounds remain byte-identical");
    check(selected.front().packet.bytes == packet.bytes && selected.back().packet.bytes == packet.bytes,
          "transparent draw preparation cannot modify source packets");
    effects.visibility = EffectVisibility::OccludedOnly;
    check(cs2::model::DrawVisible(selected, effects, 0x100, layerScratch, [&](auto) { ++callbacks; }) == 0 &&
              callbacks == 3,
          "unverified hidden-only rendering submits no Z-disabled fallback");
    effects.visibility = EffectVisibility::TwoColor;
    effects.materialColor.a = 0;
    check(cs2::model::DrawVisible(selected, effects, 0x100, layerScratch, [&](auto) { ++callbacks; }) == 0 &&
              callbacks == 3,
          "zero-opacity visible layer creates no extra draw calls");
    effects.materialColor.a = .5f;
    selected[0].opacity = 0;
    selected[1].opacity = std::numeric_limits<float>::quiet_NaN();
    check(cs2::model::DrawVisible(selected, effects, 0x100, layerScratch, [](auto) {}) == 298,
          "invalid and fully faded selections are skipped");
    check(cs2::model::DrawVisible(selected, effects, 0, layerScratch, [](auto) {}) == 0,
          "a missing cached material cannot replace the engine material");
    selected[0].opacity = selected[1].opacity = .5f;
    cs2::model::Selection selection(targets, effects);
    check(selection.Find(target.scene) && !selection.Find(0xdeadbeef), "sorted scene lookup selects exact ownership");
    effects.materialColor = {.8f, .5f, .2f, .5f};
    packet.Set(offsets::PacketColor, std::array<std::uint8_t, 4>{100, 120, 140, 255});
    auto shaded = cs2::model::VisiblePacket(packet, 1, effects, 0x123, true);
    check(shaded.Get<std::uintptr_t>(offsets::PacketMaterial) == packet.Get<std::uintptr_t>(offsets::PacketMaterial),
          "shaded fill preserves the current model material");
    check(shaded.Get<std::array<std::uint8_t, 4>>(offsets::PacketColor)[3] == 255,
          "shaded pass keeps the original alpha");
    check(cs2::model::CanCompose(selected, effects, true) && !cs2::model::CanCompose(selected, effects, false),
          "shaded pass replaces selected packets once, while translucent solid overlays the original");
    effects.materialColor.a = 1;
    for (auto &item : selected)
        item.opacity = 1;
    check(cs2::model::CanCompose(selected, effects, false), "opaque solid can replace selected original packets");
    std::vector<cs2::model::Packet> originals(300, packet);
    for (unsigned i = 0; i < originals.size(); ++i)
        originals[i].Set(0x28, std::uint64_t(i + 0xabcdef00));
    auto composed = originals;
    std::array<cs2::model::DrawItem, 3> edits{
        {{originals[3], 1, 3}, {originals[147], 1, 147}, {originals[299], 1, 299}}};
    check(cs2::model::ComposeVisible(composed, edits, effects, 0x123, false), "compose a mixed complete engine batch");
    bool exactOrder = composed.size() == originals.size(), untouched = true;
    for (unsigned i = 0; i < originals.size(); ++i) {
        exactOrder &= composed[i].Get<std::uint64_t>(0x28) == originals[i].Get<std::uint64_t>(0x28);
        if (i != 3 && i != 147 && i != 299)
            untouched &= composed[i].bytes == originals[i].bytes;
    }
    check(exactOrder && untouched, "composition preserves total primitive count, order and unrelated packets");
    check(composed[147].Get<std::uintptr_t>(offsets::PacketMaterial) == 0x123,
          "composition edits exactly the selected primitive");
    composed = originals;
    edits.back().sourceIndex = 300;
    check(!cs2::model::ComposeVisible(composed, edits, effects, 0x123, false),
          "invalid source indices reject the complete composition transaction");
    untouched = true;
    for (unsigned i = 0; i < originals.size(); ++i)
        untouched &= composed[i].bytes == originals[i].bytes;
    check(untouched, "failed validation cannot leave partially tinted packets");
    {
        cs2::model::ScratchPool<2> pool;
        const cs2::model::Packet *cachedBatch{};
        const cs2::model::DrawItem *cachedItems{};
        {
            auto first = pool.Acquire();
            auto second = pool.Acquire();
            check(first && second && !pool.Acquire(),
                  "concurrent callbacks own separate bounded slots without waiting");
            auto firstBatch = first->Batch(300);
            auto secondBatch = second->Batch(300);
            check(firstBatch.data() != secondBatch.data(),
                  "concurrent callbacks cannot overwrite each other's packets");
            cachedBatch = firstBatch.data();
            for (unsigned i = 0; i < 300; ++i)
                check(first->selected.Push(packet, 1, i), "large selection fits its reusable scratch buffer");
            cachedItems = first->selected.Items().data();
            check(first->Batch(cs2::model::MaxDrawPackets + 1).empty(),
                  "oversized native batches are rejected before allocation");
        }
        {
            auto reused = pool.Acquire();
            check(reused && reused->selected.Items().empty(), "released slot resets its live selection count");
            check(reused->Batch(256).data() == cachedBatch && reused->Batch(300).data() == cachedBatch,
                  "changing batch sizes within capacity reuses the allocation");
            for (unsigned i = 0; i < 300; ++i)
                reused->selected.Push(packet, 1, i);
            check(reused->selected.Items().data() == cachedItems && reused->selected.Items().back().sourceIndex == 299,
                  "large selections reuse allocation and preserve source indices after a clear");
            for (unsigned i = 300; i < cs2::model::MaxDrawPackets; ++i)
                reused->selected.Push(packet, 1, i);
            check(!reused->selected.Push(packet, 1) && reused->selected.Items().size() == cs2::model::MaxDrawPackets,
                  "selection scratch enforces the same bound as the native hook");
        }
        pool.Clear();
        check(bool(pool.Acquire()), "scratch storage can be released after callbacks drain and reacquired");
    }
    {
        std::array<cs2::model::Packet, 6> fixed, oldOverflow, newOverflow;
        for (auto *storage : {&fixed, &oldOverflow, &newOverflow})
            for (auto &p : *storage)
                p = packet;
        const auto ptr = [](auto &storage) { return reinterpret_cast<std::uintptr_t>(storage.data()); };
        cs2::model::PrimitiveBuffer before{ptr(fixed), 6, 4, 0, 0, 0, 0, 0};
        auto after = before;
        after.count = 6;
        after.overflow = ptr(newOverflow);
        after.overflowCapacity = 6;
        after.overflowCount = 3;
        const auto header = after;
        unsigned visits{};
        const auto update = [&](std::uintptr_t address) {
            cs2::model::Packet p;
            check(memory.Read(address, p), "generated primitive can be read");
            p.Set(offsets::PacketMaterial, std::uintptr_t(0x123));
            p.Set(0x28, std::uintptr_t(0x456));
            p.Set(offsets::PacketColor, std::uint32_t(0xffaa55bb));
            check(cs2::model::WritePrimitive(address, p), "owned generated primitive can be updated");
            ++visits;
        };
        check(cs2::model::ForEachAppended(before, after, update) && visits == 5,
              "one generator visits only new fixed and overflow entries");
        check(fixed[3].bytes == packet.bytes && newOverflow[3].bytes == packet.bytes,
              "preexisting and unused primitives remain byte-identical");
        check(!std::memcmp(&header, &after, sizeof(header)), "generation edits never change buffer counts or pointers");
        check(fixed[4].Get<std::uintptr_t>(0x28) == 0x456,
              "the pass-resolved material accompanies the base material before instance upload");
        before = after;
        before.overflow = ptr(oldOverflow);
        after.overflowCount = 5;
        visits = 0;
        check(cs2::model::ForEachAppended(before, after, update) && visits == 2,
              "overflow reallocation uses the new storage and retains independent counts");
        check(oldOverflow[3].bytes == packet.bytes, "retired overflow storage is never modified");
        after.count = 1;
        visits = 0;
        check(!cs2::model::ForEachAppended(before, after, update) && !visits,
              "shrinking or recycled headers fail before any write");
        after = before;
        after.overflow = ptr(newOverflow);
        after.overflowCount = after.overflowCapacity = 1 + cs2::model::MaxGeneratedPrimitives;
        check(!cs2::model::ForEachAppended(before, after, update), "unbounded engine counts are rejected");
        before = {0x10000, 20000, 19000, 0, 0, 0, 0, 0};
        after = before;
        after.count += 2;
        unsigned sparseVisits{};
        check(cs2::model::ForEachAppended(before, after, [&](auto) { ++sparseVisits; }) && sparseVisits == 2,
              "large existing scene buffers do not suppress a bounded player append");
        after.fixed = before.fixed = (std::numeric_limits<std::uintptr_t>::max)() - 8;
        check(!cs2::model::ForEachAppended(before, after, [](auto) {}),
              "wrapped native packet ranges are rejected before traversal");
        check(!cs2::model::WritePrimitive(0, packet), "unmapped primitive write is contained without a host exception");
    }
    {
        native_mask::Annotations<16> tags;
        const std::array<unsigned, 5> args{12, 1, 4, 0, 3};
        auto changed = args;
        changed[0]++;
        check(tags.Put(0x1234, args, 0xff554433), "command tag accepts bounded numeric identity");
        check(tags.Take(0x1234, args) == 0xff554433 && !tags.Take(0x1234, args),
              "a command tag is consumed exactly once");
        tags.Put(0x1234, args, 0xff554433);
        check(!tags.Take(0x1234, changed) && !tags.Take(0x1234, args),
              "reused command with changed arguments invalidates old selection");
        tags.Put(0x1234, args, 0xff554433);
        tags.Advance();
        check(!tags.Take(0x1234, args), "prior-frame command identity never survives generation advance");
        tags.Put(0x1234, args, 0xff554433);
        tags.Put(0x1234, changed, 0xff112233);
        check(tags.Take(0x1234, changed) == 0xff112233, "re-recorded pointer replaces the command tag");
        check(!tags.Put(0, args, 0xff112233) && !tags.Put(0x111, args, 0x112233),
              "empty command and transparent tags rejected");
    }
    VirtualFree(base, 0, MEM_RELEASE);
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
