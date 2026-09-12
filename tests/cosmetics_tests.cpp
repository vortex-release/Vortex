#include "cosmetics_options.hpp"
#include "cosmetics_state.hpp"
#include "cosmetics_native.hpp"
#include "economy_catalog.hpp"
#include <iostream>
#include <limits>
#include <string>

void OverlayLog(const char *) noexcept {}
namespace {
unsigned passed{}, failed{};
void Check(bool value, const char *name) {
    if (value)
        ++passed;
    else {
        ++failed;
        std::cerr << "FAIL: " << name << '\n';
    }
}
struct Memory {
    std::array<unsigned char, 64> bytes{};
    std::uintptr_t failAddress{};
    static bool Write(void *context, std::uintptr_t at, const void *before, const void *after,
                      std::size_t size) noexcept {
        auto &m = *static_cast<Memory *>(context);
        if (at == m.failAddress || at + size > m.bytes.size() || std::memcmp(m.bytes.data() + at, before, size))
            return false;
        std::memcpy(m.bytes.data() + at, after, size);
        return true;
    }
};
constexpr std::string_view Items = R"KV(
"items_game" {
 "seasonaloperations" { "quest_reward" { "[0]" { "item_name" "test" } "[*]" { "none" "none" } } }
 "prefabs" {
  "rifle" { "item_class" "weapon_ak47" "model_player" "weapons/models/ak47/weapon_rif_ak47.vmdl" "item_name" "#AK" }
  "knife" { "item_class" "weapon_knife" "model_player" "weapons/models/knife/karambit.vmdl" }
  "hands" { "flexible_loadout_category" "hands" "item_class" "wearable_item" "model_player" "agents/models/gloves.vmdl" }
  "player" { "item_class" "customplayer" "model_player" "characters/models/test.vmdl" "used_by_classes" { "terrorists" "1" } }
 }
 "items" {
  "7" { "name" "weapon_ak47" "prefab" "rifle" }
  "507" { "name" "weapon_knife_karambit" "prefab" "knife" }
  "5030" { "name" "sporty_gloves" "prefab" "hands" }
  "5000" { "name" "customplayer_test" "prefab" "player" }
  "9999" { "name" "not_a_weapon" "item_class" "collectible_item" }
 }
 "paint_kits" {
  "0" { "name" "default" }
  "44" { "name" "test_paint" "description_tag" "#FINISH" "use_legacy_model" "1" "wear_remap_min" "0.02" "wear_remap_max" "0.8" }
  "55" { "name" "other_paint" }
 }
 "item_sets" { "test" { "items" { "[test_paint]weapon_ak47" "1" "[test_paint]weapon_knife_karambit" "1" "[other_paint]sporty_gloves" "1" } } }
}
)KV";
} // namespace
int main(int argc, char **argv) {
    using namespace awareness::cosmetics;
    Options options;
    Check(Valid(options), "safe defaults valid");
    Check(!options.enabled, "default inactive");
    Check(!ForWeapon(options, 7), "disabled weapon lookup");
    options.enabled = 1;
    options.weapons[4].enabled = 1;
    Check(ForWeapon(options, 7) != nullptr, "definition keyed weapon selection");
    Check(!ForWeapon(options, 9), "other weapon remains independent");
    auto invalid = options;
    invalid.weapons[0].wear = std::numeric_limits<float>::quiet_NaN();
    Check(!Valid(invalid), "reject NaN wear");
    invalid = options;
    invalid.weapons[0].seed = 1001;
    Check(!Valid(invalid), "reject out of range seed");
    invalid = options;
    invalid.weapons[0].name.fill('a');
    Check(!Valid(invalid), "reject unterminated custom name");
    invalid = options;
    invalid.weapons[0].name[0] = '\n';
    Check(!Valid(invalid), "reject multiline name");
    invalid = options;
    invalid.weapons[0].name[0] = '\xc0';
    invalid.weapons[0].name[1] = '\xaf';
    Check(!Valid(invalid), "reject overlong UTF8");
    invalid = options;
    invalid.weapons[0].name[0] = '\xf4';
    invalid.weapons[0].name[1] = '\x90';
    invalid.weapons[0].name[2] = '\x80';
    invalid.weapons[0].name[3] = '\x80';
    Check(!Valid(invalid), "reject out of Unicode range");
    invalid = options;
    std::memcpy(invalid.weapons[0].name.data(), "caf\xc3\xa9", 6);
    Check(Valid(invalid), "accept valid UTF8 name");
    Check(SubclassToken(507) != SubclassToken(508) && SubclassToken(507) == SubclassToken(507),
          "stable definition subclass tokens");
    ItemState original;
    original.definition = 7;
    original.itemId = 12345;
    original.high = 123;
    original.low = 456;
    original.account = 72;
    original.initialized = 1;
    original.paint = 44;
    original.seed = 8;
    original.wear = .75f;
    original.statTrak = 182;
    auto finish = options.weapons[4];
    finish.paintKit = 55;
    finish.seed = 23;
    finish.wear = .03f;
    finish.statTrak = 1;
    finish.kills = 12;
    std::memcpy(finish.name.data(), "Vortex", 7);
    const auto desired = Desired(original, finish, 7, 42);
    Check(original.itemId == 12345 && original.paint == 44, "desired appearance preserves source snapshot");
    Check(desired.paint == 55 && desired.seed == 23 && desired.wear == .03f && desired.statTrak == 12,
          "all finish controls participate");
    Check(desired.account == 42 && desired.high == 0xffffffffu && desired.initialized == 1 && desired.disallow == 1,
          "local fallback identity");
    Check(std::string(desired.name.data()) == "Vortex" && desired.name.back() == 0, "bounded custom name padded");
    auto namedOriginal = original;
    std::memcpy(namedOriginal.name.data(), "Existing name", 14);
    auto unnamed = finish;
    unnamed.name = {};
    Check(Desired(namedOriginal, unnamed, 7, 42).name == namedOriginal.name,
          "empty name preserves original account item name");
    finish.statTrak = 0;
    Check(Desired(original, finish, 7, 42).statTrak == -1, "disabled stattrak sentinel");
    Memory m;
    std::uint32_t a = 0, b = 1, c = 2;
    PatchSet p;
    p.Add(4, a, b);
    p.Add(12, a, c);
    Check(p.count == 2 && p.Commit(&m, Memory::Write), "transaction commits fields");
    Check(p.RestoreMatching(&m, Memory::Write) == 2 && m.bytes[4] == 0 && m.bytes[12] == 0,
          "exact original restoration");
    m.failAddress = 12;
    Check(!p.Commit(&m, Memory::Write) && m.bytes[4] == 0, "partial failure rolls back completed writes");
    m.failAddress = 0;
    Check(p.Commit(&m, Memory::Write), "reapply transaction");
    m.bytes[12] = 99;
    Check(p.RestoreMatching(&m, Memory::Write) == 1 && m.bytes[12] == 99 && m.bytes[4] == 0,
          "restore preserves newer server values");
    PatchSet same;
    same.Add(4, a, a);
    Check(same.count == 0 && same.Commit(&m, Memory::Write), "unchanged fields require no write");
    Catalog catalog;
    std::string error;
    Check(ParseCatalog(Items, R"("lang" { "Tokens" { "AK" "AK-47" "FINISH" "Sample Finish" } })", catalog, error),
          "parse local catalog format");
    Check(catalog.definitions.size() == 4 && catalog.paints.size() == 3, "non-cosmetic economy entries excluded");
    Check(catalog.Find(7) && catalog.Find(7)->name == "AK-47" && catalog.Find(7)->model.starts_with("weapons/"),
          "prefab inheritance and localization");
    Check(catalog.Find(507) && catalog.Find(507)->kind == ItemKind::Knife, "knife classification");
    Check(catalog.Find(5030) && catalog.Find(5030)->kind == ItemKind::Glove, "glove classification");
    Check(catalog.Find(5000) && catalog.Find(5000)->kind == ItemKind::Agent && catalog.Find(5000)->team == 2,
          "team restricted agents");
    Check(catalog.Paint(44) && catalog.Paint(44)->legacy && catalog.Paint(44)->name == "Sample Finish",
          "finish metadata");
    Check(catalog.Supports(7, 44) && catalog.Supports(507, 44) && catalog.Supports(5030, 55),
          "exact item-finish pair mapping");
    Check(!catalog.Supports(7, 55) && !catalog.Supports(9999, 44) && catalog.Supports(7, 0),
          "incompatible finish rejected and stock supported");
    auto extended = std::string(Items);
    extended.insert(extended.rfind('}'),
                    R"( "paint_kits" { "66" { "name" "new_finish" } "44" { "description_tag" "#NEWLABEL" } } )");
    Catalog merged;
    Check(ParseCatalog(extended, {}, merged, error) && merged.Paint(66) && merged.Paint(44)->legacy &&
              merged.paints.size() == 4,
          "repeated Valve sections merge definitions and fields");
    const auto saved = catalog.definitions.size();
    Check(!ParseCatalog("\"items_game\" {", {}, catalog, error) && catalog.definitions.size() == saved,
          "parse failure transactional");
    auto cycle = std::string(Items);
    const auto at = cycle.find("\"item_class\" \"weapon_ak47\"");
    cycle.insert(at, "\"prefab\" \"rifle\" ");
    Check(!ParseCatalog(cycle, {}, catalog, error), "cyclic prefab rejected");
    Check(!Initialize(0), "native adapter fails closed without client");
    Tick(7);
    Tick(8);
    Check(SUCCEEDED(StopNative()), "inactive native stop safe");
    Shutdown();
    if (argc == 3 && std::string_view(argv[1]) == "--catalog") {
        Catalog live;
        Check(LoadCatalog(std::filesystem::path(argv[2]), live, error), "installed catalog offline parse");
        if (!error.empty())
            std::cerr << error << '\n';
        std::cout << "Installed catalog: " << live.definitions.size() << " definitions, " << live.paints.size()
                  << " finishes, " << live.pairs.size() << " valid pairs\n";
        Check(live.Find(7) && live.Find(507) && live.Find(5030), "installed weapon knife glove entries");
        Check(live.pairs.size() > 2000 && live.paints.size() > 1000,
              "installed complete asset finish compatibility index");
        unsigned agents{};
        for (const auto &d : live.definitions)
            agents += d.kind == ItemKind::Agent ? 1 : 0;
        Check(agents > 30, "installed agent catalog inheritance");
        CatalogController controller;
        Check(controller.Start(std::filesystem::path(argv[2])), "start async catalog");
        controller.Stop();
        Check(controller.Snapshot() && !controller.Busy(), "publish immutable completed catalog");
        Check(controller.Start(std::filesystem::path(argv[2]) / "missing-catalog-fixture"), "retry catalog load");
        controller.Stop();
        Check(!controller.Snapshot() && !controller.Error().empty(), "catalog retry cannot publish stale definitions");
    }
    std::cout << passed << " checks passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
