#ifdef REBIRTHS_TEST_CONTRACTS
#include "adv_test_contracts.hpp"
#endif
#include "rebirth3_adv_auto_skip.hpp"

#include "owned_patch_install.hpp"
#include "adv_operations.hpp"

#include <array>
#include <cstring>

namespace rebirths {
namespace rebirth3_auto_skip {
namespace {
using owned_patch::CallTo;

constexpr uint32_t StoryInputRva = 0x000783e0;
constexpr uint32_t StorySkipRva = 0x00077990;
constexpr uint32_t EventRequestRva = 0x0007bd60;
constexpr uint32_t InEngineRequestRva = 0x0007cfd0;
constexpr uint32_t PromptResultRva = 0x000796c0;
constexpr uint32_t PromptReadyRva = 0x0018bd90;
constexpr uint32_t PromptCloseRva = 0x0018bd70;
constexpr uint32_t UiSoundRva = 0x00272490;
constexpr uint32_t ActiveAdvPointerRva = 0x0049a484;
constexpr uint32_t AdvFlagsOffset = 0x48;
constexpr uint32_t AdvModeOffset = 0x10;
constexpr uint32_t InputInhibitOffset = 0x40;
constexpr uint32_t StateOffset = 0x850c;
constexpr uint32_t InputDisabledOffset = 0x850d;

uintptr_t gameBase = 0;
using EventRequestFn = uint32_t (__thiscall*)(uintptr_t, uint32_t);
using StoryInputFn = int (__cdecl*)(uintptr_t);
using StorySkipFn = void (__cdecl*)(uintptr_t, uint32_t);
using InEngineRequestFn = int (__cdecl*)(uintptr_t, uint32_t);
using PromptResultFn = int (__thiscall*)(uintptr_t, uint32_t);
using PromptReadyFn = unsigned char (__cdecl*)(uintptr_t);
using PromptCloseFn = uintptr_t (__cdecl*)(uintptr_t);
using StoryClearFn = void (__cdecl*)(uintptr_t);
using NepSkipFn = int (__thiscall*)(uintptr_t);
using InputQueryFn = int (__thiscall*)(uintptr_t, uint32_t);
using NepCleanupFn = void (__thiscall*)(uintptr_t);
using UiSoundFn = void (__cdecl*)(uint32_t);
EventRequestFn originalEventRequest = nullptr;
StoryInputFn originalStoryInput = nullptr;
StorySkipFn setStorySkip = nullptr;
InEngineRequestFn requestInEngineSkip = nullptr;
PromptResultFn originalPromptResult = nullptr;
PromptReadyFn promptReady = nullptr;
PromptCloseFn closePrompt = nullptr;
UiSoundFn playUiSound = nullptr;
StoryClearFn clearStorySkip = nullptr;
NepSkipFn nativeNepSkip = nullptr;
NepSkipFn originalNepInput = nullptr;
InputQueryFn originalInputQuery = nullptr;
NepSkipFn nativeDialogue = nullptr;
NepCleanupFn nativeNepCleanup = nullptr;
volatile LONG fullEnabled = 0;
uintptr_t scopedDialogueOwner = 0;
bool scopedDialogueTriggered = false;
struct DialogueEpoch { uintptr_t owner = 0, task = 0, part = 0, request = 0; } dialogueEpoch;
uintptr_t scopedNepOwner = 0;
bool scopedNepTriggered = false;
struct NepPromptOwnership {
    uintptr_t owner = 0, task = 0, part = 0, request = 0, prompt = 0;
} nepPrompt;
uintptr_t ownedPrompt = 0, ownedRequest = 0;
PVOID epochAdv = nullptr;
volatile LONG epochPending = 0;
volatile LONG activationCount = 0;
volatile LONG enabled = 0;


bool Eligible(uintptr_t adv) noexcept {
    return adv && *reinterpret_cast<const unsigned char*>(adv + StateOffset) == 6 &&
        *reinterpret_cast<const unsigned char*>(adv + InputDisabledOffset) == 0 &&
        (*reinterpret_cast<const uint32_t*>(adv + InputInhibitOffset) & 8) == 0 &&
        *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) <= 1;
}

int ShouldAutoSkip(uintptr_t adv) noexcept {
    if (!enabled || reinterpret_cast<uintptr_t>(InterlockedCompareExchangePointer(&epochAdv, nullptr, nullptr)) != adv ||
        !Eligible(adv)) return 0;
    const auto mode = *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset);
    const auto flags = *reinterpret_cast<const uint32_t*>(adv + AdvFlagsOffset);
    // Never take over a prompt opened by another native action.
    if (mode == 1 && ((flags & 0x2c0) || *reinterpret_cast<const uintptr_t*>(adv + 0x28))) return 0;
    if (!adv_operations::ConsumeEpoch<false>(epochPending)) return 0;
    if (mode == 1) {
        if ((flags & 0x108) || !*reinterpret_cast<const uintptr_t*>(adv + 0x14)) return 0;
        const int result = requestInEngineSkip(adv, 1);
        ownedPrompt = *reinterpret_cast<const uintptr_t*>(adv + 0x28);
        ownedRequest = ownedPrompt ? *reinterpret_cast<const uintptr_t*>(adv + 0x14) : 0;
        Log("AdvAutoSkip in-engine request game=rebirth3 event=%ld prompt=%s",
            InterlockedIncrement(&activationCount), ownedPrompt ? "created" : "unavailable");
        return result;
    }
    if (*reinterpret_cast<const uint32_t*>(adv + AdvFlagsOffset) & 8) return 0;
    setStorySkip(adv, 1);
    Log("AdvAutoSkip activation game=rebirth3 event=%ld", InterlockedIncrement(&activationCount));
    return 1;
}

uint32_t __fastcall EventRequest(uintptr_t adv, void*, uint32_t script) noexcept {
    const uint32_t result = originalEventRequest(adv, script);
    if (result) {
        ownedPrompt = ownedRequest = 0;
        InterlockedExchangePointer(&epochAdv, reinterpret_cast<PVOID>(adv));
        InterlockedExchange(&epochPending, 1);
    }
    return result;
}

int __cdecl StoryInput(uintptr_t adv) noexcept {
    if (ShouldAutoSkip(adv)) return 1;
    return originalStoryInput(adv);
}

int __fastcall InEnginePromptResult(uintptr_t prompt, void*, uint32_t sound) noexcept {
    const auto adv = reinterpret_cast<uintptr_t>(InterlockedCompareExchangePointer(&epochAdv, nullptr, nullptr));
    if (!enabled || !adv || prompt != adv + 0x28 || !ownedPrompt ||
        *reinterpret_cast<const uintptr_t*>(prompt) != ownedPrompt ||
        *reinterpret_cast<const uintptr_t*>(adv + 0x14) != ownedRequest ||
        *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) != 1 ||
        *reinterpret_cast<const unsigned char*>(adv + StateOffset) != 6 ||
        (*reinterpret_cast<const uint32_t*>(adv + AdvFlagsOffset) & 0x148) != 0x40)
        return originalPromptResult(prompt, sound);
    // Match the native prompt's readiness, sound and destruction protocol. The
    // original caller then executes its complete Yes branch (including VM lane
    // cleanup and the irrevocable skip flag); do not set the story skip bit here.
    if (!promptReady(ownedPrompt)) return 0;
    if (sound) playUiSound(0);
    *reinterpret_cast<uintptr_t*>(prompt) = closePrompt(ownedPrompt);
    ownedPrompt = ownedRequest = 0;
    Log("AdvAutoSkip in-engine confirmed game=rebirth3");
    return 1;
}

