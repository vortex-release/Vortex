#include <awareness/AddressDiscovery.hpp>
#include <awareness/PatternScan.hpp>
#include <Psapi.h>
#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

using namespace awareness;
namespace {
bool ReadLocal(std::uintptr_t address, void* destination, SIZE_T size) noexcept {
    if (!address || address > (std::numeric_limits<std::uintptr_t>::max)() - size) return false;
    SIZE_T read{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
        destination, size, &read) && read == size;
}
bool ReadablePage(DWORD protection) noexcept {
    if (protection & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    switch (protection & 0xFF) {
        case PAGE_READONLY: case PAGE_READWRITE: case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READ: case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY: return true;
        default: return false;
    }
}
struct ModuleReference {
    HMODULE handle{};
    ~ModuleReference() { if (handle) FreeLibrary(handle); }
};
struct Match {
    std::uintptr_t address{};
    std::uint32_t count{};
    void Add(std::uintptr_t value) noexcept {
        if (!count) address = value;
        if (count < 2) ++count;
    }
};
// No direct reads from module memory: copy readable chunks with ReadProcessMemory.
// Keep an overlap so a signature crossing a page/protection/chunk boundary is found.
bool ScanCodeRange(std::uintptr_t start, std::size_t size, const scan::Pattern& entityPattern,
                   const scan::Pattern& viewPattern, Match& entity, Match& view) {
    constexpr std::size_t ChunkSize = 64 * 1024;
    const auto overlap = (std::max)(entityPattern.size(), viewPattern.size()) - 1;
    std::vector<std::uint8_t> tail, buffer;
    std::uintptr_t cursor = start;
    const auto end = start + size; // The module/section bounds were validated by the caller.
    bool complete = true;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region))) return false;
        const auto regionBase = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (regionBase > cursor || region.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - regionBase)
            return false;
        const auto regionEnd = (std::min)(regionBase + region.RegionSize, end);
        if (regionEnd <= cursor) return false;
        if (region.State != MEM_COMMIT || !ReadablePage(region.Protect)) {
            complete = false;
            tail.clear();
            cursor = regionEnd;
            continue;
        }
        while (cursor < regionEnd) {
            const auto amount = (std::min)(ChunkSize, static_cast<std::size_t>(regionEnd - cursor));
            const auto prefix = tail.size();
            buffer.resize(prefix + amount);
            std::copy(tail.begin(), tail.end(), buffer.begin());
            if (!ReadLocal(cursor, buffer.data() + prefix, amount)) {
                complete = false;
                tail.clear();
                cursor += amount;
                continue;
            }
            const auto scanPattern = [&](const scan::Pattern& pattern, Match& match) {
                scan::ForEachMatch(buffer, pattern, [&](std::size_t offset) {
                    // Shorter signatures wholly inside the overlap were already counted.
                    if (offset + pattern.size() > prefix) match.Add(cursor - prefix + offset);
                });
            };
            scanPattern(entityPattern, entity);
            scanPattern(viewPattern, view);
            const auto keep = (std::min)(overlap, buffer.size());
            tail.assign(buffer.end() - static_cast<std::ptrdiff_t>(keep), buffer.end());
            cursor += amount;
        }
    }
    return complete;
}
HRESULT ScanModule(HMODULE module, Match& entity, Match& view) {
    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info))) return HRESULT_FROM_WIN32(GetLastError());
    const auto base = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
    const auto size = static_cast<std::size_t>(info.SizeOfImage);
    if (!size || base > (std::numeric_limits<std::uintptr_t>::max)() - size) return HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT);
    const auto inImage = [size](std::size_t offset, std::size_t length) noexcept {
        return offset <= size && length <= size - offset;
    };
    IMAGE_DOS_HEADER dos{};
    if (!inImage(0, sizeof(dos)) || !ReadLocal(base, &dos, sizeof(dos)) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) return HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT);
    const auto ntOffset = static_cast<std::size_t>(dos.e_lfanew);
    IMAGE_NT_HEADERS64 nt{};
    if (!inImage(ntOffset, sizeof(nt)) || !ReadLocal(base + ntOffset, &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt.FileHeader.NumberOfSections == 0 || nt.FileHeader.NumberOfSections > 96)
        return HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT);
    const auto table = ntOffset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
    const auto count = nt.FileHeader.NumberOfSections;
    if (!inImage(table, static_cast<std::size_t>(count) * sizeof(IMAGE_SECTION_HEADER)))
        return HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT);
    std::vector<IMAGE_SECTION_HEADER> sections(count);
    if (!ReadLocal(base + table, sections.data(), sections.size() * sizeof(IMAGE_SECTION_HEADER)))
        return HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY);
    const auto entityPattern = scan::ParsePattern(scan::EntityListSignature);
    const auto viewPattern = scan::ParsePattern(scan::ViewMatrixSignature);
    if (!entityPattern || !viewPattern) return E_UNEXPECTED;
    bool complete = true;
    for (const auto& section : sections) {
        if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const auto length = static_cast<std::size_t>(section.Misc.VirtualSize);
        if (!inImage(section.VirtualAddress, length)) return HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT);
        if (length && !ScanCodeRange(base + section.VirtualAddress, length, *entityPattern, *viewPattern, entity, view))
            complete = false;
    }
    return complete ? S_OK : HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY);
}
ScanStatus ResolveMatch(const Match& match, std::uint8_t opcode, std::uintptr_t& instruction,
                        std::uintptr_t& target) noexcept {
    if (!match.count) return ScanStatus::NotFound;
    if (match.count > 1) return ScanStatus::Ambiguous;
    instruction = match.address;
    std::array<std::uint8_t, 7> bytes{};
    if (!ReadLocal(instruction, bytes.data(), bytes.size())) return ScanStatus::UnreadableTarget;
    if (!scan::IsRipRelative7(bytes, opcode)) return ScanStatus::NotRipRelative;
    const auto result = scan::ResolveRelative32(instruction, bytes, 3, 7);
    if (!result || !*result) return ScanStatus::UnreadableTarget;
    target = *result;
    return ScanStatus::Found;
}
}

