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
constexpr size_t ImageSize = 0x510000;
unsigned char* fakeBase = nullptr;
unsigned requestCalls = 0, inputCalls = 0, skipCalls = 0, operations = 0;
unsigned queryCalls = 0, nepCalls = 0, dialogueCalls = 0, cleanupCalls = 0;
uint32_t physicalInput = 0;
unsigned inEngineCalls = 0, resultCalls = 0, closeCalls = 0, soundCalls = 0;
bool ready = false;
bool requestSuccess = true, failAfterWrite = false;
unsigned failOperation = 0;

uint32_t __fastcall OriginalRequest(uintptr_t adv, void*, uint32_t script) {
    ++requestCalls; assert(adv == reinterpret_cast<uintptr_t>(fakeBase + 0x4a0000)); assert(script == 302);
    return requestSuccess ? 1 : 0;
}
int __cdecl OriginalInput(uintptr_t adv) {
    ++inputCalls; assert(adv == reinterpret_cast<uintptr_t>(fakeBase + 0x4a0000)); return 37;
}
void __cdecl OriginalSkip(uintptr_t adv, uint32_t value) {
    ++skipCalls; assert(value == 1 && adv == reinterpret_cast<uintptr_t>(fakeBase + 0x4a0000));
    *reinterpret_cast<uint32_t*>(adv + 0x48) |= 8;
}
int __cdecl RequestInEngine(uintptr_t adv, uint32_t value) {
    ++inEngineCalls; assert(value == 1);
    *reinterpret_cast<uint32_t*>(adv + 0x48) |= 0x40;
    *reinterpret_cast<uintptr_t*>(adv + 0x28) = 0x1234;
    return 1;
}
int __fastcall OriginalResult(uintptr_t, void*, uint32_t sound) { ++resultCalls; assert(sound == 1); return 29; }
unsigned char __cdecl Ready(uintptr_t handle) { assert(handle == 0x1234); return ready; }
uintptr_t __cdecl Close(uintptr_t handle) { ++closeCalls; assert(handle == 0x1234); return 0; }
void __cdecl Sound(uint32_t id) { ++soundCalls; assert(id == 0); }

void __cdecl OriginalClear(uintptr_t adv) { *reinterpret_cast<uint32_t*>(adv + 0x48) &= ~0x1eU; }
int __fastcall OriginalQuery(uintptr_t, void*, uint32_t mask) { ++queryCalls; assert(mask == 0x100 || mask == 0x8000); return physicalInput & mask; }
int __fastcall OriginalNepInput(uintptr_t, void*) { return 41; }
int __fastcall OriginalDialogue(uintptr_t owner, void*) {
    ++dialogueCalls;
    if (!rebirths::testing::rebirth3_auto_skip::NepDialogueInput(0x2222, nullptr, 0x8000)) return 0;
    *reinterpret_cast<uint32_t*>(owner + 0x28) ^= 4;
    return 1;
}
void __fastcall OriginalCleanup(uintptr_t owner, void*) { ++cleanupCalls; std::memset(reinterpret_cast<void*>(owner), 0, 0x30); }

int __fastcall OriginalNepSkip(uintptr_t owner, void*) {
    ++nepCalls;
    if (!rebirths::testing::rebirth3_auto_skip::NepSkipInput(0x2222, nullptr, 0x100)) return 0;
    *reinterpret_cast<uintptr_t*>(owner + 0x10) = 0x1234;
    *reinterpret_cast<uint32_t*>(owner + 0x28) |= 0x1000;
    *reinterpret_cast<uintptr_t*>(owner + 0xc) = 0;
    return 1;
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
    return Mutate(sites[0].rva, sites[0].expected.data(), replacement.data(), 5) && !failAfterWrite && operations != failOperation;
}
bool FakeRetargetBytes(const rebirths::Context&, uint32_t rva, const unsigned char* expected,
                       const unsigned char* replacement, size_t count) noexcept {
    return Mutate(rva, expected, replacement, count);
}
const rebirths::AdvAutoSkipPatchOps FakeOps{FakeRetargetCalls, FakeRetargetBytes};

