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
unsigned soundHandleCalls = 0, motionCalls = 0;
uint8_t __cdecl OriginalSoundHandle(uintptr_t sound) {assert(sound == 0x1234); ++soundHandleCalls; return pollResult;}
uint32_t __fastcall OriginalMotion(uintptr_t, void*) {++motionCalls; return 0x12342000;}
uintptr_t fakeTelopPayload = 0;
uintptr_t __cdecl GetTelopPayload() { return fakeTelopPayload; }
int32_t nativeDuration = 180;
const int32_t* __cdecl OriginalParameter(uintptr_t, uint32_t) {return &nativeDuration;}
unsigned telopVisualCalls = 0;
uint8_t __fastcall OriginalTelopVisual(uint32_t*, void*, uint32_t banks, uint32_t textures) {
    assert(banks == 3 && textures == 10); ++telopVisualCalls; return 7;
}
unsigned chapterCalls = 0;
unsigned chapterFinishCalls = 0;
void __cdecl OriginalChapterFinish() { ++chapterFinishCalls; }
unsigned soundUpdateCalls = 0;
unsigned scriptSoundCalls = 0;
uintptr_t __cdecl OriginalScriptSound(uintptr_t resource,uint32_t cue,uint32_t group,int priority) {
    assert(resource==0x1234 && cue==7 && group==3 && priority==2);++scriptSoundCalls;return 0x87654321;
}
uint8_t __fastcall OriginalSoundUpdate(uint32_t* record, void*) {
    ++soundUpdateCalls; return reinterpret_cast<uint8_t*>(record)[0x10] == 5 ? 0 : 1;
}
int __cdecl OriginalChapter(uint32_t* record) {
    ++chapterCalls;
    auto& state = reinterpret_cast<uint8_t*>(record)[0x14];
    if (state == 5) {state = 0; return 1;}
    return 0;
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
uint8_t __cdecl OriginalLoad(uint32_t*) { ++originalLoads; return 7; }
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
    const bool presentationWrite = rva == 0x2e82a2 && expected[0] == 0xff;
    if (presentationWrite) ++operations;
    const bool wrote = Mutate(rva, expected, replacement, count);
    return wrote && !(presentationWrite && (failAfterWrite || operations == failAtOperation));
}
const rebirths::AdvFastForwardPatchOps FakeOps{FakeRetargetCalls, FakeRetargetBytes};

void Populate() {
    assert(Protect(fakeBase, ImageSize, PAGE_EXECUTE_READWRITE)); std::memset(fakeBase, 0, ImageSize);
    for (const auto [source, target] : std::initializer_list<std::pair<uint32_t, uint32_t>>{
            {0x804b9, 0x8beb0},
            {0x291a32, 0x228800},
            {0x294ed9, 0x228800},
            {0x9b90e, 0x9bc60},
            {0x7b89f, 0x22aa70},
            {0x813ba, 0x812b0},
            {0x76900, 0xa47b0},
            {0x8a48e, 0x8a190},
            {0x7b8d9, 0x835d0},
            {0x8a3ea, 0xa47b0},
            {0xa3ec1, 0xa47b0},
            {0x751b0, 0x9f6e0},
            {0x9d5fc, 0x9d650},
            {0x8926e, 0x8b340},
            {0x7d863, 0x7bfd0},
            {0x7b177, 0x7acf0},
            {0x7b196, 0x7ad70},
            {0x7e0fe, 0x7de50},
            {0x7d89b, 0x880e0},
            {0x8028f, 0x99080},
            {0x7f868, 0x88260},
            {0x7d849, 0x883e0},
            {0x230983, 0x2327b0},
            {0x230b61, 0x2327b0},
            {0x23398d, 0x2327b0},
            {0x230f3d, 0x2327b0},

}) {
        const auto call = fixture::CallBytes(source, target); std::memcpy(fakeBase + source, call.data(), 5);
    }
    for(const auto [r,t] : std::initializer_list<std::pair<uint32_t,uint32_t>>{
        {0x80873,0x81920},{0x807ff,0x24b3b0},{0x287f87,0x24b3b0},{0x288037,0x24b3b0}}) {
        const auto bytes=fixture::CallBytes(r,t); std::memcpy(fakeBase+r,bytes.data(),5);
    }
    const auto chapter = fixture::CallBytes(0x8ce0d, 0x8ccf0); std::memcpy(fakeBase + 0x8ce0d, chapter.data(), 5);
    const auto present = rebirths::AdvBlackoutExpectedCall(reinterpret_cast<uintptr_t>(fakeBase), 0x33b04c);
    std::memcpy(fakeBase + 0x2e82a2, present.data(), present.size());
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x33b04c) = 1;
    assert(Protect(fakeBase, ImageSize, PAGE_EXECUTE_READ));
    originalCalls = originalLoads = operations = 0;
    pollCalls = 0;
    pollResult = 1; failAfterWrite = false; failAtOperation = 0; capturedLog.clear();
}
void PrepareAdv(uintptr_t adv) {
    assert(Protect(reinterpret_cast<void*>(adv), 0x8500, PAGE_READWRITE));
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 0;
    *reinterpret_cast<uint32_t*>(adv + 0x48) = 8;
    *reinterpret_cast<uintptr_t*>(adv + 0x14) = 1;
    *reinterpret_cast<uint8_t*>(adv + 0x84fc) = 6;
    assert(Protect(fakeBase + 0x443284, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE));
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x443284) = adv;
    assert(Protect(fakeBase + 0x443284, sizeof(uintptr_t), PAGE_EXECUTE_READ));
}
}

