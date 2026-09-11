#include <awareness/AddressDiscovery.hpp>
#include "scan_fixture.hpp"
#include <cstdio>
using namespace awareness;
namespace {
int checks{}, failures{};
void Check(bool ok, const char* what) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", what); }
}
HRESULT Discover(DiscoveredAddresses& result) { return AwarenessFindAddresses(L"AwarenessScanFixture.dll", &result); }
}
int main() {
    FixtureAddresses expected;
    ConfigureScanFixture(Normal,&expected);
    DiscoveredAddresses result;
    Check(Discover(result) == S_OK, "scan loaded PE module through DLL export");
    Check(result.entityMatches == 1 && result.viewMatches == 1, "cross-chunk signatures counted exactly once");
    Check(result.entityListInstruction == expected.entityInstruction && result.viewMatrixInstruction == expected.viewInstruction,
        "matched instruction addresses");
    Check(result.entityListSlot == expected.entitySlot && result.entityList == expected.entityList,
        "positive MOV displacement and pointer indirection");
    Check(result.viewMatrix == expected.matrix, "negative LEA displacement without pointer indirection");
    DWORD originalProtection{};
    const auto page = reinterpret_cast<void*>(expected.protectionPage);
    Check(VirtualProtect(page,4096,PAGE_EXECUTE_READ,&originalProtection) != FALSE, "set different readable page protection");
    Check(Discover(result) == S_OK, "scan traverses separate readable protection regions");
    DWORD ignored{};
    Check(VirtualProtect(page,4096,PAGE_NOACCESS,&ignored) != FALSE, "make a fixture page inaccessible");
    Check(Discover(result) == HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY), "unreadable executable range returns incomplete scan");
    Check(result.entityStatus == ScanStatus::IncompleteScan && result.entityList == 0 && result.viewMatrix == 0,
        "incomplete scan publishes no usable addresses");
    Check(VirtualProtect(page,4096,originalProtection,&ignored) != FALSE, "restore fixture page protection");
    ConfigureScanFixture(NoMatches,&expected);
    Check(Discover(result) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && result.entityMatches == 0 &&
        result.viewMatches == 0 && result.entityList == 0, "missing signatures clear prior results");
    ConfigureScanFixture(DuplicateEntity,&expected);
    Check(Discover(result) == HRESULT_FROM_WIN32(ERROR_DUP_NAME) && result.entityStatus == ScanStatus::Ambiguous &&
        result.entityList == 0 && result.entityMatches == 2, "ambiguous entity signature rejected");
    ConfigureScanFixture(DuplicateView,&expected);
    Check(Discover(result) == HRESULT_FROM_WIN32(ERROR_DUP_NAME) && result.viewStatus == ScanStatus::Ambiguous &&
        result.viewMatrix == 0, "ambiguous view signature rejected");
    ConfigureScanFixture(BadLea,&expected);
    Check(Discover(result) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA) && result.viewStatus == ScanStatus::NotRipRelative &&
        result.viewMatrix == 0, "wildcarded non-RIP LEA rejected");
    ConfigureScanFixture(NullList,&expected);
    Check(Discover(result) == HRESULT_FROM_WIN32(ERROR_NOT_READY) && result.entityStatus == ScanStatus::NullEntityList &&
        result.entityListSlot == expected.entitySlot, "null list reports not ready and retains its slot address");
    Check(AwarenessFindAddresses(L"awareness-absent-module-17.dll",&result) == HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND),
        "missing module is not loaded or guessed");
    Check(AwarenessFindAddresses(L"AwarenessScanFixture.dll",nullptr) == E_INVALIDARG, "null output rejected");
    result.version = 999;
    Check(Discover(result) == E_INVALIDARG, "API version checked");
    Check(SUCCEEDED(AwarenessShutdown()), "finalize attach bootstrap without initializing graphics");
    std::printf("%d Windows scanner integration checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