void Populate() {
    rebirths::testing::rebirth3_auto_skip::ResetOptions();
    assert(Protect(fakeBase, ImageSize, PAGE_EXECUTE_READWRITE)); std::memset(fakeBase, 0, ImageSize);
    const std::pair<uint32_t, uint32_t> sites[] = {
        {0x775e9, 0x7bd60}, {0x7765c, 0x7bd60}, {0x78d8e, 0x783e0}, {0x78dc6, 0x783e0}, {0x78e98, 0x783e0},
        {0x78934, 0x796c0}, {0x82b9c, 0x776a0}, {0x15c36f, 0x15bb20},
        {0x15bb36, 0x269ee0}, {0x15c2c8, 0x796c0}, {0x15c44e, 0x15ba90},
        {0x15c364, 0x15bc70}, {0x15c459, 0x15bc70}, {0x15bc8a, 0x269ee0}, {0x15b8d6, 0x15daf0},
    };
    for (const auto [source, target] : sites) { const auto call = fixture::CallBytes(source, target); std::memcpy(fakeBase + source, call.data(), 5); }
    assert(Protect(fakeBase, ImageSize, PAGE_EXECUTE_READ));
    requestCalls = inputCalls = skipCalls = operations = 0; requestSuccess = true; failAfterWrite = false; capturedLog.clear();
    inEngineCalls = resultCalls = closeCalls = soundCalls = 0; ready = false;
    failOperation = 0; queryCalls = nepCalls = dialogueCalls = cleanupCalls = 0; physicalInput = 0;
}
void Bind() {
    using namespace rebirths::testing::rebirth3_auto_skip;
    BindEventRequest(reinterpret_cast<EventRequestFn>(OriginalRequest)); BindStoryInput(OriginalInput); BindSetStorySkip(OriginalSkip);
    BindRequestInEngineSkip(RequestInEngine); BindPromptResult(reinterpret_cast<PromptResultFn>(OriginalResult));
    BindNepInput(reinterpret_cast<NepSkipFn>(OriginalNepInput));
    BindNepDialogue(reinterpret_cast<NepSkipFn>(OriginalDialogue));
    BindNepCleanup(reinterpret_cast<NepCleanupFn>(OriginalCleanup));
    BindStoryClear(OriginalClear); BindNepSkip(reinterpret_cast<NepSkipFn>(OriginalNepSkip));
    BindInputQuery(reinterpret_cast<InputQueryFn>(OriginalQuery));
    BindPromptReady(Ready); BindClosePrompt(Close); BindPlayUiSound(Sound);
}
void Prepare(uintptr_t adv) {
    assert(Protect(reinterpret_cast<void*>(adv), 0x8510, PAGE_READWRITE));
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 0; *reinterpret_cast<uint32_t*>(adv + 0x40) = 0;
    *reinterpret_cast<uint32_t*>(adv + 0x48) = 0; *reinterpret_cast<unsigned char*>(adv + 0x850c) = 6;
    *reinterpret_cast<unsigned char*>(adv + 0x850d) = 0;
    *reinterpret_cast<uintptr_t*>(adv + 0x14) = 0x5678; *reinterpret_cast<uintptr_t*>(adv + 0x28) = 0;
}
}

