#include "adv_test_contracts.hpp"
#include "synthetic_image.hpp"
#include "owned_patch_install.hpp"

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace { std::string capturedLog; }
namespace rebirths {
void Log(const char* format, ...) noexcept {
    char line[512]{}; va_list args; va_start(args, format);
    vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args); va_end(args);
    capturedLog.append(line).append("\n");
}
}

namespace {
constexpr size_t ImageSize = 0x6a0000;
unsigned char* fakeBase = nullptr;
unsigned originalCalls = 0, originalLoads = 0, operations = 0;
bool failAfterWrite = false;
unsigned failAtOperation = 0;
volatile LONG pollCalls = 0;
volatile unsigned char pollResult = 1;
unsigned characterReadyCalls=0;
uint32_t __cdecl OriginalCharacterReady(uintptr_t) { ++characterReadyCalls; return 0x12340030; }
unsigned delayCalls = 0, aggregateCalls = 0;
int aggregateResult = 0;
int __cdecl OriginalDelay(int* state, float* record, float duration) {
    ++delayCalls;
    if (*state == 0) {
        *state = 1;
        record[0] = record[1] = record[2] = 0;
        if (duration > 0) {
            record[1] = 2; record[2] = duration; record[3] = 0;
            return 0;
        }
    } else if (*state != 1) return 1;
    if (record[0] == record[1]) { ++*state; return 1; }
    return 0;
}
int __cdecl OriginalAggregate(uint32_t mask, int wait, uint32_t* output) {
    assert(mask == 0xffffffff && wait == 1); ++aggregateCalls;
    if (output) *output = 0x12340024;
    return aggregateResult;
}
unsigned cleanupCalls = 0;
int cleanupResult = 0;
int __cdecl OriginalCleanup(int* state, int operation, int a, int b, uint32_t c, uint32_t d) {
    assert(a == 3 && b == 4 && c == 5 && d == 6); ++cleanupCalls;
    if (cleanupResult) return cleanupResult;
    if (*state == 0) { *state = operation > 0 ? 10 : 1; return 0; }
    return *state == 1 ? 1 : 0;
}
unsigned commandCalls = 0, managerCalls = 0;
int commandResult = 0;
int __cdecl OriginalCharacterCommand(uint32_t id, int arg, int expression, float duration) {
    assert(id == 42 && arg == 3 && expression == 4 && duration == 0.5f);
    ++commandCalls; return commandResult;
}
uintptr_t __cdecl OriginalManager() { ++managerCalls; return 0x12345678; }
unsigned cgCalls = 0, exitCalls = 0;
int __cdecl OriginalCg(uintptr_t object, float duration) {
    assert(object == 0x1234 && duration == 0.5f); ++cgCalls; return commandResult;
}
int __cdecl OriginalExit(int* state, uint32_t id, int arg, float duration) {
    assert(id == 42 && arg == 3 && duration == 0.5f); ++exitCalls;
    ++*state; return commandResult;
}
unsigned setupCalls = 0; uintptr_t resolvedList = 0, lookupChild = 0;
void __cdecl OriginalSetup(uint32_t, uint32_t character, int a, int b, uint32_t c) {
    assert(character == 42 && a == 11 && b == 12 && c == 13); ++setupCalls;
}
uintptr_t __stdcall ResolveTask(uintptr_t handle) { assert(handle == 77); return resolvedList; }
uint8_t __fastcall LookupCharacter(uintptr_t list, void*, uint32_t character, uintptr_t* record, uintptr_t* child) {
    assert(list == resolvedList && character == 42); *record = 0; *child = lookupChild; return 0;
}
unsigned talkCalls = 0, groupCalls = 0, createCalls = 0;
void __cdecl OriginalCreate(uintptr_t manager, uint32_t operation, uint32_t entry, uint32_t) {
    assert(manager == 0x1234 && operation == 1 && entry == 2); ++createCalls;
}
uintptr_t groupRecord = 0; float groupScale = 0;
void __fastcall OriginalTrackGroup(uintptr_t record, void*, float scale) {
    ++groupCalls; groupRecord = record; groupScale = scale;
}
uint8_t __cdecl OriginalTalkReady(uintptr_t) { ++talkCalls; return 0; }
void __cdecl OriginalTrack(float*, float) { ++originalCalls; }
int __cdecl OriginalLoad(uint32_t*) { ++originalLoads; return 7; }
__declspec(naked) uint8_t OriginalPoll() {
    __asm inc dword ptr [pollCalls]
    __asm mov eax, 0xabcd0000
    __asm cmp byte ptr [pollResult], 0
    __asm je done
    __asm or eax, 1
    __asm done:
    __asm ret
}

bool Protect(void* address, size_t length, DWORD protection) { return fixture::Protect(address, length, protection); }
bool Mutate(uint32_t rva, const unsigned char* expected, const unsigned char* replacement, size_t count) {
    auto* at = fakeBase + rva;
    if (std::memcmp(at, expected, count) || !Protect(at, count, PAGE_EXECUTE_READWRITE)) return false;
    std::memcpy(at, replacement, count); return Protect(at, count, PAGE_EXECUTE_READ);
}
bool FakeRetargetCalls(const rebirths::Context&, const rebirths::CallSite* sites, size_t count) noexcept {
    assert(count == 1); ++operations;
    const auto replacement = fixture::CallBytes(reinterpret_cast<uintptr_t>(fakeBase + sites[0].rva), reinterpret_cast<uintptr_t>(sites[0].replacement));
    return Mutate(sites[0].rva, sites[0].expected.data(), replacement.data(), 5) &&
        !failAfterWrite && operations != failAtOperation;
}
bool FakeRetargetBytes(const rebirths::Context&, uint32_t rva, const unsigned char* expected,
                       const unsigned char* replacement, size_t count) noexcept {
    if (count == 6 && replacement[0] == 0xe8) {
        ++operations;
        return Mutate(rva, expected, replacement, count) && operations != failAtOperation;
    }
    return Mutate(rva, expected, replacement, count);
}
const rebirths::AdvFastForwardPatchOps FakeOps{FakeRetargetCalls, FakeRetargetBytes};

void Populate() {
    assert(Protect(fakeBase, ImageSize, PAGE_EXECUTE_READWRITE)); std::memset(fakeBase, 0, ImageSize);
    for (const auto [source, target] : std::initializer_list<std::pair<uint32_t, uint32_t>>{
            {0x79d60, 0xa7db0}, {0x8d9da, 0x8d6c0}, {0x7ee19, 0x86b00},
            {0x8d91a, 0xa7db0}, {0xa74c1, 0xa7db0}, {0x785c0, 0xa2cd0}, {0xa0bec, 0xa0c40}, {0x8c79e, 0x8e890}, {0x80d63, 0x7f4d0}, {0x9480c, 0x90fc0}, {0x91bfd, 0x914f0}, {0x26e113,0x26ff60}, {0x26e2f1,0x26ff60}, {0x27113d,0x26ff60}, {0x26e6e5,0x26ff60},
                {0x7e697, 0x7e210}, {0x7e6b6, 0x7e290}, {0x815fe, 0x81350}, {0x80d9b, 0x8b610}, {0x8378f, 0x9c620}, {0x82d68, 0x8b790}, {0x80d49, 0x8b910},
            {0xa0dcf, 0x936f0}, {0x8e7e1, 0x8eb70}}) {
        const auto call = fixture::CallBytes(source, target); std::memcpy(fakeBase + source, call.data(), 5);
    }
    const auto presentation = rebirths::AdvBlackoutExpectedCall(reinterpret_cast<uintptr_t>(fakeBase), 0x38304c);
    std::memcpy(fakeBase+0x32ae82,presentation.data(),presentation.size());
    *reinterpret_cast<uintptr_t*>(fakeBase+0x38304c)=1;
    assert(Protect(fakeBase, ImageSize, PAGE_EXECUTE_READ));
    originalCalls = originalLoads = operations = 0;
    pollCalls = 0;
    pollResult = 1; failAfterWrite = false; failAtOperation = 0; capturedLog.clear();
}
void PrepareAdv(uintptr_t adv) {
    assert(Protect(reinterpret_cast<void*>(adv), 0x8510, PAGE_READWRITE));
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 0;
    *reinterpret_cast<uint32_t*>(adv + 0x48) = 8;
    *reinterpret_cast<uintptr_t*>(adv + 0x14) = 1;
    *reinterpret_cast<uint8_t*>(adv + 0x850c) = 6;
    assert(Protect(fakeBase + 0x49a484, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE));
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x49a484) = adv;
    assert(Protect(fakeBase + 0x49a484, sizeof(uintptr_t), PAGE_EXECUTE_READ));
}
}

