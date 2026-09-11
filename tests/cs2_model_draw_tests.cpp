#include "cs2_model_draw.hpp"
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
    effects.visibility = EffectVisibility::TwoColor;
    effects.materialColor = {1, 1, 1, .5f};
    effects.glowColor = {1, 0, 1, .75f};
    auto faded = target;
    faded.opacity = .5f;
    std::vector<cs2::model::Packet> layers;
    cs2::model::DrawLayers(packet, faded, effects, 0x100ull, 0x200ull, [&](auto p) { layers.push_back(p); });
    check(layers.size() == 3, "hidden, original, visible are submitted");
    check(layers[0].Get<std::uintptr_t>(0x20) == 0x200 && layers[2].Get<std::uintptr_t>(0x20) == 0x100,
          "depth-tested and depth-disabled materials are distinct");
    check(layers[0].Get<std::uint32_t>(0x50) == 0x60ff00ff && layers[2].Get<std::uint32_t>(0x50) == 0x40ffffff,
          "both independent color alpha values include distance opacity");
    check(layers[1].bytes == packet.bytes, "original draw packet remains byte-identical");
    bool preserved = true;
    for (auto i = 0u; i < packet.bytes.size(); ++i)
        if (!(i >= 0x20 && i < 0x28) && !(i >= 0x50 && i < 0x54))
            preserved &= layers[0].bytes[i] == packet.bytes[i] && layers[2].bytes[i] == packet.bytes[i];
    check(preserved, "animation pointers, sort key at 0x28, flags and bounds are preserved");
    layers.clear();
    effects.visibility = EffectVisibility::OccludedOnly;
    cs2::model::DrawLayers(packet, target, effects, 0x100ull, 0x200ull, [&](auto p) { layers.push_back(p); });
    check(layers.size() == 2 && layers[1].bytes == packet.bytes, "behind-walls mode preserves normal visible material");
    effects.visibility = EffectVisibility::TwoColor;
    std::vector<cs2::model::DrawItem> batch(300, {packet, 1.f});
    cs2::model::DrawBuffer buffer;
    for (unsigned i = 0; i < batch.size(); ++i)
        buffer.Push(packet, static_cast<float>(i));
    check(buffer.Items().size() == 300 && buffer.Items()[0].opacity == 0 && buffer.Items()[299].opacity == 299,
          "large callbacks spill safely beyond stack storage and preserve order");
    std::vector<unsigned> calls;
    unsigned originals{}, hiddenCount{}, visibleCount{};
    cs2::model::DrawBatch(
        batch, effects, 0x100, 0x200,
        [&](std::span<const cs2::model::Packet> group) {
            check(group.size() <= 128, "tinted draw batch is bounded");
            const bool hidden = group[0].Get<std::uintptr_t>(offsets::PacketMaterial) == 0x200;
            check(hidden ? originals == 0 : originals == 1,
                  "all hidden pieces precede original batch and visible pieces");
            (hidden ? hiddenCount : visibleCount) += static_cast<unsigned>(group.size());
            calls.push_back(static_cast<unsigned>(group.size()));
        },
        [&] { ++originals; });
    check(originals == 1 && calls.size() == 6 && hiddenCount == 300 && visibleCount == 300,
          "300 mesh pieces need seven callbacks instead of nine hundred");
    check(batch.front().packet.bytes == packet.bytes && batch.back().packet.bytes == packet.bytes,
          "batch path preserves original packets");
    cs2::model::Selection selection(targets, effects);
    check(selection.Find(target.scene) && !selection.Find(0xdeadbeef), "sorted scene lookup selects exact ownership");
    effects.glowColor.a = 0;
    effects.materialColor.a = 0;
    unsigned tintCalls{};
    originals = 0;
    cs2::model::DrawBatch(batch, effects, 0x100, 0x200, [&](auto) { ++tintCalls; }, [&] { ++originals; });
    check(!tintCalls && originals == 1, "zero-opacity overlays retain one original batch");
    effects.materialColor = {.8f, .5f, .2f, .5f};
    packet.Set(offsets::PacketColor, std::array<std::uint8_t, 4>{100, 120, 140, 255});
    auto shaded = cs2::model::VisiblePacket(packet, 1, effects, 0x123, true);
    check(shaded.Get<std::uintptr_t>(offsets::PacketMaterial) == packet.Get<std::uintptr_t>(offsets::PacketMaterial),
          "shaded fill preserves current model material");
    check(shaded.Get<std::array<std::uint8_t, 4>>(offsets::PacketColor)[3] == 255,
          "shaded pass keeps original alpha so hidden layer does not bleed through");
    check(cs2::model::CanCompose(batch, effects, true) && !cs2::model::CanCompose(batch, effects, false),
          "shaded pass avoids an extra full visible draw, transparent solid keeps correct layering");
    effects.materialColor.a = 1;
    check(cs2::model::CanCompose(batch, effects, false), "opaque solid can replace original selected packets");
    VirtualFree(base, 0, MEM_RELEASE);
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