// This adapter is reached only by Help's native entry clear. All other skip
// clears (manual cancellation, battle handoff, presentation teardown) stay native.
void __cdecl TutorialClear(uintptr_t adv) noexcept {
    const bool resume = enabled && adv &&
        reinterpret_cast<uintptr_t>(InterlockedCompareExchangePointer(&epochAdv, nullptr, nullptr)) == adv &&
        *reinterpret_cast<const uintptr_t*>(adv + 0x14) &&
        *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0 &&
        *reinterpret_cast<const unsigned char*>(adv + StateOffset) == 6 &&
        (*reinterpret_cast<const uint32_t*>(adv + AdvFlagsOffset) & 8);
    clearStorySkip(adv);
    if (resume) InterlockedExchange(&epochPending, 1);
}

bool LiveNep(uintptr_t owner) noexcept {
    return owner && *reinterpret_cast<const uintptr_t*>(owner) &&
        *reinterpret_cast<const uintptr_t*>(owner) == *reinterpret_cast<const uintptr_t*>(gameBase + 0x505ccc) &&
        *reinterpret_cast<const unsigned char*>(owner + 0x2c) == 3;
}

int __fastcall NepSkipInput(uintptr_t input, void*, uint32_t mask) noexcept {
    // Scoped to the native skip helper, after its caller's presentation waits.
    // Never manufacture input in the general input dispatcher.
    if (fullEnabled && mask == 0x100 && LiveNep(scopedNepOwner) &&
        !*reinterpret_cast<const uintptr_t*>(scopedNepOwner + 0x10) &&
        !(*reinterpret_cast<const uint32_t*>(scopedNepOwner + 0x28) & 0x1000)) {
        scopedNepTriggered = true;
        return 0x100;
    }
    return originalInputQuery(input, mask);
}