int main() {
    using namespace rebirths::testing::rebirth3_fast_forward;
    fixture::Image image(ImageSize); fakeBase = image.data();
    const rebirths::Context context{reinterpret_cast<HMODULE>(fakeBase), rebirths::GameSpecFor(rebirths::GameId::Rebirth3), L"", L""};
    const rebirths::Context wrong{reinterpret_cast<HMODULE>(fakeBase), rebirths::GameSpecFor(rebirths::GameId::Rebirth1), L"", L""};
    assert(!rebirths::InstallRebirth3AdvFastForward(wrong, &FakeOps));

    Populate(); assert(rebirths::InstallRebirth3AdvFastForward(context, &FakeOps)); assert(operations == 23 && Active() == 1);
    assert(Protect(fakeBase,ImageSize,PAGE_READWRITE));
    const auto windows=reinterpret_cast<uintptr_t>(fakeBase+0x4b0000);
    const auto widget=reinterpret_cast<uintptr_t>(fakeBase+0x520000);
    *reinterpret_cast<uintptr_t*>(fakeBase+0x684ee4)=windows;
    *reinterpret_cast<uintptr_t*>(windows+0x59a38)=widget;
    std::memcpy(reinterpret_cast<void*>(widget+0x74),"AdvWndInfo",sizeof("AdvWndInfo"));
    assert(InfoHandle()==widget);
    std::memcpy(reinterpret_cast<void*>(widget+0x74),"OtherWindow",sizeof("OtherWindow"));
    assert(InfoHandle()==0);
    *reinterpret_cast<uintptr_t*>(fakeBase+0x684ee4)=0;
    assert(Protect(fakeBase,ImageSize,PAGE_EXECUTE_READ));
    for (const auto [site, target] : std::initializer_list<std::pair<uint32_t, uint32_t>>{
            {0xa0dcf, 0x936f0}, {0x8e7e1, 0x8eb70}}) {
        const auto expected = fixture::CallBytes(site, target);
        assert(std::memcmp(fakeBase + site, expected.data(), 5) == 0);
    }
    BindScaledTrack(OriginalTrack);
    BindBackgroundLoad(OriginalLoad);
    BindAdvSePoll(reinterpret_cast<AdvSePollFn>(OriginalPoll));
    BindTalkReady(OriginalTalkReady);
    BindTrackGroup(reinterpret_cast<TrackGroupFn>(OriginalTrackGroup));
    const uintptr_t adv = reinterpret_cast<uintptr_t>(fakeBase + 0x4a0000); PrepareAdv(adv);
    BindDelay(OriginalDelay); BindAggregate(OriginalAggregate);
    BindTalkCleanup(OriginalCleanup);
    BindCharacterCommand(OriginalCharacterCommand); BindManager(OriginalManager);
    BindCgReady(OriginalCg); BindCharacterExit(OriginalExit);
    int exitState = 0;
    assert(CgReady(0x1234, 0.5f) == 1 && cgCalls == 1);
    assert(CharacterExit(&exitState, 42, 3, 0.5f) == 1 && exitCalls == 1 && exitState == 1);
    assert(CharacterCommand(42, 3, 4, 0.5f) == 1 && commandCalls == 1);
    commandResult = 0x12340000;
    assert(CharacterCommand(42, 3, 4, 0.5f) == 0x12340000 && commandCalls == 2);
    assert(CgReady(0x1234, 0.5f) == 0x12340000);
    assert(CharacterExit(&exitState, 42, 3, 0.5f) == 0x12340000);
    commandResult = -1;
    assert(CgReady(0x1234, 0.5f) == -1); // null-object native error must survive
    commandResult = 0;
    assert(CgCommandManager() == 0 && managerCalls == 1);
    int cleanupState = 0;
    assert(TalkCleanup(&cleanupState, -1, 3, 4, 5, 6) == 1 && cleanupCalls == 2 && cleanupState == 1);
    cleanupState = 0;
    assert(TalkCleanup(&cleanupState, 1, 3, 4, 5, 6) == 0 && cleanupCalls == 3 && cleanupState == 10);
    cleanupState = 0; cleanupResult = -1;
    assert(TalkCleanup(&cleanupState, -1, 3, 4, 5, 6) == -1 && cleanupCalls == 4 && cleanupState == 0);
    cleanupResult = 0;
    uint32_t aggregateOutput = 0;
    assert(Aggregate(0xffffffff, 1, &aggregateOutput) == 1 && aggregateCalls == 1);
    assert(aggregateOutput == 0x12340024);
    aggregateResult = 0x12340000;
    assert(Aggregate(0xffffffff, 1, nullptr) == 0x12340000); // full EAX, not AL
    aggregateResult = 0;
    int delayState = 0;
    auto* commandDelay = reinterpret_cast<float*>(adv + 0x18);
    assert(Delay(&delayState, commandDelay, 1.5f) == 1 && delayState == 2 && delayCalls == 2);
    assert(commandDelay[0] == 2 && commandDelay[1] == 2 && commandDelay[2] == 0 && commandDelay[3] == 2);
    delayState = 0; float unownedDelay[4]{};
    assert(Delay(&delayState, unownedDelay, 1.5f) == 0 && delayState == 1 && delayCalls == 3);
    for (unsigned guard = 0; guard < 5; ++guard) {
        PrepareAdv(adv); SetActive(true);
        if (guard == 0) SetActive(false);
        if (guard == 1) *reinterpret_cast<uint32_t*>(adv + 0x10) = 1;
        if (guard == 2) *reinterpret_cast<uint32_t*>(adv + 0x48) = 0;
        if (guard == 3) *reinterpret_cast<uintptr_t*>(adv + 0x14) = 0;
        if (guard == 4) *reinterpret_cast<uint8_t*>(adv + 0x850c) = 7;
        assert(Aggregate(0xffffffff, 1, &aggregateOutput) == 0);
        const unsigned commandsBefore = commandCalls, managersBefore = managerCalls;
        assert(CharacterCommand(42, 3, 4, 0.5f) == 0 && commandCalls == commandsBefore + 1);
        assert(CgCommandManager() == 0x12345678 && managerCalls == managersBefore + 1);
        const unsigned cgBefore = cgCalls, exitBefore = exitCalls;
        exitState = 0;
        assert(CgReady(0x1234, 0.5f) == 0 && cgCalls == cgBefore + 1);
        assert(CharacterExit(&exitState, 42, 3, 0.5f) == 0 && exitCalls == exitBefore + 1 && exitState == 1);
        cleanupState = 0;
        const unsigned cleanupBefore = cleanupCalls;
        assert(TalkCleanup(&cleanupState, -1, 3, 4, 5, 6) == 0 && cleanupState == 1 && cleanupCalls == cleanupBefore + 1);
        delayState = 0;
        const unsigned before = delayCalls;
        assert(Delay(&delayState, commandDelay, 1.5f) == 0 && delayState == 1 && delayCalls == before + 1);
    }
    PrepareAdv(adv); SetActive(true);
    BindAdvBinCreate(OriginalCreate);
    AdvBinCreate(0x1234, 1, 2, 0); assert(createCalls == 0);
    AdvBinCreate(0x1234, 1, 2, 1); assert(createCalls == 1);
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 1;
    AdvBinCreate(0x1234, 1, 2, 0); assert(createCalls == 2);
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 0;
    *reinterpret_cast<uint32_t*>(adv + 0x48) = 0;
    AdvBinCreate(0x1234, 1, 2, 0); assert(createCalls == 3);
    *reinterpret_cast<uint32_t*>(adv + 0x48) = 8;
    *reinterpret_cast<uint8_t*>(adv + 0x850c) = 7;
    AdvBinCreate(0x1234, 1, 2, 0); assert(createCalls == 4);
    *reinterpret_cast<uint8_t*>(adv + 0x850c) = 6;
    BindSetup(OriginalSetup); BindResolveTask(ResolveTask);
    BindLookupCharacter(reinterpret_cast<LookupFn>(LookupCharacter));
    uintptr_t manager[4]{}; manager[3]=77; resolvedList=0x1234;
    assert(Protect(fakeBase+0x49a4c8,4,PAGE_EXECUTE_READWRITE));
    *reinterpret_cast<uintptr_t*>(fakeBase+0x49a4c8)=reinterpret_cast<uintptr_t>(manager);
    assert(Protect(fakeBase+0x49a4c8,4,PAGE_EXECUTE_READ));
    CharacterSetup(2,42,11,12,13); assert(setupCalls==0);
    CharacterSetup(0,42,11,12,13); assert(setupCalls==0);
    CharacterSetup(1,42,11,12,13); assert(setupCalls==1);
    lookupChild=99;CharacterSetup(2,42,11,12,13);assert(setupCalls==2);lookupChild=0;
    resolvedList=0;CharacterSetup(2,42,11,12,13);assert(setupCalls==3);resolvedList=0x1234;
    *reinterpret_cast<uint32_t*>(adv+0x48)=0x408;
    CharacterSetup(2,42,11,12,13);assert(setupCalls==4);
    *reinterpret_cast<uint32_t*>(adv+0x48)=0;
    CharacterSetup(2,42,11,12,13);assert(setupCalls==5);
    *reinterpret_cast<uint32_t*>(adv+0x48)=8;
    *reinterpret_cast<uint32_t*>(adv+0x10)=1;
    CharacterSetup(2,42,11,12,13);assert(setupCalls==6);
    *reinterpret_cast<uint32_t*>(adv+0x10)=0;
    BindCharacterReady(OriginalCharacterReady);
    BindCharacterUpdate(OriginalCharacterReady);
    unsigned char queued[0x1650]{};
    queued[0x28]=2; queued[0x4d0]=1;
    *reinterpret_cast<uint32_t*>(queued+0x288)=123;
    assert(CharacterUpdate(reinterpret_cast<uintptr_t>(queued))==0x12340030);
    assert(queued[0x4d0]==2 && *reinterpret_cast<uint32_t*>(queued+0x288)==0);
    for (const size_t offset : {0x290u,0x294u,0x298u,0x29cu}) {
        queued[0x4d0]=1; *reinterpret_cast<uint32_t*>(queued+0x288)=123;
        *reinterpret_cast<uint32_t*>(queued+offset)=1;
        CharacterUpdate(reinterpret_cast<uintptr_t>(queued));
        assert(queued[0x4d0]==1 && *reinterpret_cast<uint32_t*>(queued+0x288)==123);
        *reinterpret_cast<uint32_t*>(queued+offset)=0;
    }
    for (const unsigned char state : std::array<unsigned char,6>{0,3,4,5,6,7}) {
        queued[0x4d0]=state; CharacterUpdate(reinterpret_cast<uintptr_t>(queued));
        assert(queued[0x4d0]==state && *reinterpret_cast<uint32_t*>(queued+0x288)==123);
    }
    queued[0x4d0]=1;
    *reinterpret_cast<uint32_t*>(adv+0x10)=1;
    CharacterUpdate(reinterpret_cast<uintptr_t>(queued));
    assert(queued[0x4d0]==1);
    *reinterpret_cast<uint32_t*>(adv+0x10)=0;
    queued[0x28]=1;
    CharacterUpdate(reinterpret_cast<uintptr_t>(queued));
    assert(queued[0x4d0]==1);
    queued[0x28]=2; SetActive(false);
    CharacterUpdate(reinterpret_cast<uintptr_t>(queued));
    assert(queued[0x4d0]==1);
    SetActive(true);
    characterReadyCalls=0;
    float actor[0x1650/4]{};
    for (const size_t offset : {0x1410u,0x152cu,0x153cu,0x158cu,0x1618u,0x1628u}) {
        actor[offset/4]=1;actor[offset/4+1]=5;actor[offset/4+2]=2;actor[offset/4+3]=1;
    }
    assert(CharacterReady(reinterpret_cast<uintptr_t>(actor))==0x12340030 && characterReadyCalls==1);
    for (const size_t offset : {0x1410u,0x152cu,0x153cu,0x158cu,0x1618u})
        assert(actor[offset/4]==5 && actor[offset/4+2]==0 && actor[offset/4+3]==5);
    assert(actor[0x1628/4]==1 && actor[0x1628/4+2]==2);
    *reinterpret_cast<uint32_t*>(adv+0x10)=1;actor[0x1410/4]=1;actor[0x1410/4+2]=2;
    assert(CharacterReady(reinterpret_cast<uintptr_t>(actor))==0x12340030 && characterReadyCalls==2);
    assert(actor[0x1410/4]==1 && actor[0x1410/4+2]==2);
    *reinterpret_cast<uint32_t*>(adv+0x10)=0;
    float background[] = {1, 7, 2, 1}; BackgroundTrack(background, 0.5f);
    assert(background[0] == 7 && background[2] == 0 && background[3] == 7 && originalCalls == 0);
    float inactive[] = {1, 7, 0, 99}; FadeTrack(inactive, 0.5f);
    assert(inactive[0] == 1 && inactive[3] == 99 && originalCalls == 0);
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 1; BackgroundTrack(background, 0.5f); assert(originalCalls == 1);
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 0; *reinterpret_cast<uint32_t*>(adv + 0x48) = 0;
    FadeTrack(background, 0.5f); assert(originalCalls == 2);
    *reinterpret_cast<uint32_t*>(adv + 0x48) = 8;
    float delay[] = {1, 5, 2, 1}; DelayTrack(delay, 1.0f); assert(originalCalls == 3);
    float* ownedDelay = reinterpret_cast<float*>(adv + 0x18);
    ownedDelay[0] = 1; ownedDelay[1] = 5; ownedDelay[2] = 2; ownedDelay[3] = 1;
    DelayTrack(ownedDelay, 1.0f); assert(ownedDelay[0] == 5 && ownedDelay[2] == 0 && originalCalls == 3);
    uint32_t load[0x46]{}; load[2] = 9; reinterpret_cast<unsigned char*>(load)[0x114] = 2;
    assert(BackgroundLoad(load) == 1 && load[2] == 0 && originalLoads == 0);
    load[2] = 9; load[3] = 1; assert(BackgroundLoad(load) == 7 && originalLoads == 1);
    assert(AdvSePoll(0x1234, nullptr) == 0 && pollCalls == 1);
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 1;
    assert(AdvSePoll(0x1234, nullptr) == 1 && pollCalls == 2);

    float talk[28]{};
    talk[24] = 1; talk[25] = 5; talk[26] = 2; talk[27] = 1;
    assert(TalkReady(reinterpret_cast<uintptr_t>(talk)) == 0 && talkCalls == 1);
    assert(talk[24] == 1 && talk[26] == 2); // alternate mode forwards
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 0;
    assert(TalkReady(reinterpret_cast<uintptr_t>(talk)) == 0 && talkCalls == 2);
    assert(talk[24] == 5 && talk[26] == 0 && talk[27] == 5); // pending result retained
    float group[48]{};
    for (size_t index = 0; index < 11; ++index) {
        group[index*4] = 1; group[index*4+1] = 5; group[index*4+2] = 2;
    }
    group[44] = 99; group[47] = 101;
    TrackGroup(reinterpret_cast<uintptr_t>(group), nullptr, 0.5f);
    assert(groupCalls == 1 && groupRecord == reinterpret_cast<uintptr_t>(group) && groupScale == 0.5f);
    for (size_t index = 0; index < 11; ++index)
        assert(group[index*4] == 5 && group[index*4+2] == 0 && group[index*4+3] == 5);
    assert(group[44] == 99 && group[47] == 101); // rotation storage untouched
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 1;
    group[0] = 1; group[2] = 2;
    TrackGroup(reinterpret_cast<uintptr_t>(group), nullptr, 0.25f);
    assert(groupCalls == 2 && group[0] == 1 && group[2] == 2 && groupScale == 0.25f);
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 0;
    *reinterpret_cast<uint8_t*>(adv + 0x850c) = 7;
    assert(!ActiveAdv()); // terminal owner must not accelerate
    *reinterpret_cast<uint8_t*>(adv + 0x850c) = 6;
    *reinterpret_cast<uintptr_t*>(adv + 0x14) = 0;
    assert(!ActiveAdv()); // stale skip flag without request must forward

    Populate(); failAfterWrite = true; assert(!rebirths::InstallRebirth3AdvFastForward(context, &FakeOps));
    assert(Active() == 0 && capturedLog.find("rollback=complete") != std::string::npos);
    for (unsigned failedSite = 1; failedSite <= 23; ++failedSite) {
        Populate(); failAtOperation = failedSite;
        assert(!rebirths::InstallRebirth3AdvFastForward(context, &FakeOps));
        assert(Active() == 0 && capturedLog.find("rollback=complete") != std::string::npos);
        const auto presentation = rebirths::AdvBlackoutExpectedCall(reinterpret_cast<uintptr_t>(fakeBase),0x38304c);
        assert(std::memcmp(fakeBase+0x32ae82,presentation.data(),6)==0);
        for (const auto [site, target] : std::initializer_list<std::pair<uint32_t, uint32_t>>{
                {0x79d60, 0xa7db0}, {0x8d9da, 0x8d6c0}, {0x7ee19, 0x86b00},
                {0x8d91a, 0xa7db0}, {0xa74c1, 0xa7db0}, {0x785c0, 0xa2cd0}, {0xa0bec, 0xa0c40}, {0x8c79e, 0x8e890}, {0x80d63, 0x7f4d0}, {0x9480c, 0x90fc0}, {0x91bfd, 0x914f0}, {0x26e113,0x26ff60}, {0x26e2f1,0x26ff60}, {0x27113d,0x26ff60}, {0x26e6e5,0x26ff60},
                {0x7e697, 0x7e210}, {0x7e6b6, 0x7e290}, {0x815fe, 0x81350}, {0x80d9b, 0x8b610}, {0x8378f, 0x9c620}, {0x82d68, 0x8b790}, {0x80d49, 0x8b910},
                {0xa0dcf, 0x936f0}, {0x8e7e1, 0x8eb70}}) {
            const auto expected = fixture::CallBytes(site, target);
            assert(std::memcmp(fakeBase + site, expected.data(), 5) == 0);
        }
    }
    // Keep the first image mapped: the second must exercise another load base.
    auto* firstBase = fakeBase;
    fixture::Image relocatedImage(ImageSize); fakeBase = relocatedImage.data();
    assert(fakeBase && fakeBase != firstBase);
    const rebirths::Context relocated{reinterpret_cast<HMODULE>(fakeBase), context.spec, L"", L""};
    Populate(); assert(rebirths::InstallRebirth3AdvFastForward(relocated, &FakeOps));
    const uintptr_t secondAdv = reinterpret_cast<uintptr_t>(fakeBase + 0x4a0000);
    PrepareAdv(secondAdv); assert(ActiveAdv() == secondAdv);
    assert(reinterpret_cast<uintptr_t>(NativeScaledTrack()) == reinterpret_cast<uintptr_t>(fakeBase) + 0xa7db0);
    int32_t displacement = 0; std::memcpy(&displacement, fakeBase + 0x79d61, 4);
    assert(reinterpret_cast<uintptr_t>(fakeBase + 0x79d65) + displacement == reinterpret_cast<uintptr_t>(HookDelayTrack()));
    std::puts("Re;Birth3 ADV fast-forward ABI, guards, and twenty-two-site rollback tests passed");
}
