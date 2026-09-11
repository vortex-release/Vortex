#include "loader.hpp"
#include <tlhelp32.h>
#include <fstream>
#include <algorithm>

namespace vortex {
namespace {
ULONGLONG CreationTime(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
        return 0;
    return (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
std::vector<MODULEENTRY32W> Modules(DWORD pid) {
    for (int retry = 0; retry != 6; ++retry) {
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
        if (!snapshot) {
            if (GetLastError() == ERROR_BAD_LENGTH)
                continue;
            throw std::runtime_error(WindowsError("Read process modules"));
        }
        MODULEENTRY32W entry{sizeof(entry)};
        std::vector<MODULEENTRY32W> result;
        if (Module32FirstW(snapshot.value, &entry)) {
            do {
                result.push_back(entry);
            } while (Module32NextW(snapshot.value, &entry));
        } else if (GetLastError() != ERROR_NO_MORE_FILES) {
            throw std::runtime_error(WindowsError("Read process modules"));
        }
        return result;
    }
    throw std::runtime_error("The process is still starting. Try again in a moment.");
}
bool SamePath(const std::filesystem::path &a, const std::filesystem::path &b) {
    std::error_code error;
    return std::filesystem::equivalent(a, b, error);
}
void ValidateDll(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    IMAGE_DOS_HEADER dos{};
    input.read(reinterpret_cast<char *>(&dos), sizeof(dos));
    if (!input || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < sizeof(dos) || dos.e_lfanew > 1048576)
        throw std::runtime_error("The bundled DLL is not a valid Windows library. Reinstall Vortex.");
    input.seekg(dos.e_lfanew);
    DWORD signature{};
    IMAGE_FILE_HEADER header{};
    input.read(reinterpret_cast<char *>(&signature), sizeof(signature));
    input.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (!input || signature != IMAGE_NT_SIGNATURE || header.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        !(header.Characteristics & IMAGE_FILE_DLL))
        throw std::runtime_error("Vortex requires its bundled x64 DLL. Reinstall the x64 release.");
}
} // namespace
std::vector<Process> FindTargets() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot)
        throw std::runtime_error(WindowsError("Find game process"));
    PROCESSENTRY32W entry{sizeof(entry)};
    std::vector<Process> result;
    if (Process32FirstW(snapshot.value, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"cs2.exe") != 0)
                continue;
            Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
            result.push_back({entry.th32ProcessID, entry.szExeFile, process ? CreationTime(process.value) : 0});
        } while (Process32NextW(snapshot.value, &entry));
    }
    if (GetLastError() != ERROR_NO_MORE_FILES)
        throw std::runtime_error(WindowsError("Read process list"));
    return result;
}
std::string LoadDll(const Process &target, const std::filesystem::path &dll) {
    const auto path = std::filesystem::canonical(dll);
    ValidateDll(path);
    Handle process(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                   PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE,
                               FALSE, target.id));
    if (!process)
        throw std::runtime_error(WindowsError("Open selected process"));
    if (!target.created || CreationTime(process.value) != target.created)
        throw std::runtime_error("The selected process changed. Refresh and select it again.");
    USHORT processMachine{}, nativeMachine{};
    if (!IsWow64Process2(process.value, &processMachine, &nativeMachine) ||
        processMachine != IMAGE_FILE_MACHINE_UNKNOWN || nativeMachine != IMAGE_FILE_MACHINE_AMD64)
        throw std::runtime_error("The selected process must be running natively as x64.");
    wchar_t image[32768]{};
    DWORD size = 32768;
    if (!QueryFullProcessImageNameW(process.value, 0, image, &size) ||
        _wcsicmp(std::filesystem::path(image).filename().c_str(), target.name.c_str()) != 0)
        throw std::runtime_error("The selected process no longer matches. Refresh the process list.");
    auto modules = Modules(target.id);
    for (const auto &module : modules) {
        if (SamePath(module.szExePath, path))
            return "The overlay is already loaded in this process.";
        if (_wcsicmp(module.szModule, path.filename().c_str()) == 0)
            throw std::runtime_error("A different copy of the overlay is already loaded. Restart the game first.");
    }
    // Resolve the actual owning module: forwarded exports may live in KernelBase.
    const auto loadLibrary = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner{};
    if (!loadLibrary ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(loadLibrary), &owner))
        throw std::runtime_error("Cannot locate the Windows library loader.");
    wchar_t ownerPath[32768]{};
    GetModuleFileNameW(owner, ownerPath, 32768);
    const auto ownerName = std::filesystem::path(ownerPath).filename();
    const auto offset = reinterpret_cast<std::uintptr_t>(loadLibrary) - reinterpret_cast<std::uintptr_t>(owner);
    LPTHREAD_START_ROUTINE remoteLoader{};
    // A just-created process can appear before KernelBase has finished loading.
    for (int attempt = 0; attempt < 60 && !remoteLoader; ++attempt) {
        if (attempt) {
            if (WaitForSingleObject(process.value, 50) != WAIT_TIMEOUT)
                throw std::runtime_error("The selected process exited while starting.");
            modules = Modules(target.id);
        }
        for (const auto &module : modules)
            if (_wcsicmp(module.szModule, ownerName.c_str()) == 0 && offset < module.modBaseSize &&
                SamePath(module.szExePath, ownerPath))
                remoteLoader = reinterpret_cast<LPTHREAD_START_ROUTINE>(module.modBaseAddr + offset);
    }
    if (!remoteLoader)
        throw std::runtime_error(
            "The process is still starting or uses an incompatible Windows loader. Try again shortly.");
    const auto text = path.wstring();
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    void *remotePath = VirtualAllocEx(process.value, nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!remotePath)
        throw std::runtime_error(WindowsError("Allocate DLL path"));
    SIZE_T written{};
    if (!WriteProcessMemory(process.value, remotePath, text.c_str(), bytes, &written) || written != bytes) {
        const auto error = GetLastError();
        VirtualFreeEx(process.value, remotePath, 0, MEM_RELEASE);
        throw std::runtime_error(WindowsError("Write DLL path", error));
    }
    Handle thread(CreateRemoteThread(process.value, nullptr, 0, remoteLoader, remotePath, 0, nullptr));
    if (!thread) {
        const auto error = GetLastError();
        VirtualFreeEx(process.value, remotePath, 0, MEM_RELEASE);
        throw std::runtime_error(WindowsError("Start Windows DLL loader", error));
    }
    const DWORD wait = WaitForSingleObject(thread.value, 30000);
    if (wait != WAIT_OBJECT_0) {
        // The target may still be reading this memory. Never free it or terminate its thread.
        throw std::runtime_error(
            "Loading did not finish within 30 seconds. Restart the game before retrying. No thread was terminated.");
    }
    VirtualFreeEx(process.value, remotePath, 0, MEM_RELEASE);
    // A thread exit code is only 32 bits; it is not a valid x64 HMODULE.
    for (const auto &module : Modules(target.id))
        if (SamePath(module.szExePath, path))
            return "Overlay loaded. Press Insert in the game to open its menu.";
    throw std::runtime_error("Windows did not load the DLL. Check Vortex logs and the target's compatibility.");
}
bool CanReplaceApplication(const std::filesystem::path &directory, std::string &reason) {
    // Conservatively require supported hosts to close, including hosts from a previous launcher session.
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) {
        reason = "Cannot verify that the application files are free.";
        return false;
    }
    PROCESSENTRY32W entry{sizeof(entry)};
    if (Process32FirstW(snapshot.value, &entry))
        do {
            if (_wcsicmp(entry.szExeFile, L"cs2.exe") == 0 || _wcsicmp(entry.szExeFile, L"ObserverDemo.exe") == 0) {
                reason = "Close the game and preview before installing the update.";
                return false;
            }
        } while (Process32NextW(snapshot.value, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        reason = "Cannot finish checking running hosts. Try the update again shortly.";
        return false;
    }
    Handle dll(CreateFileW((directory / L"EntityAwarenessOverlay.dll").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                           nullptr, OPEN_EXISTING, 0, nullptr));
    if (!dll) {
        reason = "The overlay file is still in use or cannot be updated. Close its host and try again.";
        return false;
    }
    reason.clear();
    return true;
}
} // namespace vortex
