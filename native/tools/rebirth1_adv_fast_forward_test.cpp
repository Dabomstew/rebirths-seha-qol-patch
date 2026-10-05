#include "adv_test_contracts.hpp"
#include "synthetic_image.hpp"
#include "owned_patch_install.hpp"

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
std::string capturedLog;
}
namespace rebirths {
void Log(const char* format, ...) noexcept {
    char line[512]{};
    va_list args;
    va_start(args, format);
    vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
    va_end(args);
    capturedLog.append(line).append("\n");
}
}

namespace {
constexpr size_t ImageSize = 0x460000;
unsigned cgPollCalls = 0;
uintptr_t cgPollObject = 0;
unsigned delayCalls = 0;
unsigned backgroundCalls = 0;
unsigned trackCalls = 0;
unsigned setupCalls = 0;
unsigned createCalls = 0;
volatile uintptr_t pollThis = 0;
volatile LONG pollCalls = 0;
volatile unsigned char pollPending = 1;
volatile unsigned lookupChild = 0;
int manager[4]{};
int list[4]{};
int battle[20]{};

uint32_t __fastcall OriginalCgPoll(uintptr_t object) {
    ++cgPollCalls; cgPollObject = object;
    // Full-width resource pending bit plus remaining non-pan track status.
    const auto* bytes = reinterpret_cast<const unsigned char*>(object);
    const auto* zoom = reinterpret_cast<const float*>(object + 0x60);
    return (bytes[0x664] == 5 ? 0 : 0x2000) | (zoom[0] != zoom[1] ? 1 : 0);
}
int __cdecl OriginalDelay(int*, float*, float) { ++delayCalls; return 71; }
BOOL WINAPI OriginalSwapBuffers(HDC) { return TRUE; }
int __cdecl OriginalBackground(uint32_t*) { ++backgroundCalls; return 72; }
void __cdecl OriginalTrack(float*) { ++trackCalls; }
void __cdecl OriginalScaledTrack(float*, float) { ++trackCalls; }
void __cdecl OriginalSetup(uint32_t, uint32_t, int, int, uint32_t) { ++setupCalls; }
void __cdecl OriginalCreate(uint32_t, uint32_t, uint32_t, uint32_t) { ++createCalls; }
int* __cdecl CharacterManager() { return manager; }
int pendingResult = 0;
unsigned retainedCalls = 0;
int __cdecl PendingAggregate(uint32_t mask, int wait, uint32_t* output) {
    assert(mask == 0xffffffff && wait == 1); *output = 0x2f; ++retainedCalls; return pendingResult;
}
int __cdecl PendingCg(uintptr_t object, float duration) {
    assert(object == 0x1234 && duration == 0); ++retainedCalls; return pendingResult;
}
int __cdecl PendingCharacter(uint32_t id, int arg, int expression, float duration) {
    assert(id == 5 && arg == 1 && expression == 2 && duration == 0); ++retainedCalls; return pendingResult;
}
int __cdecl PendingExit(int* state, uint32_t id, int arg, float duration) {
    assert(id == 5 && arg == -1 && duration == 0); ++*state; ++retainedCalls; return pendingResult;
}
int __cdecl StagedCleanup(int* state, int operation, int arg3, int arg4, uint32_t arg5, uint32_t arg6) {
    assert(arg3 == 3 && arg4 == 4 && arg5 == 5 && arg6 == 6); ++retainedCalls;
    if (*state == 0) { *state = 1; return 0; }
    assert(operation < 0); *state = 2; return 1;
}
int* __stdcall ResolveList(int) { return list; }
uintptr_t __cdecl BattleContext() { return reinterpret_cast<uintptr_t>(battle); }
__declspec(naked) uint8_t LookupCharacter() {
    __asm mov eax, dword ptr [esp + 0xc]
    __asm mov edx, dword ptr [lookupChild]
    __asm mov dword ptr [eax], edx
    __asm mov eax, 0x12340000
    __asm ret 0xc
}
__declspec(naked) uint8_t OriginalPoll() {
    __asm mov dword ptr [pollThis], ecx
    __asm inc dword ptr [pollCalls]
    __asm mov eax, 0xabcd0000
    __asm cmp byte ptr [pollPending], 0
    __asm je done
    __asm or eax, 1
    __asm done:
    __asm ret
}

unsigned char* fakeBase = nullptr;
unsigned operationCount = 0;
unsigned callFailureIndex = 0xffffffffu;
bool persistentCallFailure = false;
bool postWriteFailure = false;
bool failureLeavesWritableOriginal = false;
unsigned restoreFailures = 0;
bool leaveRestoreWritable = false;
unsigned presentPostWriteFailures = 0;
bool presentBeforeWriteFailure = false, presentWriteLeavesWritable = false, presentRestoreLeavesWritable = false;
unsigned presentRestoreFailures = 0;

bool SetPage(void* address, DWORD protection) {
    DWORD old = 0;
    return VirtualProtect(address, 5, protection, &old) != FALSE;
}
bool SetRange(void* address, size_t length, DWORD protection) { return fixture::Protect(address, length, protection); }
bool HasProtection(void* address, DWORD expected) {
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQuery(address, &info, sizeof(info)) == sizeof(info) && info.Protect == expected;
}
bool Mutate(uint32_t rva, const unsigned char* expected, const unsigned char* replacement, size_t count, bool restore) {
    auto* address = fakeBase + rva;
    if (std::memcmp(address, expected, count)) return false;
    if (!SetPage(address, PAGE_EXECUTE_READWRITE)) return false;
    std::memcpy(address, replacement, count);
    if (restore && leaveRestoreWritable) return true;
    return SetPage(address, PAGE_EXECUTE_READ);
}
bool FakeRetargetCalls(const rebirths::Context&, const rebirths::CallSite* sites, size_t count) noexcept {
    assert(count == 1);
    const bool fail = operationCount == callFailureIndex ||
                      (persistentCallFailure && operationCount >= callFailureIndex);
    ++operationCount;
    if (fail && failureLeavesWritableOriginal) {
        // Model RetargetCalls reverting its instruction bytes but failing to
        // restore the page protection before it returns false.
        assert(SetPage(fakeBase + sites[0].rva, PAGE_EXECUTE_READWRITE));
        return false;
    }
    const bool write = !fail || postWriteFailure;
    // The replacement bytes are intentionally reconstructed from the wrapper
    // address, exactly as RetargetCalls does.
    if (write) {
        const auto replacement = fixture::CallBytes(reinterpret_cast<uintptr_t>(fakeBase + sites[0].rva),
                                                  reinterpret_cast<uintptr_t>(sites[0].replacement));
        if (!Mutate(sites[0].rva, sites[0].expected.data(), replacement.data(), replacement.size(), false)) return false;
    }
    return !fail;
}
bool FakeRetargetBytes(const rebirths::Context&, uint32_t rva, const unsigned char* expected,
                       const unsigned char* replacement, size_t count) noexcept {
    if (rva == 0x2970c8 && count == 6 && expected[0] == 0xff && presentBeforeWriteFailure) return false;
    if (rva == 0x2970c8 && count == 6 && expected[0] == 0xff && presentPostWriteFailures) {
        --presentPostWriteFailures;
        assert(Mutate(rva, expected, replacement, count, false));
        if (presentWriteLeavesWritable) assert(SetPage(fakeBase + rva, PAGE_EXECUTE_READWRITE));
        return false;
    }
    if (rva == 0x2970c8 && count == 6 && expected[0] == 0xe8) {
        if (presentRestoreFailures) { --presentRestoreFailures; return false; }
        if (presentRestoreLeavesWritable) {
            if (!Mutate(rva, expected, replacement, count, false)) return false;
            assert(SetPage(fakeBase + rva, PAGE_EXECUTE_READWRITE));
            return true;
        }
    }
    if (restoreFailures) { --restoreFailures; return false; }
    return Mutate(rva, expected, replacement, count, true);
}

const rebirths::AdvFastForwardPatchOps FakeOps{FakeRetargetCalls, FakeRetargetBytes};

void ResetOperations() {
    operationCount = 0;
    callFailureIndex = 0xffffffffu;
    persistentCallFailure = false;
    postWriteFailure = false;
    failureLeavesWritableOriginal = false;
    restoreFailures = 0;
    leaveRestoreWritable = false;
    presentPostWriteFailures = 0;
    presentBeforeWriteFailure = presentWriteLeavesWritable = presentRestoreLeavesWritable = false;
    presentRestoreFailures = 0;
    capturedLog.clear();
}
void PopulateSites() {
    const std::pair<uint32_t, uint32_t> sites[] = {
        {0xfa66, 0xf640}, {0x13f48, 0x1c8b0}, {0x127de, 0x12530}, {0x11f7b, 0x1c730}, {0x11f29, 0x1ca30}, {0x1494f, 0x2e020}, {0x15a3a, 0x15930}, {0xfa47, 0xf5c0}, {0x1eb1e, 0x1e800}, {0x11f43, 0x106d0}, {0x1d8ee, 0x1f9e0},
        {0x10039, 0x17c50}, {0x1ea7a, 0x39860}, {0x28567, 0x39900}, {0x28573, 0x39900},
        {0x2857f, 0x39900}, {0x38f61, 0x39860}, {0x1e59ae, 0x1e76d0}, {0x1e5e5d, 0x1e76d0},
    };
    assert(SetRange(fakeBase, ImageSize, PAGE_EXECUTE_READWRITE));
    std::memset(fakeBase, 0, ImageSize);
    for (const auto [site, target] : sites) {
        const auto call = fixture::CallBytes(site, target);
        std::memcpy(fakeBase + site, call.data(), call.size());
    }
    const auto present = rebirths::AdvBlackoutExpectedCall(reinterpret_cast<uintptr_t>(fakeBase));
    std::memcpy(fakeBase + 0x2970c8, present.data(), present.size());
    *reinterpret_cast<BOOL (WINAPI**)(HDC)>(fakeBase + 0x32f04c) = OriginalSwapBuffers;
    assert(SetRange(fakeBase, ImageSize, PAGE_EXECUTE_READ));
}
bool IsOriginal(uint32_t site, uint32_t target) {
    const auto expected = fixture::CallBytes(site, target);
    return !std::memcmp(fakeBase + site, expected.data(), expected.size()) &&
           HasProtection(fakeBase + site, PAGE_EXECUTE_READ);
}
bool IsOriginalPresentation() {
    const auto expected = rebirths::AdvBlackoutExpectedCall(reinterpret_cast<uintptr_t>(fakeBase));
    return rebirths::owned_patch::IsOriginalExecutable(reinterpret_cast<uintptr_t>(fakeBase + 0x2970c8), expected);
}
void AssertAllOriginalCalls() {
    for (const auto [site, target] : std::initializer_list<std::pair<uint32_t, uint32_t>>{
            {0xfa66, 0xf640}, {0x13f48, 0x1c8b0}, {0x127de, 0x12530}, {0x11f7b, 0x1c730}, {0x11f29, 0x1ca30}, {0x1494f, 0x2e020}, {0x15a3a, 0x15930}, {0xfa47, 0xf5c0}, {0x1eb1e, 0x1e800}, {0x11f43, 0x106d0}, {0x1d8ee, 0x1f9e0},
            {0x10039, 0x17c50}, {0x1ea7a, 0x39860}, {0x28567, 0x39900}, {0x28573, 0x39900},
            {0x2857f, 0x39900}, {0x38f61, 0x39860}, {0x1e59ae, 0x1e76d0}, {0x1e5e5d, 0x1e76d0}}) assert(IsOriginal(site, target));
}
void BindOriginals() {
    rebirths::testing::rebirth1_fast_forward::BindCgMotionPoll(OriginalCgPoll);
    rebirths::testing::rebirth1_fast_forward::BindAggregate(PendingAggregate);
    rebirths::testing::rebirth1_fast_forward::BindCgReady(PendingCg);
    rebirths::testing::rebirth1_fast_forward::BindCharacterReady(PendingCharacter);
    rebirths::testing::rebirth1_fast_forward::BindCharacterExit(PendingExit);
    rebirths::testing::rebirth1_fast_forward::BindTalkCleanup(StagedCleanup);
    rebirths::testing::rebirth1_fast_forward::BindDelay(OriginalDelay);
    rebirths::testing::rebirth1_fast_forward::BindBackgroundLoad(OriginalBackground);
    rebirths::testing::rebirth1_fast_forward::BindTrack(OriginalTrack);
    rebirths::testing::rebirth1_fast_forward::BindScaledTrack(OriginalScaledTrack);
    rebirths::testing::rebirth1_fast_forward::BindSetup(OriginalSetup);
    rebirths::testing::rebirth1_fast_forward::BindCharacterManager(CharacterManager);
    rebirths::testing::rebirth1_fast_forward::BindResolveCharacterList(ResolveList);
    rebirths::testing::rebirth1_fast_forward::BindLookupCharacter(reinterpret_cast<rebirths::testing::rebirth1_fast_forward::LookupFn>(LookupCharacter));
    rebirths::testing::rebirth1_fast_forward::BindBattleContext(BattleContext);
    rebirths::testing::rebirth1_fast_forward::BindAdvBinCreate(OriginalCreate);
    rebirths::testing::rebirth1_fast_forward::BindAdvSePoll(reinterpret_cast<rebirths::testing::rebirth1_fast_forward::AdvSePollFn>(OriginalPoll));
}
}