int __fastcall NepSkip(uintptr_t owner, void*) noexcept {
    const uintptr_t priorOwner = scopedNepOwner;
    const bool priorTriggered = scopedNepTriggered;
    scopedNepOwner = owner;
    scopedNepTriggered = false;
    const int result = nativeNepSkip(owner);
    if (scopedNepTriggered && result && LiveNep(owner)) {
        nepPrompt = {owner, *reinterpret_cast<const uintptr_t*>(owner),
            *reinterpret_cast<const uintptr_t*>(owner + 4),
            *reinterpret_cast<const uintptr_t*>(owner + 8),
            *reinterpret_cast<const uintptr_t*>(owner + 0x10)};
        Log("NepstationSkip request game=rebirth3 prompt=%s", nepPrompt.prompt ? "created" : "unavailable");
    }
    scopedNepOwner = priorOwner;
    scopedNepTriggered = priorTriggered;
    return result;
}

// Normal presentation reaches a different input branch than autoplay. This
// boundary follows its text/presentation waits; request the same native full
// skip prompt without changing autoplay or either presentation speed flag.
int __fastcall NepStoryInput(uintptr_t owner, void*) noexcept {
    if (fullEnabled && LiveNep(owner) && !*reinterpret_cast<const uintptr_t*>(owner + 0x10) &&
        !(*reinterpret_cast<const uint32_t*>(owner + 0x28) & 0x1000) && NepSkip(owner, nullptr))
        return 1;
    return originalNepInput(owner);
}

int __fastcall NepPromptResult(uintptr_t slot, void*, uint32_t sound) noexcept {
    const auto owner = nepPrompt.owner;
    if (!fullEnabled || slot != owner + 0x10 || !nepPrompt.prompt || !LiveNep(owner) ||
        *reinterpret_cast<const uintptr_t*>(owner) != nepPrompt.task ||
        *reinterpret_cast<const uintptr_t*>(owner + 4) != nepPrompt.part ||
        *reinterpret_cast<const uintptr_t*>(owner + 8) != nepPrompt.request ||
        *reinterpret_cast<const uintptr_t*>(slot) != nepPrompt.prompt ||
        !(*reinterpret_cast<const uint32_t*>(owner + 0x28) & 0x1000))
        return originalPromptResult(slot, sound);
    if (!promptReady(nepPrompt.prompt)) return 0;
    if (sound) playUiSound(0);
    *reinterpret_cast<uintptr_t*>(slot) = closePrompt(nepPrompt.prompt);
    nepPrompt = {};
    Log("NepstationSkip confirmed game=rebirth3");
    // The unchanged native caller sets state4; NepstaPart retires its own task.
    return 1;
}

// The I-key mode keeps the normal native dialogue/presentation path. Supply
// its edge only inside the original I helper, once per task/part/request.
int __fastcall NepDialogueInput(uintptr_t input, void*, uint32_t mask) noexcept {
    if (enabled && !fullEnabled && mask == 0x8000 && LiveNep(scopedDialogueOwner) &&
        !(*reinterpret_cast<const uint32_t*>(scopedDialogueOwner + 0x28) & 4)) {
        scopedDialogueTriggered = true;
        return 0x8000;
    }
    return originalInputQuery(input, mask);
}

