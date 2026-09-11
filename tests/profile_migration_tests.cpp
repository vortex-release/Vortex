#include "../app/app_paths.hpp"
#include "../src/settings.hpp"
#include <fstream>
#include <iostream>
HMODULE OverlayModule() noexcept {
    return nullptr;
}
int wmain(int argc, wchar_t **) {
    using namespace vortex;
    using namespace awareness;
    if (argc == 1) {
        const auto root = DataDirectory() / (L"migration-" + std::to_wstring(GetCurrentProcessId()));
        const auto install = root / L"install";
        std::filesystem::create_directories(install);
        const auto executable = install / L"vortex_profile_tests.exe";
        std::filesystem::copy_file(ModuleDirectory() / L"vortex_profile_tests.exe", executable,
                                   std::filesystem::copy_options::overwrite_existing);
        SetEnvironmentVariableW(L"VORTEX_DATA_DIR", (root / L"user").c_str());
        std::wstring command = L"\"" + executable.wstring() + L"\" --child";
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                            nullptr, &si, &pi))
            return 1;
        Handle process(pi.hProcess), thread(pi.hThread);
        if (WaitForSingleObject(process.value, 10000) != WAIT_OBJECT_0)
            return 2;
        DWORD code{};
        GetExitCodeProcess(process.value, &code);
        std::ifstream report(root / L"user" / L"result.txt");
        std::cout << report.rdbuf();
        return static_cast<int>(code);
    }
    int failures{};
    std::ofstream report(DataDirectory() / L"result.txt");
    auto check = [&](bool value, const char *message) {
        report << (value ? "PASS " : "FAIL ") << message << '\n';
        if (!value)
            ++failures;
    };
    try {
        const auto old = ModuleDirectory();
        std::filesystem::create_directories(old / L"profile");
        std::ofstream(old / L"profile" / L"custom.png") << "test-asset";
        Configuration c;
        VisualOptions v;
        TrackingConfiguration t;
        EffectsConfiguration e;
        c.opacity = .43f;
        strcpy_s(v.backgroundPath, Utf8((old / L"profile" / L"custom.png").wstring()).c_str());
        check(SaveSettings((old / L"OverlaySettings.ini").wstring(), c, v, t, e), "write legacy profile");
        c.opacity = .71f;
        check(SaveSettings((old / L"DefaultSettings.ini").wstring(), c, v, t, e), "write packaged defaults");
        const auto migrated = SettingsPath();
        Configuration loaded;
        VisualOptions visual;
        check(std::filesystem::path(migrated).parent_path() == DataDirectory(),
              "settings move outside install directory");
        check(LoadSettings(migrated, loaded, visual) && loaded.opacity == .43f, "existing choices survive migration");
        check(std::filesystem::equivalent(std::filesystem::path(Wide(visual.backgroundPath)),
                                          DataDirectory() / L"profile" / L"custom.png"),
              "custom relative asset relocated");
        check(std::filesystem::exists(DataDirectory() / L"profile" / L"custom.png"), "custom asset copied");
        loaded.opacity = .22f;
        check(SaveSettings(migrated, loaded, visual), "save user changes");
        check(SettingsPath() == migrated && LoadSettings(migrated, loaded, visual) && loaded.opacity == .22f,
              "migration never overwrites existing user settings");
        check(DefaultSettingsPath(migrated) == (old / L"DefaultSettings.ini").wstring(),
              "reset uses current packaged defaults");
        check(LoadSettings((old / L"OverlaySettings.ini").wstring(), loaded, visual) && loaded.opacity == .43f,
              "legacy profile remains untouched");
    } catch (const std::exception &error) {
        report << error.what() << '\n';
        ++failures;
    }
    return failures ? 1 : 0;
}