int main() {
    fixture::Image image(ImageSize); fakeBase = image.data();
    const rebirths::Context context{reinterpret_cast<HMODULE>(fakeBase), rebirths::GameSpecFor(rebirths::GameId::Rebirth1), L"", L""};
    const rebirths::Context unsupported{reinterpret_cast<HMODULE>(fakeBase), rebirths::GameSpecFor(rebirths::GameId::Rebirth2), L"", L""};
    assert(!rebirths::InstallAdvFastForward(unsupported, &FakeOps));
    assert(rebirths::testing::rebirth1_fast_forward::Active() == 0);

    // A wrong original byte must reject before a patch operation can write.
    PopulateSites(); ResetOperations();
    assert(SetPage(fakeBase + 0x1e5e5d, PAGE_EXECUTE_READWRITE)); fakeBase[0x1e5e5d] = 0x90;
    assert(SetPage(fakeBase + 0x1e5e5d, PAGE_EXECUTE_READ));
    assert(!rebirths::InstallAdvFastForward(context, &FakeOps));
    assert(operationCount == 0 && rebirths::testing::rebirth1_fast_forward::Active() == 0);

    // The complete transaction owns nineteen calls plus the six-byte present redirect,
    // and enables wrappers only after all twenty writes succeed.
    PopulateSites(); ResetOperations();
    assert(rebirths::InstallAdvFastForward(context, &FakeOps));
    assert(operationCount == 19 && rebirths::testing::rebirth1_fast_forward::Active() == 1);
    const auto presentPatch = rebirths::AdvBlackoutReplacement(reinterpret_cast<uintptr_t>(fakeBase + 0x2970c8));
    assert(!std::memcmp(fakeBase + 0x2970c8, presentPatch.data(), presentPatch.size()));

    BindOriginals();
    auto* adv = fakeBase + 0x450000;
    assert(SetPage(adv, PAGE_EXECUTE_READWRITE));
    assert(SetPage(fakeBase + 0x4591c4, PAGE_EXECUTE_READWRITE));
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x4591c4) = reinterpret_cast<uintptr_t>(adv);
    *reinterpret_cast<uint32_t*>(adv + 0x48) = 8;
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 0;
    // ADV state is ordinary mutable game data. Only CALL pages need RX checks.
    assert(SetPage(adv, PAGE_READWRITE));
    assert(SetPage(fakeBase + 0x4591c4, PAGE_EXECUTE_READ));
    // Exact original ABI/effects are retained, but pending presentation may advance.
    for (int guard = 0; guard < 4; ++guard) {
        rebirths::testing::rebirth1_fast_forward::SetActive(guard == 1 ? 0 : 1);
        *reinterpret_cast<uint32_t*>(adv + 0x48) = guard == 2 ? 0 : 8;
        *reinterpret_cast<uint32_t*>(adv + 0x10) = guard == 3 ? 1 : 0;
        const bool active = guard == 0;
        uint32_t output = 0; int stage = 0;
        const unsigned before = retainedCalls;
        assert(rebirths::testing::rebirth1_fast_forward::Aggregate(0xffffffff, 1, &output) == (active ? 1 : 0) && output == 0x2f);
        assert(rebirths::testing::rebirth1_fast_forward::CgReady(0x1234, 0) == (active ? 1 : 0));
        assert(rebirths::testing::rebirth1_fast_forward::CharacterReady(5, 1, 2, 0) == (active ? 1 : 0));
        assert(rebirths::testing::rebirth1_fast_forward::CharacterExit(&stage, 5, -1, 0) == (active ? 1 : 0) && stage == 1);
        stage = 0;
        assert(rebirths::testing::rebirth1_fast_forward::TalkCleanup(&stage, -1, 3, 4, 5, 6) == (active ? 1 : 0));
        assert(stage == (active ? 2 : 1) && retainedCalls == before + (active ? 6 : 5));
        assert(rebirths::testing::rebirth1_fast_forward::CgCommandManager() == (active ? nullptr : manager));
    }
    rebirths::testing::rebirth1_fast_forward::SetActive(true); *reinterpret_cast<uint32_t*>(adv + 0x48) = 8; *reinterpret_cast<uint32_t*>(adv + 0x10) = 0;
    pendingResult = -1; uint32_t output = 0;
    assert(rebirths::testing::rebirth1_fast_forward::Aggregate(0xffffffff, 1, &output) == -1);
    assert(rebirths::testing::rebirth1_fast_forward::CgReady(0x1234, 0) == -1);
    int positiveStage = 0;
    assert(rebirths::testing::rebirth1_fast_forward::TalkCleanup(&positiveStage, 1, 3, 4, 5, 6) == 0 && positiveStage == 1);
    pendingResult = 0;
    float* delayRecord = reinterpret_cast<float*>(adv + 0x18);
    delayRecord[0] = 0; delayRecord[1] = 4; delayRecord[2] = 2;
    int state = 0;
    assert(rebirths::testing::rebirth1_fast_forward::Delay(&state, delayRecord, 3.0f) == 71 && state == 1 && delayRecord[0] == 2 && delayCalls == 1);
    delayRecord[0] = 1; delayRecord[1] = 4; delayRecord[2] = 2;
    assert(rebirths::testing::rebirth1_fast_forward::Delay(&state, delayRecord, 3.0f) == 71 && delayRecord[0] == 4 && delayRecord[2] == 0 && delayCalls == 2);
    float otherRecord[3]{};
    assert(rebirths::testing::rebirth1_fast_forward::Delay(&state, otherRecord, 3.0f) == 71 && delayCalls == 3);
    state = 0;
    assert(rebirths::testing::rebirth1_fast_forward::Delay(&state, delayRecord, 0.0f) == 71 && delayCalls == 4);
    uint32_t background[70]{}; background[2] = 91; reinterpret_cast<unsigned char*>(background)[0x114] = 2;
    assert(rebirths::testing::rebirth1_fast_forward::BackgroundLoad(background) == 1 && background[2] == 0 && backgroundCalls == 0);
    background[2] = 91; background[3] = 42;
    assert(rebirths::testing::rebirth1_fast_forward::BackgroundLoad(background) == 72 && background[2] == 91 && background[3] == 42 && backgroundCalls == 1);
    float track[] = {1, 7, 2, 1}; rebirths::testing::rebirth1_fast_forward::FrameTrack(track);
    assert(track[0] == 7 && track[2] == 0 && track[3] == 7 && trackCalls == 0);
    float inactiveTrack[] = {1, 7, 0, 99}; rebirths::testing::rebirth1_fast_forward::FrameTrack(inactiveTrack);
    assert(inactiveTrack[0] == 1 && inactiveTrack[2] == 0 && inactiveTrack[3] == 99 && trackCalls == 0);
    alignas(float) unsigned char cg[0x668]{};
    auto* x = reinterpret_cast<float*>(cg + 0x30);
    auto* y = reinterpret_cast<float*>(cg + 0x40);
    auto* zoom = reinterpret_cast<float*>(cg + 0x60);
    x[0] = 0; x[1] = -1240; x[2] = -1; x[3] = 0;
    y[0] = 0; y[1] = 100; y[2] = 1; y[3] = 0;
    zoom[1] = 5; zoom[2] = 1;
    const auto cgAddress = reinterpret_cast<uintptr_t>(cg);
    assert(rebirths::testing::rebirth1_fast_forward::CgMotionPoll(cgAddress) == 0x2001);
    assert(cgPollCalls == 1 && cgPollObject == cgAddress);
    assert(x[0] == -1240 && x[2] == 0 && x[3] == -1240);
    assert(y[0] == 100 && y[2] == 0 && y[3] == 100);
    assert(zoom[0] == 0 && zoom[1] == 5 && zoom[2] == 1 && cg[0x664] == 0);
    cg[0x664] = 5; zoom[0] = zoom[1];
    assert(rebirths::testing::rebirth1_fast_forward::CgMotionPoll(cgAddress) == 0);
    for (unsigned guard = 0; guard < 3; ++guard) {
        rebirths::testing::rebirth1_fast_forward::SetActive(guard != 0);
        *reinterpret_cast<uint32_t*>(adv + 0x48) = guard == 1 ? 0 : 8;
        *reinterpret_cast<uint32_t*>(adv + 0x10) = guard == 2 ? 1 : 0;
        x[0] = 0; x[2] = -1;
        unsigned char before[sizeof(cg)]; std::memcpy(before, cg, sizeof(cg));
        assert(rebirths::testing::rebirth1_fast_forward::CgMotionPoll(cgAddress) == 0);
        assert(!std::memcmp(before, cg, sizeof(cg)));
    }
    rebirths::testing::rebirth1_fast_forward::SetActive(true);
    *reinterpret_cast<uint32_t*>(adv + 0x48) = 8;
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 0;
    x[2] = 0; x[3] = 99;
    assert(rebirths::testing::rebirth1_fast_forward::CgMotionPoll(cgAddress) == 0 && x[0] == 0 && x[3] == 99);
    manager[3] = 1; battle[0x48 / 4] = 0;
    rebirths::testing::rebirth1_fast_forward::CharacterSetup(2, 77, 1, 2, 3); // Lookup returns AL false with upper-EAX garbage; child is zero.
    assert(setupCalls == 0);
    lookupChild = 123;
    rebirths::testing::rebirth1_fast_forward::CharacterSetup(2, 77, 1, 2, 3); assert(setupCalls == 1);
    lookupChild = 0; battle[0x48 / 4] = 0x400;
    rebirths::testing::rebirth1_fast_forward::CharacterSetup(2, 77, 1, 2, 3); assert(setupCalls == 2);
    battle[0x48 / 4] = 0; manager[3] = 0;
    rebirths::testing::rebirth1_fast_forward::CharacterSetup(2, 77, 1, 2, 3); assert(setupCalls == 3);
    manager[3] = 1;
    rebirths::testing::rebirth1_fast_forward::AdvBinCreate(1, 2, 3, 0); assert(createCalls == 0);
    pollPending = 0; // Original returns AL=0 with upper EAX=0xABCD0000.
    assert(rebirths::testing::rebirth1_fast_forward::AdvSePoll(0x12345678, nullptr) == 0 && pollThis == 0x12345678 && pollCalls == 1);
    pollPending = 1;
    assert(rebirths::testing::rebirth1_fast_forward::AdvSePoll(0x12345678, nullptr) == 0 && pollThis == 0x12345678 && pollCalls == 2);

    // Disabled and non-mode-zero guards forward the exact original ABI.
    rebirths::testing::rebirth1_fast_forward::SetActive(false);
    assert(rebirths::testing::rebirth1_fast_forward::AdvSePoll(0x87654321, nullptr) == 1 && pollThis == 0x87654321 && pollCalls == 3);
    assert(rebirths::testing::rebirth1_fast_forward::Delay(&state, delayRecord, 3.0f) == 71 && delayCalls == 5);
    rebirths::testing::rebirth1_fast_forward::SetActive(true);
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 1;
    rebirths::testing::rebirth1_fast_forward::AdvBinCreate(1, 2, 3, 0); assert(createCalls == 1);
    assert(rebirths::testing::rebirth1_fast_forward::AdvSePoll(0x2468, nullptr) == 1 && pollThis == 0x2468 && pollCalls == 4);
    background[2] = 91; background[3] = 0;
    assert(rebirths::testing::rebirth1_fast_forward::BackgroundLoad(background) == 1 && background[2] == 0); // Exact 8076 behavior has no mode guard.
    rebirths::testing::rebirth1_fast_forward::FrameTrack(inactiveTrack); assert(trackCalls == 1);

    // A failed write at the new final CG call belongs to the same rollback.
    PopulateSites(); ResetOperations(); callFailureIndex = 12;
    persistentCallFailure = true; postWriteFailure = true;
    assert(!rebirths::InstallAdvFastForward(context, &FakeOps));
    assert(rebirths::testing::rebirth1_fast_forward::Active() == 0); AssertAllOriginalCalls();

    // A failure before a write restores prior sites and keeps wrappers disabled.
    PopulateSites(); ResetOperations(); callFailureIndex = 4; persistentCallFailure = true;
    assert(!rebirths::InstallAdvFastForward(context, &FakeOps));
    assert(rebirths::testing::rebirth1_fast_forward::Active() == 0 && IsOriginal(0xfa47, 0xf5c0) && IsOriginal(0x1d8ee, 0x1f9e0));
    assert(capturedLog.find("rollback=complete") != std::string::npos);

    // Each window-skin site rejects a before-write failure and rolls every
    // earlier transaction-owned instruction back to original execute-read.
    PopulateSites(); ResetOperations(); callFailureIndex = 10; persistentCallFailure = true;
    assert(!rebirths::InstallAdvFastForward(context, &FakeOps));
    assert(rebirths::testing::rebirth1_fast_forward::Active() == 0); AssertAllOriginalCalls();
    assert(capturedLog.find("rva=0x1e59ae") != std::string::npos && capturedLog.find("rollback=complete") != std::string::npos);
    PopulateSites(); ResetOperations(); callFailureIndex = 11; persistentCallFailure = true;
    assert(!rebirths::InstallAdvFastForward(context, &FakeOps));
    assert(rebirths::testing::rebirth1_fast_forward::Active() == 0); AssertAllOriginalCalls();
    assert(capturedLog.find("rva=0x1e5e5d") != std::string::npos && capturedLog.find("rollback=complete") != std::string::npos);

    // A low-level false result after either window-skin write leaves that
    // site transaction-owned, so both paths take the same complete rollback.
    PopulateSites(); ResetOperations(); callFailureIndex = 10; postWriteFailure = true;
    assert(!rebirths::InstallAdvFastForward(context, &FakeOps));
    assert(rebirths::testing::rebirth1_fast_forward::Active() == 0); AssertAllOriginalCalls();
    assert(capturedLog.find("rva=0x1e59ae") != std::string::npos && capturedLog.find("rollback=complete") != std::string::npos);
    PopulateSites(); ResetOperations(); callFailureIndex = 11; postWriteFailure = true;
    assert(!rebirths::InstallAdvFastForward(context, &FakeOps));
    assert(rebirths::testing::rebirth1_fast_forward::Active() == 0); AssertAllOriginalCalls();
    assert(capturedLog.find("rva=0x1e5e5d") != std::string::npos && capturedLog.find("rollback=complete") != std::string::npos);

    // The six-byte present redirect is transaction-owned too: a false return
    // after its write rolls it and all nineteen preceding calls back before enable.
    PopulateSites(); ResetOperations(); presentPostWriteFailures = 1;
    assert(!rebirths::InstallAdvFastForward(context, &FakeOps));
    const auto presentOriginal = rebirths::AdvBlackoutExpectedCall(reinterpret_cast<uintptr_t>(fakeBase));
    assert(rebirths::testing::rebirth1_fast_forward::Active() == 0 && !std::memcmp(fakeBase + 0x2970c8, presentOriginal.data(), presentOriginal.size()));
    AssertAllOriginalCalls();
    assert(HasProtection(fakeBase + 0x2970c8, PAGE_EXECUTE_READ));
    assert(capturedLog.find("blackout failed rva=0x2970c8") != std::string::npos && capturedLog.find("rollback=complete") != std::string::npos);

    PopulateSites(); ResetOperations(); presentBeforeWriteFailure = true;
    assert(!rebirths::InstallAdvFastForward(context, &FakeOps));
    AssertAllOriginalCalls();
    assert(rebirths::testing::rebirth1_fast_forward::Active() == 0 && IsOriginalPresentation());
    assert(capturedLog.find("rollback=complete") != std::string::npos);

    // The failed six-byte write can leave an RWX page. Ownership survives
    // transient restore failures, and success requires the original RX page.
    PopulateSites(); ResetOperations(); presentPostWriteFailures = 1;
    presentWriteLeavesWritable = true; presentRestoreFailures = 2;
    assert(!rebirths::InstallAdvFastForward(context, &FakeOps));
    AssertAllOriginalCalls();
    assert(rebirths::testing::rebirth1_fast_forward::Active() == 0 && IsOriginalPresentation() && presentRestoreFailures == 0);
    assert(capturedLog.find("blackout rollback failed") != std::string::npos && capturedLog.find("rollback=complete") != std::string::npos);

    PopulateSites(); ResetOperations(); presentPostWriteFailures = 1; presentRestoreLeavesWritable = true;
    assert(!rebirths::InstallAdvFastForward(context, &FakeOps));
    AssertAllOriginalCalls();
    assert(rebirths::testing::rebirth1_fast_forward::Active() == 0 && !std::memcmp(fakeBase + 0x2970c8, presentOriginal.data(), presentOriginal.size()));
    assert(HasProtection(fakeBase + 0x2970c8, PAGE_EXECUTE_READWRITE));
    assert(capturedLog.find("rollback=incomplete") != std::string::npos && capturedLog.find("rollback=complete") == std::string::npos);

    // RetargetCalls may return false after changing bytes. The failed site is also rolled back.
    PopulateSites(); ResetOperations(); callFailureIndex = 0; postWriteFailure = true;
    assert(!rebirths::InstallAdvFastForward(context, &FakeOps));
    assert(rebirths::testing::rebirth1_fast_forward::Active() == 0 && IsOriginal(0xfa47, 0xf5c0));

    // A failed low-level transaction can leave original bytes but RWX. The
    // final audit must reject that state even though no replacement remains.
    PopulateSites(); ResetOperations(); callFailureIndex = 0; persistentCallFailure = true; failureLeavesWritableOriginal = true;
    assert(!rebirths::InstallAdvFastForward(context, &FakeOps));
    assert(rebirths::testing::rebirth1_fast_forward::Active() == 0 && !HasProtection(fakeBase + 0xfa47, PAGE_EXECUTE_READ));
    assert(capturedLog.find("rollback=incomplete") != std::string::npos);
    assert(SetPage(fakeBase + 0xfa47, PAGE_EXECUTE_READ));

    // A rollback that restores bytes but leaves RW is incomplete and cannot enable wrappers.
    PopulateSites(); ResetOperations(); callFailureIndex = 0; postWriteFailure = true; leaveRestoreWritable = true;
    assert(!rebirths::InstallAdvFastForward(context, &FakeOps));
    assert(rebirths::testing::rebirth1_fast_forward::Active() == 0 && !HasProtection(fakeBase + 0xfa47, PAGE_EXECUTE_READ));
    assert(capturedLog.find("rollback=incomplete") != std::string::npos);
    assert(SetPage(fakeBase + 0xfa47, PAGE_EXECUTE_READ));

    // A transient rollback failure may recover on retry and must not leave a stale incomplete result.
    PopulateSites(); ResetOperations(); callFailureIndex = 0; postWriteFailure = true; restoreFailures = 1;
    assert(!rebirths::InstallAdvFastForward(context, &FakeOps));
    assert(rebirths::testing::rebirth1_fast_forward::Active() == 0 && IsOriginal(0xfa47, 0xf5c0));
    assert(capturedLog.find("rollback=complete") != std::string::npos);

    std::puts("ADV fast-forward ABI, nineteen-site transaction, guards, and rollback tests passed");
}