int __fastcall NepDialogue(uintptr_t owner, void*) noexcept {
    if (!enabled || fullEnabled || !LiveNep(owner) || *reinterpret_cast<const uintptr_t*>(owner + 0x10))
        return nativeDialogue(owner);
    const DialogueEpoch current{owner, *reinterpret_cast<const uintptr_t*>(owner),
        *reinterpret_cast<const uintptr_t*>(owner + 4), *reinterpret_cast<const uintptr_t*>(owner + 8)};
    if (dialogueEpoch.owner == current.owner && dialogueEpoch.task == current.task &&
        dialogueEpoch.part == current.part && dialogueEpoch.request == current.request)
        return nativeDialogue(owner);
    dialogueEpoch = current; // Also consume an already-active native mode.
    const auto priorOwner = scopedDialogueOwner;
    const bool priorTriggered = scopedDialogueTriggered;
    scopedDialogueOwner = owner;
    scopedDialogueTriggered = false;
    const int result = nativeDialogue(owner);
    if (scopedDialogueTriggered && result)
        Log("AdvAutoSkip Nepstation dialogue activation game=rebirth3");
    scopedDialogueOwner = priorOwner;
    scopedDialogueTriggered = priorTriggered;
    return result;
}

void __fastcall NepCleanup(uintptr_t owner, void*) noexcept {
    if (dialogueEpoch.owner == owner) dialogueEpoch = {};
    nativeNepCleanup(owner);
}

using PreparedSite = owned_patch::PreparedCall;
using owned_patch::IsOriginalExecutable;
using owned_patch::BytesAt;
} // namespace

bool Install(const Context& context, const AdvAutoSkipPatchOps* injectedOps) noexcept {
    const AdvAutoSkipPatchOps productionOps{RetargetCalls, RetargetBytes};
    const auto& ops = injectedOps ? *injectedOps : productionOps;
    if (context.spec.id != GameId::Rebirth3) { Log("AdvAutoSkip rebirth3 wrong target"); return false; }
    if (!ops.retargetCalls || !ops.retargetBytes) { Log("AdvAutoSkip rebirth3 unavailable patch operations"); return false; }
    gameBase = reinterpret_cast<uintptr_t>(context.game);
    originalEventRequest = reinterpret_cast<EventRequestFn>(gameBase + EventRequestRva);
    originalStoryInput = reinterpret_cast<StoryInputFn>(gameBase + StoryInputRva);
    setStorySkip = reinterpret_cast<StorySkipFn>(gameBase + StorySkipRva);
    requestInEngineSkip = reinterpret_cast<InEngineRequestFn>(gameBase + InEngineRequestRva);
    originalPromptResult = reinterpret_cast<PromptResultFn>(gameBase + PromptResultRva);
    promptReady = reinterpret_cast<PromptReadyFn>(gameBase + PromptReadyRva);
    closePrompt = reinterpret_cast<PromptCloseFn>(gameBase + PromptCloseRva);
    playUiSound = reinterpret_cast<UiSoundFn>(gameBase + UiSoundRva);
    clearStorySkip = reinterpret_cast<StoryClearFn>(gameBase + 0x776a0);
    originalInputQuery = reinterpret_cast<InputQueryFn>(gameBase + 0x269ee0);
    nativeDialogue = reinterpret_cast<NepSkipFn>(gameBase + 0x15bc70);
    nativeNepCleanup = reinterpret_cast<NepCleanupFn>(gameBase + 0x15daf0);
    scopedDialogueOwner = 0;
    scopedDialogueTriggered = false;
    dialogueEpoch = {};
    ownedPrompt = ownedRequest = 0;
    InterlockedExchangePointer(&epochAdv, nullptr);
    InterlockedExchange(&epochPending, 0);
    InterlockedExchange(&activationCount, 0);
    InterlockedExchange(&enabled, 0);
    std::array<PreparedSite, 11> sites{{
        {{0x000775e9, {0xe8,0x72,0x47,0x00,0x00}, reinterpret_cast<void*>(&EventRequest)}},
        {{0x0007765c, {0xe8,0xff,0x46,0x00,0x00}, reinterpret_cast<void*>(&EventRequest)}},
        {{0x00078d8e, {0xe8,0x4d,0xf6,0xff,0xff}, reinterpret_cast<void*>(&StoryInput)}},
        {{0x00078dc6, {0xe8,0x15,0xf6,0xff,0xff}, reinterpret_cast<void*>(&StoryInput)}},
        {{0x00078e98, {0xe8,0x43,0xf5,0xff,0xff}, reinterpret_cast<void*>(&StoryInput)}},
        {{0x00078934, {0xe8,0x87,0x0d,0x00,0x00}, reinterpret_cast<void*>(&InEnginePromptResult)}},
        {{0x00082b9c, {0xe8,0xff,0x4a,0xff,0xff}, reinterpret_cast<void*>(&TutorialClear)}},
        {{0x0015c364, {0xe8,0x07,0xf9,0xff,0xff}, reinterpret_cast<void*>(&NepDialogue)}},
        {{0x0015c459, {0xe8,0x12,0xf8,0xff,0xff}, reinterpret_cast<void*>(&NepDialogue)}},
        {{0x0015bc8a, {0xe8,0x51,0xe2,0x10,0x00}, reinterpret_cast<void*>(&NepDialogueInput)}},
        {{0x0015b8d6, {0xe8,0x15,0x22,0x00,0x00}, reinterpret_cast<void*>(&NepCleanup)}},
    }};
    if (*reinterpret_cast<const uintptr_t*>(gameBase + 0x505ccc)) {
        Log("AdvAutoSkip rebirth3 late Nepstation install refused"); return false;
    }
    const uintptr_t currentAdv = *reinterpret_cast<uintptr_t*>(gameBase + ActiveAdvPointerRva);
    if (currentAdv && *reinterpret_cast<uintptr_t*>(currentAdv + 0x14)) {
        Log("AdvAutoSkip rebirth3 late install refused"); return false;
    }
    const PatchResult result = owned_patch::Install(context, sites, ops, "AdvAutoSkip rebirth3");
    if (!result.Succeeded()) return false;
    InterlockedExchange(&enabled, 1);
    Log("AdvAutoSkip installed eleven guarded lifecycle calls game=rebirth3");
    return true;
}

