#include "build_verification.hpp"
#include <cstdio>
using namespace awareness::cs2;
int main() {
    const std::uintptr_t base=0x10000000;
    std::vector<unsigned char> bytes(0x700000);
    const auto put=[&](std::size_t offset,const auto& value) { std::memcpy(bytes.data()+offset,&value,sizeof(value)); };
    IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=0x100;put(0,dos);
    IMAGE_NT_HEADERS64 nt{};nt.Signature=IMAGE_NT_SIGNATURE;nt.FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);nt.FileHeader.NumberOfSections=1;
    nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;put(0x100,nt);
    IMAGE_SECTION_HEADER section{};section.VirtualAddress=0x1000;section.Misc.VirtualSize=0x2000;section.Characteristics=IMAGE_SCN_MEM_EXECUTE;
    put(0x100+sizeof(nt),section);
    const std::array<std::uint8_t,22> signature{0x89,0x05,0,0,0,0,0x48,0x8d,0x0d,0,0,0,0,0xff,0x15,0,0,0,0,0x48,0x8b,0x0d};
    put(0x1100,signature);put(0x1102,std::int32_t{0x5000-0x1106});put(0x5000,std::uint32_t{14181});
    put(offsets::BuildNumber,std::uint32_t{655});
    const Memory memory{&bytes,[](void* context,std::uintptr_t address,void* target,std::size_t size) noexcept {
        const auto& data=*static_cast<std::vector<unsigned char>*>(context);
        if(address<0x10000000 || address-0x10000000>data.size() || size>data.size()-(address-0x10000000))return false;
        std::memcpy(target,data.data()+address-0x10000000,size);return true;
    }};
    int failures{};
    const auto check=[&](bool ok,const char* why){if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}};
    auto result=VerifyEngineBuild(memory,base,bytes.size());
    check(result.check==BuildCheck::Verified && result.build==14181 && result.bundledAddressValue==655 && result.address==base+0x5000,
          "stale value 655 is never treated as the verified game build");
    put(0x1200,signature);check(VerifyEngineBuild(memory,base,bytes.size()).check==BuildCheck::AmbiguousSignature,"duplicate build signatures rejected");
    bytes[0x1200]=0;bytes[0x1100]=0;
    check(VerifyEngineBuild(memory,base,bytes.size()).check==BuildCheck::MissingSignature,"missing signature does not trust bundled offset");
    bytes[0x1100]=0x89;put(0x1102,std::int32_t{-0x4000});
    check(VerifyEngineBuild(memory,base,bytes.size()).check==BuildCheck::InvalidTarget,"out-of-module build address rejected");
    bytes[0]=0;check(VerifyEngineBuild(memory,base,bytes.size()).check==BuildCheck::InvalidImage,"invalid PE header rejected");
    std::printf("5 build-verification checks; %d failures\n",failures);return failures?1:0;
}