int main() {
    using namespace rebirths::testing::rebirth3_auto_skip;
    fixture::Image image(ImageSize); fakeBase = image.data();
    const rebirths::Context context{reinterpret_cast<HMODULE>(fakeBase), rebirths::GameSpecFor(rebirths::GameId::Rebirth3), L"", L""};
    const rebirths::Context wrong{reinterpret_cast<HMODULE>(fakeBase), rebirths::GameSpecFor(rebirths::GameId::Rebirth1), L"", L""};
    assert(!rebirths::InstallRebirth3AdvAutoSkip(wrong, &FakeOps));
    const uintptr_t adv = reinterpret_cast<uintptr_t>(fakeBase + 0x4a0000);

    Populate(); assert(rebirths::InstallRebirth3AdvAutoSkip(context, &FakeOps)); assert(operations == 11); Bind(); Prepare(adv);
    assert(EventRequest(adv, nullptr, 302) == 1); assert(StoryInput(adv) == 1); assert(skipCalls == 1 && inputCalls == 0);
    assert(StoryInput(adv) == 37 && skipCalls == 1 && inputCalls == 1);
    *reinterpret_cast<uint32_t*>(adv + 0x48) = 0; assert(StoryInput(adv) == 37);
    assert(EventRequest(adv, nullptr, 302) == 1); assert(StoryInput(adv) == 1 && skipCalls == 2);

    Prepare(adv); requestSuccess = false; assert(EventRequest(adv, nullptr, 302) == 0); assert(StoryInput(adv) == 37);
    requestSuccess = true; assert(EventRequest(adv, nullptr, 302));
    *reinterpret_cast<unsigned char*>(adv + 0x850c) = 5; assert(StoryInput(adv) == 37);
    *reinterpret_cast<unsigned char*>(adv + 0x850c) = 6; *reinterpret_cast<uint32_t*>(adv + 0x40) = 8; assert(StoryInput(adv) == 37);
    *reinterpret_cast<uint32_t*>(adv + 0x40) = 0; assert(StoryInput(adv) == 1 && skipCalls == 3);

    // Mode one opens exactly one native prompt, waits for readiness and frees it
    // before returning Yes to the original caller. No normal story setter call.
    Prepare(adv); assert(EventRequest(adv, nullptr, 302));
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 1;
    *reinterpret_cast<uint32_t*>(adv + 0x40) = 8; assert(StoryInput(adv) == 37 && inEngineCalls == 0);
    *reinterpret_cast<uint32_t*>(adv + 0x40) = 0;
    assert(StoryInput(adv) == 1 && inEngineCalls == 1 && skipCalls == 3);
    assert(InEnginePromptResult(adv + 0x28, nullptr, 1) == 0 && closeCalls == 0);
    assert(InEnginePromptResult(adv + 0x2c, nullptr, 1) == 29 && resultCalls == 1);
    ready = true;
    assert(InEnginePromptResult(adv + 0x28, nullptr, 1) == 1 && closeCalls == 1 && soundCalls == 1);
    assert(*reinterpret_cast<uintptr_t*>(adv + 0x28) == 0);
    assert(InEnginePromptResult(adv + 0x28, nullptr, 1) == 29);
    *reinterpret_cast<uint32_t*>(adv + 0x48) = 0; // Native battle handoff clears flags.
    assert(StoryInput(adv) == 37 && inEngineCalls == 1);

    // A fresh event must not inherit ownership of the previous prompt.
    Prepare(adv); assert(EventRequest(adv, nullptr, 302));
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 1; assert(StoryInput(adv) == 1);
    assert(EventRequest(adv, nullptr, 302));
    assert(InEnginePromptResult(adv + 0x28, nullptr, 1) == 29 && closeCalls == 1);
    // Already skipping and unknown modes do not initiate mode-one skip.
    Prepare(adv); assert(EventRequest(adv, nullptr, 302));
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 2; assert(StoryInput(adv) == 37);
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 1; *reinterpret_cast<uint32_t*>(adv + 0x48) = 0x108;
    assert(StoryInput(adv) == 37); *reinterpret_cast<uint32_t*>(adv + 0x48) = 0;
    assert(StoryInput(adv) == 37 && inEngineCalls == 2);

    Prepare(adv); assert(EventRequest(adv, nullptr, 302));
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 1;
    *reinterpret_cast<uint32_t*>(adv + 0x48) = 0x40; // A user-owned prompt is untouched.
    assert(StoryInput(adv) == 37 && inEngineCalls == 2);
    assert(InEnginePromptResult(adv + 0x28, nullptr, 1) == 29);
    *reinterpret_cast<uint32_t*>(adv + 0x48) = 0;
    assert(StoryInput(adv) == 1);
    *reinterpret_cast<uintptr_t*>(adv + 0x14) = 0x9999;
    assert(InEnginePromptResult(adv + 0x28, nullptr, 1) == 29 && closeCalls == 1);

    // Native Help suspends an already-skipping event, then restores input. The
    // same owner/request needs another activation; arbitrary clears do not.
    Populate(); assert(rebirths::InstallRebirth3AdvAutoSkip(context, &FakeOps)); Bind(); Prepare(adv);
    assert(EventRequest(adv, nullptr, 302) && StoryInput(adv) == 1);
    *reinterpret_cast<uint32_t*>(adv + 0x40) = 15;
    TutorialClear(adv); assert(!(*reinterpret_cast<uint32_t*>(adv + 0x48) & 8));
    assert(StoryInput(adv) == 37 && skipCalls == 1);
    // A consecutive Help starts before any opportunity to re-enable skip.
    TutorialClear(adv); assert(StoryInput(adv) == 37);
    *reinterpret_cast<uint32_t*>(adv + 0x40) = 0;
    assert(StoryInput(adv) == 1 && skipCalls == 2);
    assert(StoryInput(adv) == 37 && skipCalls == 2);
    OriginalClear(adv); TutorialClear(adv); assert(StoryInput(adv) == 37); // User cancelled.
    Prepare(adv); assert(EventRequest(adv, nullptr, 302) && StoryInput(adv) == 1);
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 1;
    TutorialClear(adv); assert(StoryInput(adv) == 37); // No new mode-one battle epoch.

    // Execute independent full skip through its actual nested input seam.
    // A requestless part is valid; foreign prompts, tasks and parts stay native.
    Populate(); assert(rebirths::InstallRebirth3NepstationSkip(context, &FakeOps)); assert(operations == 4); Bind();
    const auto nep = reinterpret_cast<uintptr_t>(fakeBase + 0x4b0000);
    assert(Protect(reinterpret_cast<void*>(nep), 0x30, PAGE_READWRITE));
    assert(Protect(fakeBase + 0x505ccc, 4, PAGE_READWRITE));
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x505ccc) = 0xdead;
    *reinterpret_cast<uintptr_t*>(nep) = 0xdead;
    *reinterpret_cast<uintptr_t*>(nep + 4) = 99;
    *reinterpret_cast<unsigned char*>(nep + 0x2c) = 3;
    assert(NepSkipInput(0x2222, nullptr, 0x100) == 0 && queryCalls == 1); // Outside scope.
    assert(NepStoryInput(nep, nullptr) == 1 && nepCalls == 1 && queryCalls == 1);
    assert(*reinterpret_cast<uint32_t*>(nep + 0x28) == 0x1000); // No speed/autoplay bits.
    assert(NepPromptResult(nep + 0x10, nullptr, 1) == 0 && closeCalls == 0);
    assert(NepPromptResult(nep + 0x14, nullptr, 1) == 29 && closeCalls == 0);
    ready = true;
    *reinterpret_cast<uintptr_t*>(nep + 4) = 100;
    assert(NepPromptResult(nep + 0x10, nullptr, 1) == 29 && closeCalls == 0);
    *reinterpret_cast<uintptr_t*>(nep + 4) = 99;
    assert(NepPromptResult(nep + 0x10, nullptr, 1) == 1 && closeCalls == 1 && soundCalls == 1);
    assert(!*reinterpret_cast<uintptr_t*>(nep + 0x10));
    assert(NepPromptResult(nep + 0x10, nullptr, 1) == 29 && closeCalls == 1);
    *reinterpret_cast<uint32_t*>(nep + 0x28) = 0;
    *reinterpret_cast<uintptr_t*>(nep + 0x10) = 0x9876;
    assert(NepStoryInput(nep, nullptr) == 41); // Never adopt a user's prompt.
    *reinterpret_cast<uintptr_t*>(nep + 0x10) = 0;
    *reinterpret_cast<unsigned char*>(nep + 0x2c) = 2;
    assert(NepSkip(nep, nullptr) == 0);
    *reinterpret_cast<unsigned char*>(nep + 0x2c) = 3;
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x505ccc) = 0xbeef;
    assert(NepStoryInput(nep, nullptr) == 41);

    // AdvAutoSkip owns only one native I activation, with no full-skip prompt.
    Populate(); assert(rebirths::InstallRebirth3AdvAutoSkip(context, &FakeOps)); Bind();
    assert(Protect(reinterpret_cast<void*>(nep), 0x30, PAGE_READWRITE));
    assert(Protect(fakeBase + 0x505ccc, 4, PAGE_READWRITE));
    auto prepareNep = [&] {
        *reinterpret_cast<uintptr_t*>(fakeBase + 0x505ccc) = 0xdead;
        *reinterpret_cast<uintptr_t*>(nep) = 0xdead;
        *reinterpret_cast<uintptr_t*>(nep + 4) = 99;
        *reinterpret_cast<unsigned char*>(nep + 0x2c) = 3;
    };
    prepareNep();
    assert(NepDialogueInput(0x2222, nullptr, 0x8000) == 0); // Outside helper scope.
    assert(NepDialogue(nep, nullptr) == 1 && dialogueCalls == 1);
    assert(*reinterpret_cast<uint32_t*>(nep + 0x28) == 4 && closeCalls == 0 && nepCalls == 0);
    assert(!*reinterpret_cast<uintptr_t*>(nep + 0x10));
    assert(NepDialogue(nep, nullptr) == 0); // Never toggle back off automatically.
    physicalInput = 0x8000; assert(NepDialogue(nep, nullptr) == 1); // Manual cancellation.
    physicalInput = 0; assert(NepDialogue(nep, nullptr) == 0);
    assert(!*reinterpret_cast<uint32_t*>(nep + 0x28));
    NepCleanup(nep, nullptr); assert(cleanupCalls == 1);
    prepareNep(); assert(NepDialogue(nep, nullptr) == 1); // Exact task/address reuse.
    *reinterpret_cast<uintptr_t*>(nep + 4) = 100;
    *reinterpret_cast<uint32_t*>(nep + 0x28) = 4;
    assert(NepDialogue(nep, nullptr) == 0); // Already-active mode consumes epoch.
    *reinterpret_cast<uint32_t*>(nep + 0x28) = 0;
    assert(NepDialogue(nep, nullptr) == 0);
    *reinterpret_cast<uintptr_t*>(nep + 4) = 101;
    *reinterpret_cast<uintptr_t*>(nep + 0x10) = 0x9876;
    assert(NepDialogue(nep, nullptr) == 0); // User prompt stays native.
    *reinterpret_cast<uintptr_t*>(nep + 0x10) = 0;
    *reinterpret_cast<unsigned char*>(nep + 0x2c) = 2;
    assert(NepDialogue(nep, nullptr) == 0);
    *reinterpret_cast<unsigned char*>(nep + 0x2c) = 3;
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x505ccc) = 0xbeef;
    assert(NepDialogue(nep, nullptr) == 0);

    // Both installs coexist without overlapping CALLs; full skip takes priority.
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x505ccc) = 0;
    assert(rebirths::InstallRebirth3NepstationSkip(context, &FakeOps)); Bind(); prepareNep();
    assert(NepDialogue(nep, nullptr) == 0 && !*reinterpret_cast<uint32_t*>(nep + 0x28));
    assert(NepStoryInput(nep, nullptr) == 1 && *reinterpret_cast<uint32_t*>(nep + 0x28) == 0x1000);

    // A running Nepstation task cannot be adopted by late installation.
    Populate(); assert(Protect(fakeBase + 0x505ccc, 4, PAGE_READWRITE));
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x505ccc) = 0xdead;
    assert(!rebirths::InstallRebirth3AdvAutoSkip(context, &FakeOps) && operations == 0);
    Bind(); assert(NepStoryInput(0, nullptr) == 41); // Disabled adapter forwards.

    const std::pair<uint32_t, uint32_t> allSites[] = {
        {0x775e9,0x7bd60},{0x7765c,0x7bd60},{0x78d8e,0x783e0},
        {0x78dc6,0x783e0},{0x78e98,0x783e0},{0x78934,0x796c0},
        {0x82b9c,0x776a0},{0x15c364,0x15bc70},{0x15c459,0x15bc70},{0x15bc8a,0x269ee0},{0x15b8d6,0x15daf0},
    };
    for (unsigned failure = 1; failure <= 11; ++failure) {
        Populate(); failOperation = failure;
        assert(!rebirths::InstallRebirth3AdvAutoSkip(context, &FakeOps));
        for (const auto [source,target] : allSites)
            assert(rebirths::owned_patch::IsOriginalExecutable(reinterpret_cast<uintptr_t>(fakeBase + source), fixture::CallBytes(source,target)));
    }
    const std::pair<uint32_t, uint32_t> fullSites[] = {
        {0x15c36f,0x15bb20},{0x15bb36,0x269ee0},{0x15c2c8,0x796c0},{0x15c44e,0x15ba90},
    };
    for (unsigned failure = 1; failure <= 4; ++failure) {
        Populate(); failOperation = failure;
        assert(!rebirths::InstallRebirth3NepstationSkip(context, &FakeOps));
        for (const auto [source,target] : fullSites)
            assert(rebirths::owned_patch::IsOriginalExecutable(reinterpret_cast<uintptr_t>(fakeBase + source), fixture::CallBytes(source,target)));
    }
    Populate();
    assert(Protect(fakeBase + 0x78934, 5, PAGE_EXECUTE_READWRITE));
    fakeBase[0x78934] = 0x90;
    assert(Protect(fakeBase + 0x78934, 5, PAGE_EXECUTE_READ));
    assert(!rebirths::InstallRebirth3AdvAutoSkip(context, &FakeOps) && operations == 0);

    Populate(); failAfterWrite = true; assert(!rebirths::InstallRebirth3AdvAutoSkip(context, &FakeOps));
    Bind(); Prepare(adv); assert(EventRequest(adv, nullptr, 302) == 1); assert(StoryInput(adv) == 37);
    std::puts("Re;Birth3 ADV auto-skip ABI, epoch, tutorial resume, Nepstation I-mode and independent full skip, mode-one lifecycle, eligibility, and rollback tests passed");
}