bool InstallFull(const Context& context, const AdvAutoSkipPatchOps* injectedOps) noexcept {
    if (context.spec.id != GameId::Rebirth3) { Log("NepstationSkip wrong target"); return false; }
    const AdvAutoSkipPatchOps productionOps{RetargetCalls, RetargetBytes};
    const auto& ops = injectedOps ? *injectedOps : productionOps;
    if (!ops.retargetCalls || !ops.retargetBytes) return false;
    gameBase = reinterpret_cast<uintptr_t>(context.game);
    nativeNepSkip = reinterpret_cast<NepSkipFn>(gameBase + 0x15bb20);
    originalNepInput = reinterpret_cast<NepSkipFn>(gameBase + 0x15ba90);
    originalInputQuery = reinterpret_cast<InputQueryFn>(gameBase + 0x269ee0);
    originalPromptResult = reinterpret_cast<PromptResultFn>(gameBase + PromptResultRva);
    promptReady = reinterpret_cast<PromptReadyFn>(gameBase + PromptReadyRva);
    closePrompt = reinterpret_cast<PromptCloseFn>(gameBase + PromptCloseRva);
    playUiSound = reinterpret_cast<UiSoundFn>(gameBase + UiSoundRva);
    scopedNepOwner = 0;
    scopedNepTriggered = false;
    nepPrompt = {};
    InterlockedExchange(&fullEnabled, 0);
    if (*reinterpret_cast<const uintptr_t*>(gameBase + 0x505ccc)) {
        Log("NepstationSkip late install refused"); return false;
    }
    std::array<PreparedSite, 4> sites{{
        {{0x0015c36f, {0xe8,0xac,0xf7,0xff,0xff}, reinterpret_cast<void*>(&NepSkip)}},
        {{0x0015bb36, {0xe8,0xa5,0xe3,0x10,0x00}, reinterpret_cast<void*>(&NepSkipInput)}},
        {{0x0015c2c8, {0xe8,0xf3,0xd3,0xf1,0xff}, reinterpret_cast<void*>(&NepPromptResult)}},
        {{0x0015c44e, {0xe8,0x3d,0xf6,0xff,0xff}, reinterpret_cast<void*>(&NepStoryInput)}},
    }};
    if (!owned_patch::Install(context, sites, ops, "NepstationSkip rebirth3").Succeeded()) return false;
    InterlockedExchange(&fullEnabled, 1);
    Log("NepstationSkip installed four guarded calls game=rebirth3");
    return true;
}

} // namespace rebirth3_auto_skip

bool InstallRebirth3AdvAutoSkip(const Context& context, const AdvAutoSkipPatchOps* ops) noexcept {
    return rebirth3_auto_skip::Install(context, ops);
}

