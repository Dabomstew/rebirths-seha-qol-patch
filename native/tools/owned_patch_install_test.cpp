#include "owned_patch_install.hpp"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace rebirths {
void Log(const char*, ...) noexcept {}
}

namespace {
using namespace rebirths;
using namespace rebirths::owned_patch;
constexpr size_t ImageSize = 0x5000;
constexpr DWORD InstallError = 0xe000000d, RestoreError = 0xe000000b;
unsigned char* image;
unsigned checks = 0, calls = 0, applies = 0, restores = 0, installSleeps = 0, restoreSleeps = 0;
int failSite = -1;
unsigned transientFailures = 0, recoveryFailures = 0;
bool writeOnFailure = false, corruptOnFailure = false, failPresentation = false;
bool originalWritable = false, installedWritable = false, restoredWritable = false;
bool restoreReturnsFalse = false, failQuery = false;
std::array<uint32_t, 2048> recoveryOrder{};
unsigned recoveryOrderCount = 0;
std::array<unsigned char, 6> presentationExpected{0xff, 0x15, 0, 0, 0, 0};
std::array<unsigned char, 6> presentationReplacement{0xe8, 1, 2, 3, 4, 0x90};

void Protect(DWORD protection) {
    DWORD old;
    assert(VirtualProtect(image, ImageSize, protection, &old));
}

void Write(uint32_t rva, const unsigned char* data, size_t size, bool writable) {
    Protect(PAGE_EXECUTE_READWRITE);
    std::memcpy(image + rva, data, size);
    if (!writable) Protect(PAGE_EXECUTE_READ);
}

SIZE_T WINAPI Query(LPCVOID address, PMEMORY_BASIC_INFORMATION memory, SIZE_T size) {
    return failQuery ? 0 : VirtualQuery(address, memory, size);
}

void WINAPI Pause(DWORD duration) {
    if (duration == 25) ++installSleeps;
    else { assert(duration == 1); ++restoreSleeps; }
}

bool Apply(uint32_t rva, const unsigned char* replacement, size_t size, bool fail) noexcept {
    ++applies;
    if (!fail || writeOnFailure) Write(rva, replacement, size, installedWritable);
    if (fail && corruptOnFailure) {
        const unsigned char foreign = 0xcc;
        Write(rva, &foreign, 1, false);
    }
    if (fail && originalWritable) Protect(PAGE_EXECUTE_READWRITE);
    if (fail) SetLastError(InstallError);
    return !fail;
}

bool Calls(const Context&, const CallSite* sites, size_t count) noexcept {
    assert(count == 1); // No accidental batching across the original boundary.
    ++calls;
    const auto& site = sites[0];
    bool fail = static_cast<int>(site.rva) == failSite;
    if (transientFailures) { --transientFailures; fail = true; }
    const auto replacement = CallTo(reinterpret_cast<uintptr_t>(image + site.rva),
                                    reinterpret_cast<uintptr_t>(site.replacement));
    return Apply(site.rva, replacement.data(), replacement.size(), fail);
}

bool Bytes(const Context&, uint32_t rva, const unsigned char* expected,
           const unsigned char* replacement, size_t size) noexcept {
    if (rva == 0x3000 && expected[0] == 0xff)
        return Apply(rva, replacement, size, failPresentation);
    ++restores;
    assert(recoveryOrderCount < recoveryOrder.size());
    recoveryOrder[recoveryOrderCount++] = rva;
    if (recoveryFailures) { --recoveryFailures; SetLastError(RestoreError); return false; }
    if (std::memcmp(image + rva, expected, size)) { SetLastError(RestoreError); return false; }
    Write(rva, replacement, size, restoredWritable);
    if (restoreReturnsFalse) { SetLastError(RestoreError); return false; }
    return true;
}

const PatchOps Ops{Calls, Bytes};
const Platform TestPlatform{Query, Pause};

template <size_t Count>
std::array<PreparedCall, Count> Prepare() {
    calls = applies = restores = installSleeps = restoreSleeps = recoveryOrderCount = 0;
    failSite = -1;
    transientFailures = recoveryFailures = 0;
    writeOnFailure = corruptOnFailure = failPresentation = false;
    originalWritable = installedWritable = restoredWritable = restoreReturnsFalse = failQuery = false;
    Protect(PAGE_EXECUTE_READWRITE);
    std::memset(image, 0, ImageSize);
    std::array<PreparedCall, Count> sites{};
    for (size_t i = 0; i < Count; ++i) {
        const uint32_t rva = 0x1000 + static_cast<uint32_t>(i) * 16;
        sites[i].site = {rva, CallTo(rva, 0x2000), image + 0x2800};
        std::memcpy(image + rva, sites[i].site.expected.data(), 5);
    }
    std::memcpy(image + 0x3000, presentationExpected.data(), presentationExpected.size());
    Protect(PAGE_EXECUTE_READ);
    SetLastError(0);
    return sites;
}

void Check(PatchResult result, PatchOutcome outcome, DWORD error) {
    assert(result.outcome == outcome && result.error == error);
    assert(result.Succeeded() == (outcome == PatchOutcome::Installed && !error));
    assert(result.MayRedirect() == (outcome == PatchOutcome::Installed || outcome == PatchOutcome::IncompleteRecovery));
    assert(GetLastError() == error);
    ++checks;
}

template <size_t Count>
PatchResult Run(const Context& context, std::array<PreparedCall, Count>& sites, const BytePatch* byte = nullptr,
                RemovalOrder order = RemovalOrder::SwapLast) {
    return Install(context, sites.data(), sites.size(), Ops, "contract", byte, byte ? 1 : 0, order, TestPlatform);
}
}