HRESULT __cdecl AwarenessFindAddresses(const wchar_t* moduleName, DiscoveredAddresses* output) noexcept {
    try {
        if (!output || output->size != sizeof(*output) || output->version != ApiVersion) return E_INVALIDARG;
        *output = {};
        ModuleReference module;
        if (!GetModuleHandleExW(0, moduleName ? moduleName : L"client.dll", &module.handle))
            return HRESULT_FROM_WIN32(GetLastError());
        Match entity, view;
        const HRESULT hr = ScanModule(module.handle, entity, view);
        output->entityMatches = entity.count;
        output->viewMatches = view.count;
        if (FAILED(hr)) {
            output->entityStatus = output->viewStatus = ScanStatus::IncompleteScan;
            return hr; // Incomplete coverage cannot prove a signature is unique.
        }
        output->entityStatus = ResolveMatch(entity, 0x8B, output->entityListInstruction, output->entityListSlot);
        output->viewStatus = ResolveMatch(view, 0x8D, output->viewMatrixInstruction, output->viewMatrix);
        if (output->entityStatus == ScanStatus::Found) {
            std::uintptr_t value{}, probe{};
            if (!ReadLocal(output->entityListSlot, &value, sizeof(value))) output->entityStatus = ScanStatus::UnreadableTarget;
            else if (!value) output->entityStatus = ScanStatus::NullEntityList;
            else if (!ReadLocal(value, &probe, sizeof(probe))) output->entityStatus = ScanStatus::UnreadableTarget;
            else output->entityList = value; // MOV reads the pointer stored at the RIP-relative slot.
        }
        if (output->viewStatus == ScanStatus::Found) {
            std::array<std::uint8_t, sizeof(Matrix4x4)> probe{};
            if (!ReadLocal(output->viewMatrix, probe.data(), probe.size())) {
                output->viewStatus = ScanStatus::UnreadableTarget;
                output->viewMatrix = 0;
            } // LEA returns the target address directly; do not dereference a pointer here.
        }
        if (output->entityStatus == ScanStatus::Found && output->viewStatus == ScanStatus::Found) return S_OK;
        if (output->entityStatus == ScanStatus::Ambiguous || output->viewStatus == ScanStatus::Ambiguous)
            return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
        if (output->entityStatus == ScanStatus::NotRipRelative || output->viewStatus == ScanStatus::NotRipRelative)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        if (output->entityStatus == ScanStatus::UnreadableTarget || output->viewStatus == ScanStatus::UnreadableTarget)
            return HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY);
        if (output->entityStatus == ScanStatus::NullEntityList) return HRESULT_FROM_WIN32(ERROR_NOT_READY);
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (...) { return E_FAIL; }
}