bool InstallRebirth3NepstationSkip(const Context& context, const AdvAutoSkipPatchOps* ops) noexcept {
    return rebirth3_auto_skip::InstallFull(context, ops);
}

} // namespace rebirths

#ifdef REBIRTHS_TEST_CONTRACTS
namespace rebirths::testing::rebirth3_auto_skip {
void ResetOptions() noexcept { InterlockedExchange(&rebirths::rebirth3_auto_skip::enabled, 0); InterlockedExchange(&rebirths::rebirth3_auto_skip::fullEnabled, 0); }
void BindNepDialogue(NepSkipFn callback) noexcept { rebirths::rebirth3_auto_skip::nativeDialogue = callback; }
void BindNepCleanup(NepCleanupFn callback) noexcept { rebirths::rebirth3_auto_skip::nativeNepCleanup = callback; }
int __fastcall NepDialogue(uintptr_t owner, void* unused) noexcept { return rebirths::rebirth3_auto_skip::NepDialogue(owner, unused); }
int __fastcall NepDialogueInput(uintptr_t input, void* unused, uint32_t mask) noexcept { return rebirths::rebirth3_auto_skip::NepDialogueInput(input, unused, mask); }
void __fastcall NepCleanup(uintptr_t owner, void* unused) noexcept { rebirths::rebirth3_auto_skip::NepCleanup(owner, unused); }
void BindStoryClear(StoryClearFn callback) noexcept { rebirths::rebirth3_auto_skip::clearStorySkip = callback; }
void BindNepInput(NepSkipFn callback) noexcept { rebirths::rebirth3_auto_skip::originalNepInput = callback; }
int __fastcall NepStoryInput(uintptr_t owner, void* unused) noexcept { return rebirths::rebirth3_auto_skip::NepStoryInput(owner, unused); }
void BindNepSkip(NepSkipFn callback) noexcept { rebirths::rebirth3_auto_skip::nativeNepSkip = callback; }
void BindInputQuery(InputQueryFn callback) noexcept { rebirths::rebirth3_auto_skip::originalInputQuery = callback; }
void __cdecl TutorialClear(uintptr_t adv) noexcept { rebirths::rebirth3_auto_skip::TutorialClear(adv); }
int __fastcall NepSkip(uintptr_t owner, void* unused) noexcept { return rebirths::rebirth3_auto_skip::NepSkip(owner, unused); }
int __fastcall NepSkipInput(uintptr_t input, void* unused, uint32_t mask) noexcept { return rebirths::rebirth3_auto_skip::NepSkipInput(input, unused, mask); }
int __fastcall NepPromptResult(uintptr_t slot, void* unused, uint32_t sound) noexcept { return rebirths::rebirth3_auto_skip::NepPromptResult(slot, unused, sound); }
void BindEventRequest(EventRequestFn callback) noexcept { rebirths::rebirth3_auto_skip::originalEventRequest = callback; }
void BindStoryInput(StoryInputFn callback) noexcept { rebirths::rebirth3_auto_skip::originalStoryInput = callback; }
void BindSetStorySkip(StorySkipFn callback) noexcept { rebirths::rebirth3_auto_skip::setStorySkip = callback; }
void BindRequestInEngineSkip(InEngineRequestFn callback) noexcept { rebirths::rebirth3_auto_skip::requestInEngineSkip = callback; }
void BindPromptResult(PromptResultFn callback) noexcept { rebirths::rebirth3_auto_skip::originalPromptResult = callback; }
void BindPromptReady(PromptReadyFn callback) noexcept { rebirths::rebirth3_auto_skip::promptReady = callback; }
void BindClosePrompt(PromptCloseFn callback) noexcept { rebirths::rebirth3_auto_skip::closePrompt = callback; }
void BindPlayUiSound(UiSoundFn callback) noexcept { rebirths::rebirth3_auto_skip::playUiSound = callback; }
uint32_t __fastcall EventRequest(uintptr_t adv, void* unused1, uint32_t script) noexcept { return rebirths::rebirth3_auto_skip::EventRequest(adv, unused1, script); }
int __cdecl StoryInput(uintptr_t adv) noexcept { return rebirths::rebirth3_auto_skip::StoryInput(adv); }
int __fastcall InEnginePromptResult(uintptr_t prompt, void* unused1, uint32_t sound) noexcept { return rebirths::rebirth3_auto_skip::InEnginePromptResult(prompt, unused1, sound); }
}
#endif
