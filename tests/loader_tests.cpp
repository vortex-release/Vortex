#include "../app/loader.hpp"
#include <fstream>
#include <iostream>
int wmain(int argc, wchar_t **argv) {
    using namespace vortex;
    if (argc > 1 && std::wstring(argv[1]) == L"--host") {
        auto name = L"Local\\VortexFixture.Stop." + std::to_wstring(GetCurrentProcessId());
        Handle stop(CreateEventW(nullptr, TRUE, FALSE, name.c_str()));
        WaitForSingleObject(stop.value, 60000);
        return 0;
    }
    int failures{};
    auto check = [&](bool good, const char *name) {
        std::cout << (good ? "PASS " : "FAIL ") << name << '\n';
        if (!good)
            ++failures;
    };
    const auto executable = ModuleDirectory() / L"vortex_loader_tests.exe";
    std::wstring command = L"\"" + executable.wstring() + L"\" --host";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &info))
        return 1;
    Handle process(info.hProcess), thread(info.hThread);
    Handle stop(CreateEventW(nullptr, TRUE, FALSE,
                             (L"Local\\VortexFixture.Stop." + std::to_wstring(info.dwProcessId)).c_str()));
    Handle loaded(CreateEventW(nullptr, TRUE, FALSE,
                               (L"Local\\VortexFixture.Loaded." + std::to_wstring(info.dwProcessId)).c_str()));
    FILETIME created{}, exit{}, kernel{}, user{};
    GetProcessTimes(process.value, &created, &exit, &kernel, &user);
    Process selected{info.dwProcessId, L"vortex_loader_tests.exe",
                     (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime};
    try {
        const auto dll = ModuleDirectory() / L"VortexLoaderFixture.dll";
        auto stale = selected;
        stale.created++;
        bool rejected{};
        try {
            LoadDll(stale, dll);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "reject reused/stale PID");
        const auto result = LoadDll(selected, dll);
        check(WaitForSingleObject(loaded.value, 1000) == WAIT_OBJECT_0, "DLL entry point ran in controlled x64 host");
        check(result.find("loaded") != std::string::npos, "exact loaded DLL confirmed");
        check(LoadDll(selected, dll).find("already") != std::string::npos, "duplicate load is idempotent");
        const auto guardDirectory = ModuleDirectory() / (L"update-guard-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(guardDirectory);
        const auto guardDll = guardDirectory / L"EntityAwarenessOverlay.dll";
        std::filesystem::copy_file(dll, guardDll, std::filesystem::copy_options::overwrite_existing);
        LoadDll(selected, guardDll);
        std::string reason;
        check(!CanReplaceApplication(guardDirectory, reason), "update blocked while a host maps the DLL");
        const auto bad = ModuleDirectory() / L"vortex-invalid-fixture.dll";
        std::ofstream(bad) << "invalid";
        rejected = false;
        try {
            LoadDll(selected, bad);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "reject invalid PE");
        std::filesystem::remove(bad);
        selected.name = L"wrong-process.exe";
        rejected = false;
        try {
            LoadDll(selected, dll);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "reject mismatched process name");
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        ++failures;
    }
    SetEvent(stop.value);
    check(WaitForSingleObject(process.value, 5000) == WAIT_OBJECT_0, "controlled host exits normally");
    return failures ? 1 : 0;
}
