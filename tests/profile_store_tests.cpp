#include "profile_browser.hpp"
#include <cstdio>
#include <fstream>
#include <thread>
#include <atomic>
HMODULE OverlayModule() noexcept {
    return GetModuleHandleW(nullptr);
}
int main() {
    using namespace awareness;
    namespace fs = std::filesystem;
    using namespace profiles;
    int failures{};
    const auto check = [&](bool good, const char *label) {
        std::printf("%s %s\n", good ? "PASS" : "FAIL", label);
        failures += !good;
    };
    const auto rejected = [&](auto work, const char *label) {
        bool threw{};
        try {
            work();
        } catch (...) {
            threw = true;
        }
        check(threw, label);
    };
    // Every fixture lives under an isolated, unique test folder.
    const auto root = vortex::DataDirectory() / (L"named-profile-tests-" + std::to_wstring(GetCurrentProcessId()) +
                                                 L"-" + std::to_wstring(GetTickCount64()));
    try {
        Store store(root / L"library");
        State original;
        original.config.opacity = .43f;
        original.visual.sky.enabled = 1;
        original.visual.playerStyle.boxGlow = .27f;
        original.visual.assists.shoot = 1;
        original.visual.menuFont = 11;
        store.Save("Practice", original);
        check(store.List().size() == 1 && store.Load("Practice").config.opacity == .43f,
              "create and load a named setup");
        const auto working = root / L"OverlaySettings.ini";
        check(SaveSettings(working.wstring(), original.config, original.visual, original.tracking, original.effects),
              "working settings fixture");
        auto other = original;
        other.config.opacity = .71f;
        rejected([&] { store.Save("practice", other); }, "case-insensitive duplicate names never overwrite");
        check(store.Load("Practice").config.opacity == .43f, "failed create leaves the prior profile intact");
        store.Save("Practice", other, true);
        check(store.Load("Practice").config.opacity == .71f, "explicit replace saves the selected snapshot");
        State active;
        check(LoadSettings(working.wstring(), active.config, active.visual) && active.config.opacity == .43f,
              "named profile operations never change working settings");
        for (const auto *name : {"", ".", "..", "../escape", "bad/name", "bad\\name", "CON", "NUL.txt", "COM1", "LPT2",
                                 " tail", "tail ", "tail.", "a:b"})
            check(!ValidName(name), "invalid and reserved filenames rejected");
        check(ValidName("Quiet evening") && ValidName("Config.v2") && ValidName("\xE6\x98\x9F\xE7\xA9\xBA"),
              "spaces, interior dots and Unicode names accepted");
        store.Save("\xE6\x98\x9F\xE7\xA9\xBA", original);
        check(store.Load("\xE6\x98\x9F\xE7\xA9\xBA").config.opacity == .43f, "Unicode profile round trip");
        const auto duplicate = store.Duplicate("Practice");
        check(duplicate != "Practice" && store.Load(duplicate).config.opacity == .71f,
              "duplicate keeps independent settings");
        store.Rename(duplicate, "Broadcast");
        check(store.Load("Broadcast").visual.playerStyle.boxGlow == .27f, "rename preserves complete contents");
        rejected([&] { store.Rename("Broadcast", "Practice"); }, "rename collision preserves both profiles");

        const auto exports = root / L"exports";
        fs::create_directories(exports);
        const auto output = exports / L"shared.ini";
        store.Export("Practice", output);
        rejected([&] { store.Export("Broadcast", output); }, "export refuses an existing filename");
        const auto imported = store.Import(output);
        check(store.Load(imported).visual.sky.enabled == 1 && store.Load(imported).visual.menuFont == 11,
              "import/export retains the complete configuration");
        const auto importedAgain = store.Import(output);
        check(importedAgain != imported, "repeated import creates a unique name");

        const auto legacy = root / L"legacy.ini";
        std::ofstream(legacy) << "[Overlay]\nVersion=1\nopacity=0.38\n";
        active = original;
        active.config.worldUnitsPerMeter = 39.37f;
        active.visual.sessionBadge = 0;
        active.visual.keepAwake = 1;
        check(LoadSettings(legacy.wstring(), active.config, active.visual, &active.tracking, &active.effects) &&
                  active.config.opacity == .38f && !active.visual.sky.enabled &&
                  active.visual.playerStyle.boxGlow == 0 && !active.visual.assists.shoot &&
                  active.visual.sessionBadge == 1 && !active.visual.keepAwake &&
                  active.config.worldUnitsPerMeter == 39.37f,
              "older profiles reset omitted features while preserving the host geometry scale");

        const auto invalid = root / L"invalid.ini";
        std::ofstream(invalid) << "[Overlay]\nVersion=1\nopacity=nan\n";
        rejected([&] { store.Import(invalid); }, "invalid import never enters the library");
        const auto repair = store.Path("Repair me");
        std::ofstream(repair) << "";
        store.Save("Repair me", original, true);
        check(store.Load("Repair me").config.opacity == .43f, "explicit replace repairs a corrupt saved profile");
        const auto large = root / L"large.ini";
        std::ofstream(large) << "[Overlay]\nVersion=1\n" << std::string(MaximumProfileBytes + 1, ' ');
        rejected([&] { store.Import(large); }, "oversized profile rejected before parsing");
        active = original;
        check(!LoadSettings(large.wstring(), active.config, active.visual) && active.config.opacity == .43f,
              "oversized working-file load leaves active settings untouched");
        store.Archive("Broadcast");
        check(!fs::exists(store.Path("Broadcast")), "archive removes only the selected library entry");
        fs::path archived;
        for (const auto &entry : fs::directory_iterator(store.Directory()))
            if (entry.path().extension() == L".archived")
                archived = entry.path();
        check(!archived.empty() && fs::exists(archived), "archive keeps a recoverable original file");
        const auto restored = store.Import(archived, "Restored");
        check(store.Load(restored).config.opacity == .71f, "archived settings can be imported again");

        // Concurrent saves get separate temporary files and publish whole snapshots.
        const auto race = root / L"atomic.ini";
        std::atomic<unsigned> saves{};
        const auto writer = [&](float opacity) {
            auto state = original;
            state.config.opacity = opacity;
            state.visual.fillOpacity = opacity / 2;
            for (unsigned i = 0; i < 12; ++i)
                saves += SaveSettings(race.wstring(), state.config, state.visual, state.tracking, state.effects);
        };
        std::thread a(writer, .4f), b(writer, .8f);
        a.join();
        b.join();
        check(saves > 0 && LoadSettings(race.wstring(), active.config, active.visual) &&
                  (active.config.opacity == .4f || active.config.opacity == .8f) &&
                  active.visual.fillOpacity == active.config.opacity / 2,
              "parallel atomic saves publish a complete coherent profile");
        bool leftovers{};
        for (const auto &entry : fs::directory_iterator(root))
            leftovers |= entry.path().filename().wstring().find(L".tmp.") != std::wstring::npos;
        check(!leftovers, "atomic writes leave no temporary files");

        Browser browser(store.Directory());
        State runtime;
        runtime.config.worldUnitsPerMeter = 39.37f;
        check(browser.Start({Operation::Load, "Practice"}, runtime) && !browser.Start({Operation::Refresh}),
              "browser serializes user operations without blocking the caller");
        Completion completion;
        const auto deadline = GetTickCount64() + 5000;
        while (!browser.Poll(completion) && GetTickCount64() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(completion.success && completion.loaded && completion.catalogReady &&
                  completion.state.config.opacity == .71f && completion.state.config.worldUnitsPerMeter == 39.37f &&
                  !browser.Busy(),
              "background load returns a complete snapshot and refreshed catalog");
    } catch (const std::exception &error) {
        std::printf("FAIL unexpected: %s\n", error.what());
        ++failures;
    }
    std::printf("Named-profile checks: %d failures\n", failures);
    return failures ? 1 : 0;
}
