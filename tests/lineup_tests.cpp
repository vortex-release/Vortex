#include "grenade_lineups.hpp"
#include "grenade_lineups_draw.hpp"
#include "grenade_lineups_panel.hpp"
#include <cstdio>
#include <limits>
#include <thread>
using namespace awareness;
using namespace awareness::lineups;
namespace {
int checks{}, failures{};
void Check(bool value, const char *text) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s\n", text);
    }
}
template <class F> void Throws(F f, const char *label) {
    try {
        f();
        Check(false, label);
    } catch (const std::exception &) {
        Check(true, label);
    }
}
void Drain(Controller &controller) {
    for (unsigned i = 0; i < 500 && controller.Busy(); ++i) {
        controller.Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Check(!controller.Busy(), "background file operation completed");
}
} // namespace
int main() {
    Options options;
    Check(Valid(options), "valid quiet defaults");
    Check(NormalizeMap("workshop/234/DE_MIRAGE.vmap_c") == "de_mirage", "map normalization");
    Check(NormalizeMap("maps\\de_nuke.bsp") == "de_nuke", "Windows path normalization");
    Check(NormalizeMap("../..").empty() && NormalizeMap("de bad").empty(), "ambiguous map rejected");
    Check(WeaponKind(46) == Kind::Fire && WeaponKind(48) == Kind::Fire && WeaponKind(7) == Kind::Any,
          "grenade classification");
    Capture capture{true, "de_mirage", {10, 20, 30}, {10, 20, 94}, -15, 179, 45, 10};
    Library library;
    auto record = MakeRecord(capture, "Window smoke", 1, "Run 2 steps, jump, release");
    const auto id = library.Add(record);
    Check(id && library.Find(id) && library.Find(id)->eyeHeight == 64, "manual capture saves actual eye height");
    const auto aim = AimPoint(record);
    Check(aim.z > record.position.z + record.eyeHeight, "negative Source pitch aims upward");
    Throws(
        [&] {
            auto invalid = capture;
            invalid.weapon = 7;
            MakeRecord(invalid, "bad", 0);
        },
        "firearm cannot fabricate grenade capture");
    Throws(
        [&] {
            auto invalid = capture;
            invalid.map.clear();
            MakeRecord(invalid, "bad", 0);
        },
        "capture needs explicit map");
    options.enabled = 1;
    auto selected = Select(library.Records(), capture, options, 39.37f, 10.1);
    Check(selected.count == 1 && selected.guides[0].aligned, "standing at captured position and angle is aligned");
    capture.yaw = -179;
    selected = Select(library.Records(), capture, options, 39.37f, 10.1);
    Check(selected.count == 1 && selected.guides[0].angle == 2 && !selected.guides[0].aligned,
          "yaw wraps across 180 degrees");
    capture.yaw = 539;
    Check(MakeRecord(capture, "Wrapped", 0).yaw == 179 &&
              Select(library.Records(), capture, options, 39.37f, 10.1).guides[0].aligned,
          "continuous raw yaw normalizes at capture and selection");
    capture.yaw = 179;
    capture.map = "de_nuke";
    Check(Select(library.Records(), capture, options, 39.37f, 10.1).count == 0, "other map never leaks guides");
    strcpy_s(options.mapOverride, "de_mirage");
    Check(Select(library.Records(), capture, options, 39.37f, 10.1).count == 0,
          "native map has priority over manual fallback");
    capture.map.clear();
    Check(Select(library.Records(), capture, options, 39.37f, 10.1).count == 1, "explicit map fallback works");
    capture.map = "de_mirage";
    capture.weapon = 44;
    Check(Select(library.Records(), capture, options, 39.37f, 10.1).count == 0, "held grenade filter");
    options.heldOnly = 0;
    Check(Select(library.Records(), capture, options, 39.37f, 10.1).count == 1, "all grenade option");
    Check(Select(library.Records(), capture, options, 39.37f, 10.3).count == 0, "stale capture hides guides");
    Check(Select(library.Records(), capture, options, 39.37f, 9).count == 0, "clock rollback hides guides");
    Check(Select(library.Records(), capture, options, 0, 10).count == 0, "bad unit scale hides guides");
    for (unsigned i = 0; i < 30; ++i) {
        auto extra = record;
        extra.name = "Extra " + std::to_string(i);
        extra.position.x += 20 * (30 - i);
        library.Add(extra);
    }
    selected = Select(library.Records(), capture, options, 39.37f, 10.1);
    Check(selected.count == 8 && selected.guides[0].index == 0, "nearest guides are bounded and stable");
    for (std::size_t i = 1; i < selected.count; ++i)
        Check(selected.guides[i].distance >= selected.guides[i - 1].distance, "guide distance ordering");
    const auto text = library.Serialize();
    Library copy;
    Check(copy.Parse(text, false) == 31 && copy.Records().size() == 31, "JSON round trip all records");
    Check(copy.Records()[0].notes == record.notes && copy.Records()[0].throwType == 1, "throw instructions roundtrip");
    Check(copy.Parse(text, true) == 0 && copy.Records().size() == 31, "duplicate import is idempotent");
    const std::string legacy =
        R"([{"name":"Legacy","map":"workshop/1/de_mirage","kind":3,"throw":4,"pos":[1,2,3],"ang":[10,20,46],"enabled":true,"inputs":"Crouch"}])";
    Check(copy.Parse(legacy, true) == 1 && copy.Records().back().eyeHeight == 46,
          "Anthony JSON imports captured crouch eye height");
    const auto preserved = copy.Serialize();
    for (const auto *malformed :
         {"{", R"({"version":2,"lineups":[]})",
          R"([{ "name":"bad","map":"de_nuke","kind":4294967296,"pos":[1,2,3],"ang":[0,0,0]}])",
          R"([{ "name":"bad","map":"de_nuke","kind":3,"throw":-1,"pos":[1,2,3],"ang":[0,0,0]}])",
          R"([{ "name":"bad","map":"de_nuke","kind":3,"pos":[1e30,2,3],"ang":[0,0,0]}])"}) {
        Throws([&] { copy.Parse(malformed, false); }, "malformed JSON rejected");
        Check(copy.Serialize() == preserved, "failed replacement preserves library");
    }
    auto update = *copy.Find(copy.Records()[0].id);
    update.notes = "Changed";
    Check(copy.Update(update) && copy.Find(update.id)->notes == "Changed", "edit persists to exact id");
    update.pitch = 100;
    Check(!copy.Update(update), "bad edit rejected");
    Check(copy.Remove(update.id) && !copy.Find(update.id) && !copy.Remove(update.id), "remove exact id once");
    Options invalid = options;
    invalid.aimTolerance = std::numeric_limits<float>::quiet_NaN();
    Check(!Valid(invalid), "NaN rejected");
    memset(invalid.mapOverride, 'a', sizeof(invalid.mapOverride));
    Check(!Valid(invalid), "unterminated map rejected");
    const auto directory =
        std::filesystem::temp_directory_path() / (L"Vortex-lineup-tests-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto path = directory / L"roundtrip.json";
    library.Save(path);
    Library disk;
    disk.Load(path);
    Check(disk.Serialize() == library.Serialize(), "atomic file save and load");
    library.Remove(id);
    library.Save(path);
    disk.Load(path);
    Check(disk.Records().size() == 30, "atomic existing file replacement");
    {
        Controller controller;
        controller.Start(directory / L"controller");
        Drain(controller);
        Request request{Operation::Capture};
        request.record = record;
        controller.Submit(request);
        Drain(controller);
        Check(controller.Records().Records().size() == 1, "async capture publishes after save");
        Request exported{Operation::Export};
        exported.path = directory / L"export.json";
        controller.Submit(exported);
        Drain(controller);
        Check(std::filesystem::exists(exported.path), "async export path");
        Request imported{Operation::Import};
        imported.path = path;
        controller.Submit(imported);
        Drain(controller);
        Check(controller.Records().Records().size() == 31, "async imported entries merge with capture");
        Request failed{Operation::Import};
        failed.path = directory / L"missing.json";
        controller.Submit(failed);
        Drain(controller);
        Check(controller.Records().Records().size() == 31 && !controller.Message().empty(),
              "async failed import preserves published state");
    }
    {
        Controller stopped;
        stopped.Start(directory / L"stopped");
        stopped.Stop();
        stopped.Tick();
        Request blocked{Operation::Capture};
        blocked.record = record;
        stopped.Submit(blocked);
        Check(!stopped.Busy() && stopped.Records().Records().empty(), "stopped controller rejects new IO");
    }
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.DisplaySize = {1280, 720};
    io.DeltaTime = 1.f / 60;
    unsigned char *pixels{};
    int width{}, height{};
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    FrameSnapshot frame;
    frame.viewProjection.m[0][0] = .001f;
    frame.viewProjection.m[1][2] = .001f;
    frame.viewProjection.m[2][1] = .0001f;
    frame.viewProjection.m[3][3] = 1;
    Configuration config;
    config.worldUnitsPerMeter = 39.37f;
    capture.weapon = 45;
    Library drawing;
    drawing.Add(record);
    auto *draw = ImGui::GetBackgroundDrawList();
    const auto before = draw->VtxBuffer.Size;
    Draw(*draw, ImGui::GetFont(), drawing, capture, frame, {0, 0, 1280, 720}, config, options, 10.1);
    Check(draw->VtxBuffer.Size > before, "guides emit real draw geometry");
    const auto liveVertices = draw->VtxBuffer.Size;
    Draw(*draw, ImGui::GetFont(), drawing, capture, frame, {0, 0, 1280, 720}, config, options, 11);
    Check(draw->VtxBuffer.Size == liveVertices, "stale capture emits no geometry");
    ImGui::EndFrame();
    ImGui::DestroyContext();
    const auto resolved = std::filesystem::weakly_canonical(directory);
    const auto tempRoot = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    Check(resolved.parent_path() == tempRoot && resolved.filename().wstring().starts_with(L"Vortex-lineup-tests-"),
          "fixture cleanup remains inside its temporary workspace");
    if (resolved.parent_path() == tempRoot && resolved.filename().wstring().starts_with(L"Vortex-lineup-tests-"))
        std::filesystem::remove_all(resolved);
    std::printf("Lineup checks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
