#include <awareness/PatternScan.hpp>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
using namespace awareness::scan;
namespace {
int checks{}, failures{};
void Check(bool ok, const char* what) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", what); }
}
std::vector<std::size_t> Matches(std::span<const std::uint8_t> bytes, const Pattern& pattern) {
    std::vector<std::size_t> found;
    ForEachMatch(bytes, pattern, [&](std::size_t offset) { found.push_back(offset); });
    return found;
}
}
int main() {
    const auto entity = ParsePattern(EntityListSignature);
    const auto view = ParsePattern(ViewMatrixSignature);
    Check(entity && entity->size() == 11, "entity signature parses");
    Check(view && view->size() == 18, "view signature parses");
    const auto wildcards = ParsePattern(" \t48 8b ?? ?\n");
    Check(wildcards && (*wildcards)[1] == 0x8B && (*wildcards)[2] == -1 && (*wildcards)[3] == -1,
        "case, whitespace and both wildcard spellings");
    Check(!ParsePattern(""), "empty signature rejected");
    Check(!ParsePattern("? ??"), "all-wildcard signature rejected");
    Check(!ParsePattern("48 8G"), "invalid hex rejected");
    Check(!ParsePattern("48 8"), "half byte rejected");
    Check(!ParsePattern("48 8?"), "partial wildcard rejected");
    Check(!ParsePattern("488B"), "missing byte separator rejected");
    const std::array<std::uint8_t, 14> data{0x48,0x8B,0x0D,1,2,3,4,0x48,0x8D,0x94,0x24,0,0,0};
    Check(Matches(data, *entity) == std::vector<std::size_t>{0}, "match at beginning");
    std::vector<std::uint8_t> end(7,0);
    end.insert(end.end(), data.begin(), data.begin()+11);
    Check(Matches(end, *entity) == std::vector<std::size_t>{7}, "match at last legal position");
    Check(Matches(std::span(data).first(10), *entity).empty(), "truncated signature is not read past its end");
    Check(Matches(data, *view).empty(), "longer signature rejected");
    const std::array<std::uint8_t,3> repeated{0x48,0x48,0x48};
    Check(Matches(repeated, *ParsePattern("48 48")) == std::vector<std::size_t>{0,1}, "overlapping matches reported");
    Check(Matches(repeated, *ParsePattern("? 48")) == std::vector<std::size_t>{0,1}, "leading wildcard retains overlapping and last-position matches");
    Check(Matches(data, *ParsePattern("? ? 24")) == std::vector<std::size_t>{8}, "literal anchor after multiple wildcards");
    Check(Matches(data, *ParsePattern("FE ED")).empty(), "no match");
    std::array<std::uint8_t,7> mov{0x48,0x8B,0x0D,0x20,0,0,0};
    Check(ResolveRelative32(0x1000,mov,3,7) == 0x1027, "positive signed disp32");
    mov[3]=0xE0; mov[4]=mov[5]=mov[6]=0xFF;
    Check(ResolveRelative32(0x1000,mov,3,7) == 0xFE7, "negative signed disp32");
    mov[3]=mov[4]=mov[5]=mov[6]=0;
    Check(ResolveRelative32(0x1000,mov,3,7) == 0x1007, "zero displacement uses next instruction");
    mov[6]=0x80;
    Check(ResolveRelative32(0x80000000ull,mov,3,7) == 7, "INT32_MIN sign-extends correctly");
    Check(!ResolveRelative32(0,mov,3,7), "negative target underflow rejected");
    mov[3]=mov[4]=mov[5]=0xFF; mov[6]=0x7F;
    Check(ResolveRelative32(0x100000000ull,mov,3,7) == 0x180000006ull, "64-bit result is not truncated");
    Check(!ResolveRelative32((std::numeric_limits<std::uintptr_t>::max)()-10,mov,3,7), "positive target overflow rejected");
    Check(!ResolveRelative32((std::numeric_limits<std::uintptr_t>::max)()-2,mov,3,7), "next-instruction overflow rejected");
    Check(!ResolveRelative32(0x1000,std::span(mov).first(6),3,7), "short instruction rejected");
    Check(!ResolveRelative32(0x1000,mov,4,7), "displacement past instruction rejected");
    Check(IsRipRelative7(mov,0x8B), "MOV RIP operand accepted");
    mov[1]=0x8D; mov[2]=0x35;
    Check(IsRipRelative7(mov,0x8D), "LEA with another destination register accepted");
    mov[2]=0x45;
    Check(!IsRipRelative7(mov,0x8D), "base-register LEA rejected");
    mov[2]=0x04;
    Check(!IsRipRelative7(mov,0x8D), "SIB LEA rejected");
    mov[2]=0x0D; mov[0]=0x40;
    Check(!IsRipRelative7(mov,0x8D), "non-64-bit operand rejected");
    std::printf("%d pattern/relative-address checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
