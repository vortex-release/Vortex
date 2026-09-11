#pragma once
#include "app_paths.hpp"
#include <vector>

namespace vortex {
struct Process {
    DWORD id{};
    std::wstring name;
    ULONGLONG created{};
};
std::vector<Process> FindTargets();
// Kept separate for an integration test against our own inert host and fixture DLL.
std::string LoadDll(const Process &process, const std::filesystem::path &dll);
bool CanReplaceApplication(const std::filesystem::path &directory, std::string &reason);
} // namespace vortex
