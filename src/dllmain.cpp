#include "bootstrap.hpp"
#include "runtime_support.hpp"
#include "cs2_offsets.hpp"
#include "../app/app_paths.hpp"
#include "app_config.hpp"
#include <MinHook.h>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cwchar>

namespace {
HANDLE bootstrapThread{};
HMODULE ownModule{}, automaticReference{};
std::atomic<bool> initialized{false}, stopRequested{false};
DWORD WINAPI Bootstrap(void *) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct ComScope {
        HRESULT status;
        ~ComScope() {
            if (SUCCEEDED(status))
                CoUninitialize();
        }
    } comScope{com};
    char version[128]{};
    std::snprintf(version, sizeof(version), "Vortex %s loaded; offset snapshot build %u.", vortex::AppVersion,
                  awareness::cs2::offsets::ExpectedBuild);
    OverlayLog(version);
    wchar_t modulePath[32768]{};
    GetModuleFileNameW(ownModule, modulePath, 32768);
    try {
        const auto identity = std::string("Loaded module: ") + vortex::Utf8(modulePath) +
                              "; PID=" + std::to_string(GetCurrentProcessId()) +
                              "; COM=" + std::to_string(static_cast<unsigned long>(com)) +
                              "; profile=" + vortex::Utf8(vortex::DataDirectory().wstring());
        OverlayLog(identity.c_str());
    } catch (...) {
        OverlayLog("Could not resolve module/profile diagnostics.");
    }
    const auto status = MH_Initialize();
    initialized.store(status == MH_OK, std::memory_order_release);
    if (status != MH_OK) {
        OverlayLog("MinHook initialization failed");
        return static_cast<DWORD>(status);
    }
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    const auto *slash = std::wcsrchr(executable, L'\\');
    const auto *name = slash ? slash + 1 : executable;
    if (_wcsicmp(name, L"cs2.exe") == 0) {
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&Bootstrap),
                                &automaticReference)) {
            OverlayLog("Automatic startup could not retain its DLL reference.");
            return 0;
        }
        StartCs2Automatically();
    }
    return 0;
}
} // namespace
HMODULE OverlayModule() noexcept {
    return ownModule;
}
bool BootstrapStopRequested() noexcept {
    return stopRequested.load();
}
void RequestBootstrapStop() noexcept {
    stopRequested = true;
}
void WaitForOverlayBootstrap() noexcept {
    if (bootstrapThread) {
        WaitForSingleObject(bootstrapThread, INFINITE);
        CloseHandle(bootstrapThread);
        bootstrapThread = nullptr;
    }
}
HRESULT EnsureHookLibrary() noexcept {
    WaitForOverlayBootstrap();
    if (initialized.load(std::memory_order_acquire))
        return S_OK;
    const auto status = MH_Initialize();
    if (status != MH_OK)
        return E_FAIL;
    initialized = true;
    return S_OK;
}
HRESULT StopHookLibrary() noexcept {
    RequestBootstrapStop();
    WaitForOverlayBootstrap();
    if (initialized && MH_Uninitialize() != MH_OK)
        return E_FAIL;
    initialized = false;
    if (automaticReference) {
        const auto reference = automaticReference;
        automaticReference = nullptr;
        FreeLibrary(reference); // The explicit API caller must still own its module reference.
    }
    return S_OK;
}
void OverlayLog(const char *message) noexcept {
    OutputDebugStringA(message);
    std::filesystem::path path;
    try {
        path = vortex::LogDirectory() / L"EntityAwarenessOverlay.log";
    } catch (...) {
        return;
    }
    const auto file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    SYSTEMTIME time{};
    GetLocalTime(&time);
    char line[512]{};
    const int length = std::snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u %s\r\n", time.wYear,
                                     time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, message);
    DWORD written{};
    if (length > 0)
        WriteFile(file, line, static_cast<DWORD>((std::min)(length, static_cast<int>(sizeof(line) - 1))), &written,
                  nullptr);
    CloseHandle(file);
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        ownModule = module;
        // Keep thread notifications enabled: the statically linked CRT requires them.
        bootstrapThread = CreateThread(nullptr, 0, Bootstrap, nullptr, 0, nullptr);
        return bootstrapThread != nullptr;
    }
    // No graphics work or cross-thread waits while the loader lock is held.
    return TRUE;
}
