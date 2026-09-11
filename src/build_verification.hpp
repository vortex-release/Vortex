#pragma once
#include "cs2_reader.hpp"
#include <awareness/PatternScan.hpp>
#include <vector>

namespace awareness::cs2 {
enum class BuildCheck { Verified, Unreadable, InvalidImage, MissingSignature, AmbiguousSignature, InvalidTarget };
struct BuildEvidence {
    BuildCheck check{BuildCheck::Unreadable};
    std::uint32_t build{}, bundledAddressValue{};
    std::uintptr_t address{};
};
// cs2-dumper's engine2 build-number store, resolved independently of dump RVAs.
// https://github.com/a2x/cs2-dumper/blob/main/src/analysis/offsets.rs
inline constexpr auto BuildSignature = "89 05 ? ? ? ? 48 8D 0D ? ? ? ? FF 15 ? ? ? ? 48 8B 0D";
inline BuildEvidence VerifyEngineBuild(const Memory& memory, std::uintptr_t base, std::size_t size) noexcept {
    BuildEvidence result;
    try {
        const auto inside = [size](std::size_t offset,std::size_t length) {
            return offset<=size && length<=size-offset;
        };
        if (!base || base>(std::numeric_limits<std::uintptr_t>::max)()-size) return result;
        memory.Field(base,offsets::BuildNumber,result.bundledAddressValue);
        IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
        if (!inside(0,sizeof(dos)) || !memory.Read(base,dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0 ||
            !inside(dos.e_lfanew,sizeof(nt)) || !memory.Field(base,dos.e_lfanew,nt) ||
            nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.FileHeader.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER64) ||
            !nt.FileHeader.NumberOfSections || nt.FileHeader.NumberOfSections>96) {
            result.check=BuildCheck::InvalidImage; return result;
        }
        const auto table=static_cast<std::size_t>(dos.e_lfanew)+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+nt.FileHeader.SizeOfOptionalHeader;
        const auto pattern=scan::ParsePattern(BuildSignature);
        std::uint32_t matches{};
        std::uintptr_t instruction{};
        std::array<std::uint8_t,22> instructionBytes{};
        for (unsigned i=0;i<nt.FileHeader.NumberOfSections;++i) {
            IMAGE_SECTION_HEADER section{};
            const auto position=table+i*sizeof(section);
            if (!inside(position,sizeof(section)) || !memory.Field(base,position,section)) return result;
            if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            const auto length=section.Misc.VirtualSize;
            if (!inside(section.VirtualAddress,length) || length>64*1024*1024) { result.check=BuildCheck::InvalidImage; return result; }
            if (!length) continue;
            std::vector<std::uint8_t> bytes(length);
            if (!memory.read || !memory.read(memory.context,base+section.VirtualAddress,bytes.data(),bytes.size())) return result;
            scan::ForEachMatch(bytes,*pattern,[&](std::size_t offset) {
                if (++matches==1) {
                    instruction=base+section.VirtualAddress+offset;
                    std::copy_n(bytes.data()+offset,instructionBytes.size(),instructionBytes.data());
                }
            });
        }
        if (matches!=1) { result.check=matches ? BuildCheck::AmbiguousSignature : BuildCheck::MissingSignature; return result; }
        const auto target=scan::ResolveRelative32(instruction,instructionBytes,2,6);
        if (!target || *target<base || !inside(*target-base,sizeof(result.build)) ||
            !memory.Read(*target,result.build) || !result.build) {
            result.build=0; result.check=BuildCheck::InvalidTarget; return result;
        }
        result.address=*target; result.check=BuildCheck::Verified;
    } catch (...) { result={}; }
    return result;
}
}