int main() {
    using namespace rebirths::testing::rebirth2_fast_forward;
    fixture::Image image(ImageSize); fakeBase = image.data();
    const rebirths::Context context{reinterpret_cast<HMODULE>(fakeBase), rebirths::GameSpecFor(rebirths::GameId::Rebirth2), L"", L""};
    const rebirths::Context wrong{reinterpret_cast<HMODULE>(fakeBase), rebirths::GameSpecFor(rebirths::GameId::Rebirth1), L"", L""};
    assert(!rebirths::InstallRebirth2AdvFastForward(wrong, &FakeOps));

    Populate(); assert(rebirths::InstallRebirth2AdvFastForward(context, &FakeOps)); assert(operations == 27 && Active() == 1);
    BindScaledTrack(OriginalTrack);
    BindBackgroundLoad(OriginalLoad);
    BindAdvSePoll(reinterpret_cast<AdvSePollFn>(OriginalPoll));
    BindTalkReady(OriginalTalkReady);
    BindTrackGroup(reinterpret_cast<TrackGroupFn>(OriginalTrackGroup));
    const uintptr_t adv = reinterpret_cast<uintptr_t>(fakeBase + 0x4a0000); PrepareAdv(adv);
    assert(BlackoutActive());
    BindSoundHandlePoll(OriginalSoundHandle);
    BindCgMotionPoll(reinterpret_cast<CgMotionPollFn>(OriginalMotion));
    BindAdvSoundUpdate(reinterpret_cast<AdvSoundUpdateFn>(OriginalSoundUpdate));
    uint32_t se[5]{0,42,7,0x3f800000,1};
    SetActive(false); assert(AdvSoundUpdate(se,nullptr)==1 && se[4]==1);
    SetActive(true); assert(AdvSoundUpdate(se,nullptr)==0 && se[4]==5 && !se[0] && se[1]==42 && se[2]==7 && se[3]==0x3f800000);
    for(unsigned state : {0u,2u,3u,4u}) {se[4]=state; assert(AdvSoundUpdate(se,nullptr)==1 && se[4]==state);}
    se[0]=77;se[4]=1; assert(AdvSoundUpdate(se,nullptr)==1 && se[0]==77 && se[4]==1);
    assert(soundUpdateCalls==7);
    BindScriptSoundPlay(OriginalScriptSound);
    auto play=[]() {return ScriptSoundPlay(0x1234,7,3,2);};
    assert(play()==0 && scriptSoundCalls==0);
    *reinterpret_cast<uint32_t*>(adv+0x48)=0;assert(play()==0); // Option applies before native I.
    for(unsigned reason=0;reason<6;reason++) {
        PrepareAdv(adv);SetActive(true);
        assert(Protect(fakeBase+0x443284,4,PAGE_READWRITE));
        assert(Protect(fakeBase+0x44f214,4,PAGE_READWRITE));
        if(reason==0) SetActive(false);
        if(reason==1) *reinterpret_cast<uintptr_t*>(fakeBase+0x443284)=0;
        if(reason==2) *reinterpret_cast<uintptr_t*>(adv+0x14)=0;
        if(reason==3) *reinterpret_cast<uint8_t*>(adv+0x84fc)=7;
        if(reason==4) *reinterpret_cast<uint32_t*>(adv+0x10)=1;
        if(reason==5) {
            const uintptr_t scene=reinterpret_cast<uintptr_t>(fakeBase+0x500000);
            assert(Protect(reinterpret_cast<void*>(scene+0x12a7c8),4,PAGE_READWRITE));
            *reinterpret_cast<uintptr_t*>(fakeBase+0x44f214)=scene;
            *reinterpret_cast<uint32_t*>(scene+0x12a7c8)=77;
        }
        assert(play()==0x87654321);
        *reinterpret_cast<uintptr_t*>(fakeBase+0x44f214)=0;
    }
    assert(scriptSoundCalls==6);PrepareAdv(adv);SetActive(true);
    assert(SoundHandlePoll(0x1234) == 0 && soundHandleCalls == 1);
    pollResult = 0; assert(SoundHandlePoll(0x1234) == 0 && soundHandleCalls == 2); pollResult = 1;
    float cgObject[0x700 / sizeof(float)]{};
    auto* pan = cgObject + 0x30 / sizeof(float);
    pan[0] = 1; pan[1] = 5; pan[2] = 2; pan[3] = 1;
    pan[4] = 7; pan[5] = 9; pan[6] = 0; pan[7] = 7;
    cgObject[0xb0 / 4] = 42;
    assert(CgMotionPoll(reinterpret_cast<uintptr_t>(cgObject), nullptr) == 0x12342000 && motionCalls == 1);
    assert(pan[0] == 5 && pan[2] == 0 && pan[3] == 5);
    assert(pan[4] == 7 && pan[7] == 7 && cgObject[0xb0 / 4] == 42);
    for (int guard = 0; guard < 4; ++guard) {
        PrepareAdv(adv); SetActive(true);
        if (guard == 0) SetActive(false);
        if (guard == 1) *reinterpret_cast<uint32_t*>(adv + 0x10) = 1;
        if (guard == 2) *reinterpret_cast<uint32_t*>(adv + 0x48) = 0;
        if (guard == 3) *reinterpret_cast<uintptr_t*>(adv + 0x14) = 0;
        pan[0] = 1; pan[2] = 2; pan[3] = 1;
        assert(SoundHandlePoll(0x1234) == 1);
        assert(CgMotionPoll(reinterpret_cast<uintptr_t>(cgObject), nullptr) == 0x12342000);
        assert(pan[0] == 1 && pan[2] == 2 && pan[3] == 1);
    }
    PrepareAdv(adv); SetActive(true);
    // FILE4 can retain a live mode-zero ADV request throughout battle. A stale
    // skip bit must never conceal battle, and no native state should be changed.
    const uintptr_t scene = reinterpret_cast<uintptr_t>(fakeBase + 0x500000);
    assert(Protect(fakeBase + 0x44f214, 4, PAGE_READWRITE));
    assert(Protect(reinterpret_cast<void*>(scene + 0x12a7c8), 4, PAGE_READWRITE));
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x44f214) = scene;
    *reinterpret_cast<uint32_t*>(scene + 0x12a7c8) = 77;
    for (unsigned frame = 0; frame < 120; ++frame) assert(!BlackoutActive());
    assert(ActiveAdv() == adv);
    assert(*reinterpret_cast<uint32_t*>(adv + 0x48) == 8);
    assert(*reinterpret_cast<uintptr_t*>(adv + 0x14) == 1);
    assert(*reinterpret_cast<uint32_t*>(scene + 0x12a7c8) == 77);
    *reinterpret_cast<uint32_t*>(scene + 0x12a7c8) = 0;
    assert(BlackoutActive()); // No stale suppression after returning from battle.
    *reinterpret_cast<uint32_t*>(scene + 0x12a7c8) = 88;
    assert(!BlackoutActive()); // Next battle in the same process.
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x44f214) = 0;
    assert(BlackoutActive()); // Startup/no scene remains safe.
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 1; assert(!BlackoutActive());
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 0;
    SetActive(false); assert(!BlackoutActive()); SetActive(true);
    unsigned char noticeManager[0x280]{}, widget[0xa0]{}, otherWidget[0xa0]{};
    const uintptr_t windows = reinterpret_cast<uintptr_t>(fakeBase + 0x600000);
    assert(Protect(fakeBase + 0x4432c8, 4, PAGE_EXECUTE_READWRITE));
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x4432c8) = reinterpret_cast<uintptr_t>(noticeManager);
    *reinterpret_cast<uint32_t*>(noticeManager) = 1;
    *reinterpret_cast<uint32_t*>(noticeManager + 0x27c) = 1;
    *reinterpret_cast<uintptr_t*>(noticeManager + 0x70) = reinterpret_cast<uintptr_t>(widget);
    assert(InfoHandle() == reinterpret_cast<uintptr_t>(widget));
    *reinterpret_cast<uint32_t*>(noticeManager + 0x27c) = 0;
    assert(InfoHandle() == 0);
    assert(Protect(fakeBase + 0x5cffdc, 4, PAGE_EXECUTE_READWRITE));
    assert(Protect(reinterpret_cast<void*>(windows), 0x59140, PAGE_READWRITE));
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x5cffdc) = windows;
    *reinterpret_cast<uintptr_t*>(windows + 0x59138) = reinterpret_cast<uintptr_t>(otherWidget);
    *reinterpret_cast<uintptr_t*>(otherWidget + 0x10) = reinterpret_cast<uintptr_t>(widget);
    std::memcpy(widget + 0x74, "AdvWndInfo", sizeof("AdvWndInfo"));
    assert(InfoHandle() == 0); // Reject the RB3 layout.
    std::memset(widget + 0x70, 0, 32);
    std::memcpy(widget + 0x70, "AdvWndInfo", sizeof("AdvWndInfo"));
    assert(InfoHandle() == reinterpret_cast<uintptr_t>(widget));
    std::memcpy(widget + 0x70, "AdvWndInfoMoney", sizeof("AdvWndInfoMoney"));
    assert(InfoHandle() == reinterpret_cast<uintptr_t>(widget));
    *reinterpret_cast<uintptr_t*>(otherWidget + 0x10) = 0; assert(InfoHandle() == 0);
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x5cffdc) = 0;
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x4432c8) = 0;
    assert(Protect(fakeBase + 0x4432c8, 4, PAGE_EXECUTE_READ));
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
        if (guard == 4) *reinterpret_cast<uint8_t*>(adv + 0x84fc) = 7;
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
    *reinterpret_cast<uint8_t*>(adv + 0x84fc) = 7;
    AdvBinCreate(0x1234, 1, 2, 0); assert(createCalls == 4);
    *reinterpret_cast<uint8_t*>(adv + 0x84fc) = 6;
    BindSetup(OriginalSetup); BindResolveTask(ResolveTask);
    BindLookupCharacter(reinterpret_cast<LookupFn>(LookupCharacter));
    uintptr_t manager[4]{}; manager[3]=77; resolvedList=0x1234;
    assert(Protect(fakeBase+0x4432c8,4,PAGE_EXECUTE_READWRITE));
    *reinterpret_cast<uintptr_t*>(fakeBase+0x4432c8)=reinterpret_cast<uintptr_t>(manager);
    assert(Protect(fakeBase+0x4432c8,4,PAGE_EXECUTE_READ));
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
    *reinterpret_cast<uint8_t*>(adv + 0x84fc) = 7;
    assert(!ActiveAdv()); // terminal owner must not accelerate
    *reinterpret_cast<uint8_t*>(adv + 0x84fc) = 6;
    *reinterpret_cast<uintptr_t*>(adv + 0x14) = 0;
    assert(!ActiveAdv()); // stale skip flag without request must forward

    Populate();
    assert(Protect(fakeBase + 0x7f868, 5, PAGE_EXECUTE_READWRITE));
    fakeBase[0x7f868] = 0x90;
    assert(Protect(fakeBase + 0x7f868, 5, PAGE_EXECUTE_READ));
    assert(!rebirths::InstallRebirth2AdvFastForward(context, &FakeOps) && operations == 0 && Active() == 0);
    Populate();
    assert(Protect(fakeBase + 0x7f868, 5, PAGE_READWRITE));
    assert(!rebirths::InstallRebirth2AdvFastForward(context, &FakeOps) && operations == 0);
    Populate(); PrepareAdv(adv);
    assert(!rebirths::InstallRebirth2AdvFastForward(context, &FakeOps) && operations == 0);

    Populate(); failAfterWrite = true; assert(!rebirths::InstallRebirth2AdvFastForward(context, &FakeOps));
    assert(Active() == 0 && capturedLog.find("rollback=complete") != std::string::npos);
    for (unsigned failedSite = 1; failedSite <= 27; ++failedSite) {
        Populate(); failAtOperation = failedSite;
        assert(!rebirths::InstallRebirth2AdvFastForward(context, &FakeOps));
        assert(Active() == 0 && capturedLog.find("rollback=complete") != std::string::npos);
        const auto restoredPresent = rebirths::AdvBlackoutExpectedCall(reinterpret_cast<uintptr_t>(fakeBase), 0x33b04c);
        assert(std::memcmp(fakeBase + 0x2e82a2, restoredPresent.data(), 6) == 0);
        assert(rebirths::owned_patch::IsOriginalExecutable(reinterpret_cast<uintptr_t>(fakeBase + 0x2e82a2), restoredPresent));
        for (const auto [site, target] : std::initializer_list<std::pair<uint32_t, uint32_t>>{
            {0x804b9, 0x8beb0},
            {0x291a32, 0x228800},
            {0x294ed9, 0x228800},
            {0x9b90e, 0x9bc60},
            {0x7b89f, 0x22aa70},
            {0x813ba, 0x812b0},
            {0x76900, 0xa47b0},
            {0x8a48e, 0x8a190},
            {0x7b8d9, 0x835d0},
            {0x8a3ea, 0xa47b0},
            {0xa3ec1, 0xa47b0},
            {0x751b0, 0x9f6e0},
            {0x9d5fc, 0x9d650},
            {0x8926e, 0x8b340},
            {0x7d863, 0x7bfd0},
            {0x7b177, 0x7acf0},
            {0x7b196, 0x7ad70},
            {0x7e0fe, 0x7de50},
            {0x7d89b, 0x880e0},
            {0x8028f, 0x99080},
            {0x7f868, 0x88260},
            {0x7d849, 0x883e0},
            {0x230983, 0x2327b0},
            {0x230b61, 0x2327b0},
            {0x23398d, 0x2327b0},
            {0x230f3d, 0x2327b0},

}) {
            const auto expected = fixture::CallBytes(site, target);
            assert(std::memcmp(fakeBase + site, expected.data(), 5) == 0);
        }
    }
    // Keep the first image mapped: the second must exercise another load base.
    auto* firstBase = fakeBase;
    fixture::Image relocatedImage(ImageSize); fakeBase = relocatedImage.data();
    assert(fakeBase && fakeBase != firstBase);
    const rebirths::Context relocated{reinterpret_cast<HMODULE>(fakeBase), context.spec, L"", L""};
    Populate(); assert(rebirths::InstallRebirth2AdvFastForward(relocated, &FakeOps));
    const uintptr_t secondAdv = reinterpret_cast<uintptr_t>(fakeBase + 0x4a0000);
    PrepareAdv(secondAdv); assert(ActiveAdv() == secondAdv);
    assert(reinterpret_cast<uintptr_t>(NativeScaledTrack()) == reinterpret_cast<uintptr_t>(fakeBase) + 0xa47b0);
    int32_t displacement = 0; std::memcpy(&displacement, fakeBase + 0x76901, 4);
    assert(reinterpret_cast<uintptr_t>(fakeBase + 0x76905) + displacement == reinterpret_cast<uintptr_t>(HookDelayTrack()));
    Populate();
    assert(!rebirths::InstallRebirth2SkipChapterIntros(wrong, &FakeOps));
    assert(rebirths::InstallRebirth2SkipChapterIntros(relocated, &FakeOps));
    BindTelopVisualCreate(reinterpret_cast<TelopVisualCreateFn>(&OriginalTelopVisual));
    uint32_t telopStorage[190]{}; auto* visual=telopStorage+1; visual[13]=0x3f800000;
    fakeTelopPayload=reinterpret_cast<uintptr_t>(telopStorage); BindTelopPayload(GetTelopPayload);
    BindScriptParameter(OriginalParameter);
    uint32_t vmStorage[0x1010]{}; const auto vm=reinterpret_cast<uintptr_t>(vmStorage);
    vmStorage[0x1001]=123; PrepareAdv(secondAdv);
    assert(Protect(fakeBase+0x4432e8,4,PAGE_READWRITE));
    *reinterpret_cast<uintptr_t*>(fakeBase+0x4432e8)=77;
    assert(*TelopParameter(vm,0)==180);
    assert(TelopVisualCreate(visual,nullptr,3,10)==0 && telopVisualCalls==0 && visual[13]==0x3f800000);
    for(unsigned i=0;i<7;++i) {
        visual[i]=99; assert(TelopVisualCreate(visual,nullptr,3,10)==7 && visual[i]==99); visual[i]=0;
    }
    SetChapterActive(false); assert(TelopVisualCreate(visual,nullptr,3,10)==7);
    SetChapterActive(true); assert(TelopVisualCreate(nullptr,nullptr,3,10)==7);
    assert(telopVisualCalls==9);
    assert(*TelopWaitParameter(vm,0)==0 && nativeDuration==180);
    assert(*TelopWaitParameter(vm,1)==180);
    assert(*TelopWaitParameter(vm+4,0)==180);
    for(unsigned guard=0;guard<9;++guard) {
        PrepareAdv(secondAdv); assert(Protect(fakeBase+0x443284,0x80,PAGE_READWRITE)); SetChapterActive(true); vmStorage[0x1001]=123;
        *reinterpret_cast<uintptr_t*>(fakeBase+0x4432e8)=77;
        fakeTelopPayload=reinterpret_cast<uintptr_t>(telopStorage);
        if(guard==0) SetChapterActive(false);
        if(guard==1) vmStorage[0x1001]=456;
        if(guard==2) *reinterpret_cast<uintptr_t*>(fakeBase+0x4432e8)=78;
        if(guard==3) fakeTelopPayload=0;
        if(guard==4) *reinterpret_cast<uintptr_t*>(secondAdv+0x14)=2;
        if(guard==5) *reinterpret_cast<uint8_t*>(secondAdv+0x84fc)=7;
        if(guard==6) *reinterpret_cast<uint32_t*>(secondAdv+0x10)=1;
        if(guard==7) *reinterpret_cast<uintptr_t*>(fakeBase+0x44328c)=1;
        if(guard==8) *reinterpret_cast<uintptr_t*>(fakeBase+0x443284)=0;
        assert(*TelopWaitParameter(vm,0)==180);
        *reinterpret_cast<uintptr_t*>(fakeBase+0x44328c)=0;
    }
    PrepareAdv(secondAdv); assert(Protect(fakeBase+0x443284,0x80,PAGE_READWRITE)); SetChapterActive(true); *reinterpret_cast<uintptr_t*>(fakeBase+0x4432e8)=0;
    BindChapterUpdate(OriginalChapter);
    uint32_t chapter[6]{42,0,0,0,0x2000,1};
    assert(ChapterUpdate(chapter) == 0 && chapterCalls == 1 && chapter[5] == 3 && chapter[0] == 42 && chapter[4] == 0x2000);
    assert(ChapterUpdate(chapter) == 0 && chapter[5] == 3); // Task remains visible to next script command.
    chapter[5]=4; assert(ChapterUpdate(chapter)==1 && chapter[5]==0); // Script-authorized close.
    for (unsigned state : {0u,2u,3u}) {
        chapter[5] = state; assert(ChapterUpdate(chapter) == 0 && chapter[5] == state);
    }
    for (unsigned field : {1u,2u,3u}) {
        chapter[5]=1; chapter[field]=77; assert(ChapterUpdate(chapter)==0 && chapter[5]==1 && chapter[field]==77); chapter[field]=0;
        chapter[5]=4; chapter[field]=77; assert(ChapterUpdate(chapter)==0 && chapter[5]==4 && chapter[field]==77); chapter[field]=0;
    }
    SetChapterActive(false); chapter[5]=1; assert(ChapterUpdate(chapter)==0 && chapter[5]==1);
    // A skipped chapter's script still owns an explicit wait after its visual
    // task closes. Native skip has not yet activated on this path.
    PrepareAdv(secondAdv); SetActive(true); SetChapterActive(true);
    *reinterpret_cast<uint32_t*>(secondAdv+0x48)=0;
    BindDelay(OriginalDelay); BindScaledTrack(OriginalTrack);
    chapter[5]=1; assert(ChapterUpdate(chapter)==0 && chapter[5]==3);
    chapter[5]=4; assert(ChapterUpdate(chapter)==1);
    int chapterDelayState=0;
    auto* chapterDelay=reinterpret_cast<float*>(secondAdv+0x18);
    assert(Delay(&chapterDelayState,chapterDelay,3.0f)==1 && chapterDelayState==2);
    assert(*reinterpret_cast<uint32_t*>(secondAdv+0x48)==0); // No synthetic skip.
    float chapterFade[]{1,5,2,1}; FadeTrack(chapterFade,1.0f);
    assert(chapterFade[0]==5 && chapterFade[2]==0 && chapterFade[3]==5);
    float unrelatedDelay[4]{}; chapterDelayState=0;
    assert(Delay(&chapterDelayState,unrelatedDelay,3.0f)==0);
    // These guards must retain native pending behavior even with remembered
    // chapter provenance. Other FF command wrappers still require native skip.
    for(unsigned guard=0;guard<8;++guard) {
        PrepareAdv(secondAdv); *reinterpret_cast<uint32_t*>(secondAdv+0x48)=0;
        SetActive(true); SetChapterActive(true); chapter[5]=1; ChapterUpdate(chapter);
        assert(Protect(fakeBase+0x443284,0x80,PAGE_READWRITE));
        if(guard==0) SetActive(false);
        if(guard==1) SetChapterActive(false);
        if(guard==2) *reinterpret_cast<uintptr_t*>(secondAdv+0x14)=2;
        if(guard==3) *reinterpret_cast<uint8_t*>(secondAdv+0x84fc)=7;
        if(guard==4) *reinterpret_cast<uint32_t*>(secondAdv+0x10)=1;
        if(guard==5) *reinterpret_cast<uintptr_t*>(fakeBase+0x443284)=0;
        if(guard==6) *reinterpret_cast<uintptr_t*>(fakeBase+0x44328c)=1;
        if(guard==7) *reinterpret_cast<uintptr_t*>(fakeBase+0x4432e8)=1;
        chapterDelayState=0;
        assert(Delay(&chapterDelayState,chapterDelay,3.0f)==0 && chapterDelayState==1);
        *reinterpret_cast<uintptr_t*>(fakeBase+0x44328c)=0;
        *reinterpret_cast<uintptr_t*>(fakeBase+0x4432e8)=0;
    }
    PrepareAdv(secondAdv); SetActive(true); SetChapterActive(true);
    *reinterpret_cast<uint32_t*>(secondAdv+0x48)=0;
    chapter[5]=1; ChapterUpdate(chapter); assert(ChapterWaitOwner()==secondAdv);
    BindChapterFinish(OriginalChapterFinish); ChapterFinish();
    assert(chapterFinishCalls==1 && !ChapterWaitOwner());
    chapterDelayState=0; assert(Delay(&chapterDelayState,chapterDelay,3.0f)==0);
    for(unsigned failure=1;failure<=5;++failure) {
        Populate(); failAtOperation=failure;
        assert(!rebirths::InstallRebirth2SkipChapterIntros(relocated,&FakeOps) && ChapterActive()==0);
        assert(rebirths::owned_patch::IsOriginalExecutable(reinterpret_cast<uintptr_t>(fakeBase)+0x8ce0d,fixture::CallBytes(0x8ce0d,0x8ccf0)));
        for(const auto [r,t] : std::initializer_list<std::pair<uint32_t,uint32_t>>{
            {0x80873,0x81920},{0x807ff,0x24b3b0},{0x287f87,0x24b3b0},{0x288037,0x24b3b0}})
            assert(rebirths::owned_patch::IsOriginalExecutable(reinterpret_cast<uintptr_t>(fakeBase)+r,fixture::CallBytes(r,t)));
    }
    Populate(); assert(Protect(fakeBase+0x4432e8,4,PAGE_READWRITE));
    *reinterpret_cast<uint32_t*>(fakeBase+0x4432e8)=1;
    assert(!rebirths::InstallRebirth2SkipChapterIntros(relocated,&FakeOps) && operations==0);
    Populate(); failAfterWrite=true;
    assert(!rebirths::InstallRebirth2SkipChapterIntros(relocated, &FakeOps) && ChapterActive()==0);
    assert(rebirths::owned_patch::IsOriginalExecutable(reinterpret_cast<uintptr_t>(fakeBase)+0x8ce0d,fixture::CallBytes(0x8ce0d,0x8ccf0)));
    Populate(); assert(Protect(fakeBase+0x4432a4,4,PAGE_READWRITE));
    *reinterpret_cast<uint32_t*>(fakeBase+0x4432a4)=1;
    assert(!rebirths::InstallRebirth2SkipChapterIntros(relocated, &FakeOps) && operations==0);
    std::puts("Re;Birth2 ADV fast-forward ABI, guards, and twenty-six-call/present rollback tests passed");
}