int main() {
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, ImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    assert(image);
    GameSpec spec{};
    spec.id = GameId::Rebirth1;
    Context context(reinterpret_cast<HMODULE>(image), spec, L"", L"");
    const BytePatch presentation{0x3000, presentationExpected.data(), presentationReplacement.data(), 6, "blackout "};

    auto sites = Prepare<3>();
    Check(Run(context, sites), PatchOutcome::Installed, 0);
    assert(calls == 3 && restores == 0 && !installSleeps && !restoreSleeps);
    for (const auto& site : sites) {
        const auto call = CallTo(reinterpret_cast<uintptr_t>(image + site.site.rva), reinterpret_cast<uintptr_t>(site.site.replacement));
        assert(IsOriginalExecutable(reinterpret_cast<uintptr_t>(image + site.site.rva), call));
    }
    // Original sequences above the primitive's sixteen-site capacity still use
    // exactly one transaction per CALL, with presentation last.
    auto many = Prepare<26>();
    Check(Run(context, many, &presentation), PatchOutcome::Installed, 0);
    assert(calls == 26 && applies == 27 && restores == 0);

    sites = Prepare<3>(); transientFailures = 2;
    Check(Run(context, sites), PatchOutcome::Installed, 0);
    assert(calls == 5 && installSleeps == 2);
    for (const auto order : {RemovalOrder::SwapLast, RemovalOrder::Stable}) {
        sites = Prepare<3>(); failSite = sites[2].site.rva; writeOnFailure = true; recoveryFailures = 2;
        Check(Run(context, sites, nullptr, order), PatchOutcome::Restored, InstallError);
        assert(recoveryOrder[3] == sites[order == RemovalOrder::Stable ? 2 : 1].site.rva);
        assert(recoveryOrder[4] == sites[order == RemovalOrder::Stable ? 1 : 2].site.rva);
    }
    for (const auto order : {RemovalOrder::SwapLast, RemovalOrder::Stable}) {
        for (const unsigned index : {0u, 1u, 2u}) {
            sites = Prepare<3>(); failSite = sites[index].site.rva;
            Check(Run(context, sites, nullptr, order), index ? PatchOutcome::Restored : PatchOutcome::Rejected, InstallError);
            assert(calls == index + 3 && restores == index && installSleeps == 2);
            for (unsigned i = 0; i < index; ++i) assert(recoveryOrder[i] == sites[index - i - 1].site.rva);
            sites = Prepare<3>(); failSite = sites[index].site.rva; writeOnFailure = true;
            Check(Run(context, sites, nullptr, order), PatchOutcome::Restored, InstallError);
            assert(calls == index + 1 && restores == index + 1 && !installSleeps);
        }
    }
    sites = Prepare<3>(); failSite = sites[2].site.rva; writeOnFailure = true; recoveryFailures = 2;
    Check(Run(context, sites), PatchOutcome::Restored, InstallError);
    assert(restores == 5 && restoreSleeps == 1);
    sites = Prepare<3>(); failSite = sites[0].site.rva; writeOnFailure = true; recoveryFailures = 100;
    Check(Run(context, sites), PatchOutcome::IncompleteRecovery, InstallError);
    assert(restores == 20 && restoreSleeps == 20);
    sites = Prepare<3>(); failSite = sites[0].site.rva; writeOnFailure = true; restoredWritable = true;
    Check(Run(context, sites), PatchOutcome::IncompleteRecovery, InstallError);
    assert(restores == 20 && restoreSleeps == 20);
    sites = Prepare<3>(); failSite = sites[0].site.rva; originalWritable = true;
    Check(Run(context, sites), PatchOutcome::IncompleteRecovery, InstallError);
    assert(!restores);
    sites = Prepare<3>(); installedWritable = true;
    Check(Run(context, sites), PatchOutcome::Restored, ERROR_INVALID_DATA);
    assert(calls == 1 && restores == 1);
    sites = Prepare<3>(); failSite = sites[0].site.rva; corruptOnFailure = true;
    Check(Run(context, sites), PatchOutcome::IncompleteRecovery, InstallError);
    sites = Prepare<3>(); failSite = sites[0].site.rva; writeOnFailure = true; restoreReturnsFalse = true;
    Check(Run(context, sites), PatchOutcome::Restored, InstallError);

    for (const bool postWrite : {false, true}) {
        sites = Prepare<3>(); failPresentation = true; writeOnFailure = postWrite;
        Check(Run(context, sites, &presentation), PatchOutcome::Restored, InstallError);
        assert(restores == 3 + static_cast<unsigned>(postWrite));
        if (postWrite) assert(recoveryOrder[0] == presentation.rva);
    }
    sites = Prepare<3>(); failPresentation = true; writeOnFailure = true; recoveryFailures = 1;
    Check(Run(context, sites, &presentation), PatchOutcome::Restored, InstallError);
    assert(recoveryOrder[0] == presentation.rva && recoveryOrder[4] == presentation.rva && restores == 5);
    sites = Prepare<3>(); failPresentation = true; writeOnFailure = true; recoveryFailures = 100;
    Check(Run(context, sites, &presentation), PatchOutcome::IncompleteRecovery, InstallError);
    assert(restores == 80);

    sites = Prepare<3>(); const unsigned char mismatch = 0xcc; Write(sites[2].site.rva, &mismatch, 1, false);
    Check(Run(context, sites), PatchOutcome::Rejected, ERROR_INVALID_DATA); assert(!calls);
    sites = Prepare<3>(); Protect(PAGE_EXECUTE_READWRITE);
    Check(Run(context, sites), PatchOutcome::Rejected, ERROR_INVALID_DATA); assert(!calls);
    sites = Prepare<3>(); failQuery = true;
    Check(Run(context, sites), PatchOutcome::Rejected, ERROR_INVALID_DATA); assert(!calls);
    sites = Prepare<3>(); sites[2].site.rva = sites[0].site.rva;
    sites[2].site.expected = sites[0].site.expected;
    Check(Run(context, sites), PatchOutcome::Rejected, ERROR_INVALID_PARAMETER); assert(!calls);
    sites = Prepare<3>();
    auto badByte = presentation; badByte.size = 17;
    Check(Run(context, sites, &badByte), PatchOutcome::Rejected, ERROR_INVALID_DATA); assert(!calls);
    badByte = presentation; badByte.expected = nullptr;
    Check(Run(context, sites, &badByte), PatchOutcome::Rejected, ERROR_INVALID_DATA); assert(!calls);
    auto missingOps = Ops; missingOps.retargetCalls = nullptr;
    Check(Install(context, sites.data(), 3, missingOps, "contract"), PatchOutcome::Rejected, ERROR_INVALID_PARAMETER);
    Check(Install(context, nullptr, 1, Ops, "contract"), PatchOutcome::Rejected, ERROR_INVALID_PARAMETER);
    Check(Install(context, sites.data(), 65, Ops, "contract"), PatchOutcome::Rejected, ERROR_INVALID_PARAMETER);
    Check(Install(context, sites.data(), 3, Ops, "contract", &presentation, 62), PatchOutcome::Rejected, ERROR_INVALID_PARAMETER);
    Check(Install(context, nullptr, 0, Ops, "contract"), PatchOutcome::Rejected, ERROR_INVALID_PARAMETER);
    missingOps = Ops; missingOps.retargetBytes = nullptr;
    Check(Install(context, sites.data(), 3, missingOps, "contract"), PatchOutcome::Rejected, ERROR_INVALID_PARAMETER);
    auto missingPlatform = TestPlatform; missingPlatform.query = nullptr;
    Check(Install(context, sites.data(), 3, Ops, "contract", nullptr, 0, RemovalOrder::Stable, missingPlatform),
          PatchOutcome::Rejected, ERROR_INVALID_PARAMETER);
    sites = Prepare<3>();
    Check(Install(context, nullptr, 0, Ops, "contract", &presentation, 1, RemovalOrder::Stable, TestPlatform),
          PatchOutcome::Installed, 0);
    assert(!calls && applies == 1);

    // A site spanning two regions audits both pages before reading any bytes.
    sites = Prepare<3>(); auto crossing = Prepare<1>();
    crossing[0].site.rva = 0x1ffd;
    Write(crossing[0].site.rva, crossing[0].site.expected.data(), 5, false);
    DWORD old;
    assert(VirtualProtect(image + 0x2000, 0x1000, PAGE_NOACCESS, &old));
    Check(Run(context, crossing), PatchOutcome::Rejected, ERROR_INVALID_DATA); assert(!calls);
    assert(VirtualProtect(image + 0x2000, 0x1000, PAGE_EXECUTE_READ, &old));
    Check(Run(context, crossing), PatchOutcome::Installed, 0);

    assert(VirtualFree(image, 0, MEM_RELEASE));
    std::printf("%u owned installation contracts passed (CALL/byte order, retry, recovery, ownership and RX audit)\n", checks);
}
