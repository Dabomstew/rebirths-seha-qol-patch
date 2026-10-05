#ifdef REBIRTHS_TEST_CONTRACTS
#include "adv_test_contracts.hpp"
#endif
#include "sega_adv_fast_forward.hpp"
#include "adv_blackout.hpp"

#include "owned_patch_install.hpp"
#include "adv_operations.hpp"

#include <array>
#include <cstring>

namespace rebirths {
namespace sega_fast_forward {
namespace {
using owned_patch::CallTo;

constexpr uint32_t ActiveAdvPointerRva = 0x437b44;
constexpr uint32_t AdvFlagsOffset = 0x48;
constexpr uint32_t AdvModeOffset = 0x10;
constexpr uint32_t AdvRequestOffset = 0x14;
constexpr uint32_t ScaledTrackRva = 0xb0b60;
constexpr uint32_t BackgroundLoadRva = 0x968f0;
constexpr uint32_t AdvSePollRva = 0x8fc60;
constexpr size_t FastForwardSiteCount = 23;

uintptr_t gameBase = 0;
volatile LONG enabled = 0;
using ScaledTrackFn = void (__cdecl*)(float*, float);
using BackgroundLoadFn = uint8_t (__cdecl*)(uint32_t*);
using AdvSePollFn = uint8_t (__thiscall*)(uintptr_t);
using FrameTrackFn = void (__cdecl*)(float*);
FrameTrackFn originalFrameTrack = nullptr;
using AdvBinCreateFn = void (__cdecl*)(uintptr_t, uint32_t, uint32_t, uint32_t);
AdvBinCreateFn originalAdvBinCreate = nullptr;
using SetupFn = void (__cdecl*)(uint32_t, uint32_t, int, int, uint32_t);
using ResolveFn = uintptr_t (__stdcall*)(uintptr_t);
using LookupFn = uint8_t (__thiscall*)(uintptr_t, uint32_t, uintptr_t*, uintptr_t*);
SetupFn originalSetup = nullptr;
ResolveFn resolveTask = nullptr;
LookupFn lookupCharacter = nullptr;
ScaledTrackFn originalScaledTrack = nullptr;
BackgroundLoadFn originalBackgroundLoad = nullptr;
AdvSePollFn originalAdvSePoll = nullptr;
using DelayFn = int (__cdecl*)(int*, float*, float);
using AggregateFn = int (__cdecl*)(uint32_t, int, uint32_t*);
DelayFn originalDelay = nullptr;
AggregateFn originalAggregate = nullptr;
using TalkCleanupFn = int (__cdecl*)(int*, int, int, int, uint32_t, uint32_t);
TalkCleanupFn originalTalkCleanup = nullptr;
using CharacterCommandFn = int (__cdecl*)(uint32_t, int, int, float);
using ManagerFn = uintptr_t (__cdecl*)();
CharacterCommandFn originalCharacterCommand = nullptr;
ManagerFn originalManager = nullptr;
using CgReadyFn = int (__cdecl*)(uintptr_t, float);
using CharacterExitFn = int (__cdecl*)(int*, uint32_t, int, float);
CgReadyFn originalCgReady = nullptr;
CharacterExitFn originalCharacterExit = nullptr;
using CharacterAnimationFn = int (__cdecl*)(int*, uint32_t, int);
CharacterAnimationFn originalCharacterAnimation = nullptr;
using StoryMovieFn = int (__thiscall*)(uintptr_t);
StoryMovieFn originalStoryMovie = nullptr;


uintptr_t ActiveAdv() noexcept {
    const uintptr_t adv = *reinterpret_cast<const uintptr_t*>(gameBase + ActiveAdvPointerRva);
    return adv && *reinterpret_cast<const uintptr_t*>(adv + AdvRequestOffset) &&
        *reinterpret_cast<const uint8_t*>(adv + 0x850c) == 6 &&
        (*reinterpret_cast<const uint32_t*>(adv + AdvFlagsOffset) & 8) ? adv : 0;
}

bool CompleteTrack(float* record) noexcept {
    const uintptr_t adv = ActiveAdv();
    if (!enabled || !adv || *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) != 0 || !record) return false;
    // Exact terminal stores from 0x004b0b60. An inactive record is untouched.
    adv_operations::FinishTrack(record);
    return true;
}

void __cdecl BackgroundTrack(float* record, float scale) noexcept {
    if (!CompleteTrack(record)) originalScaledTrack(record, scale);
}

void __cdecl FadeTrack(float* record, float scale) noexcept {
    if (!CompleteTrack(record)) originalScaledTrack(record, scale);
}

void __cdecl DelayTrack(float* record, float scale) noexcept {
    const uintptr_t adv = ActiveAdv();
    if (!enabled || !adv || *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) != 0 ||
        record != reinterpret_cast<float*>(adv + 0x18)) {
        originalScaledTrack(record, scale);
        return;
    }
    CompleteTrack(record);
}

uint8_t __cdecl BackgroundLoad(uint32_t* record) noexcept {
    // Match the independently validated Sega equivalent of the accepted
    // Re;Birth1 guard. This resource path intentionally is not mode-zero-only.
    if (enabled && ActiveAdv() && record && !record[3] &&
        reinterpret_cast<const unsigned char*>(record)[0x118] == 2 && record[2]) {
        record[2] = 0;
        return 1;
    }
    return originalBackgroundLoad(record);
}

uint8_t __fastcall AdvSePoll(uintptr_t context, void*) noexcept {
    const uint8_t result = originalAdvSePoll(context);
    const uintptr_t adv = ActiveAdv();
    if (enabled && result && adv && *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0) return 0;
    return result;
}

void __cdecl CharacterSetup(uint32_t mode, uint32_t character, int arg3, int arg4, uint32_t arg5) noexcept {
    const uintptr_t adv = ActiveAdv();
    if (enabled && adv && *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0 &&
        !( *reinterpret_cast<const uint32_t*>(adv + AdvFlagsOffset) & 0x400) && (mode == 0 || mode == 2)) {
        const uintptr_t manager = *reinterpret_cast<const uintptr_t*>(gameBase + 0x437b88);
        const uintptr_t handle = manager ? *reinterpret_cast<const uintptr_t*>(manager + 0xc) : 0;
        const uintptr_t list = handle ? resolveTask(handle) : 0;
        uintptr_t record = 0, child = 0;
        if (list) lookupCharacter(list, character, &record, &child);
        // Match the original's child-output decision, not lookup AL. Keep
        // existing actors and unsupported setup modes on their native path.
        if (list && !child) return;
    }
    originalSetup(mode, character, arg3, arg4, arg5);
}

void __cdecl AdvBinCreate(uintptr_t manager, uint32_t operation, uint32_t entry, uint32_t modeFlag) noexcept {
    const uintptr_t adv = ActiveAdv();
    // Elide only optional inner AdvBinProc creation. Existing children still
    // use the caller's original child-list wait and native destruction.
    if (enabled && adv && *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0 && modeFlag == 0) return;
    originalAdvBinCreate(manager, operation, entry, modeFlag);
}

void __cdecl FrameTrack(float* record) noexcept {
    if (!CompleteTrack(record)) originalFrameTrack(record);
}

bool BlackoutActive() noexcept {
    const uintptr_t adv = ActiveAdv();
    return enabled && adv && *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0;
}
uintptr_t InfoHandle() noexcept {
    const uintptr_t manager = *reinterpret_cast<const uintptr_t*>(gameBase + 0x437b88);
    // Inline AdvWndInfo record begins at manager+0x6c, not a pointer at +0x6c.
    if (manager && *reinterpret_cast<const uintptr_t*>(manager) &&
        (*reinterpret_cast<const uint32_t*>(manager + 0x27c) & 1)) {
        const auto widget = *reinterpret_cast<const uintptr_t*>(manager + 0x70);
        if (widget) return widget;
    }
    // Script-owned ADV_INFO records are separate from the inline manager
    // record. Follow the same visible-widget list used by 0x0063a080.
    const auto windows = *reinterpret_cast<const uintptr_t*>(gameBase + 0x5c16f4);
    if (!windows) return 0;
    auto widget = *reinterpret_cast<const uintptr_t*>(windows + 0x5aab8);
    for (size_t count = 0; widget && count < 4096; ++count) {
        const auto name = reinterpret_cast<const char*>(widget + 0x78);
        if (std::memcmp(name, "AdvWndInfo", sizeof("AdvWndInfo")) == 0 ||
            std::memcmp(name, "AdvWndInfoMoney", sizeof("AdvWndInfoMoney")) == 0) return widget;
        widget = *reinterpret_cast<const uintptr_t*>(widget + 0x10);
    }
    return 0;
}

bool SkipPresentationWaits() noexcept {
    const uintptr_t adv = ActiveAdv();
    return enabled && adv && *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0;
}

int __cdecl Delay(int* state, float* record, float duration) noexcept {
    const uintptr_t adv = ActiveAdv();
    const bool complete = SkipPresentationWaits() && record == reinterpret_cast<float*>(adv + 0x18);
    // Preserve the native producer, including its initial track setup. Complete
    // its scalar track and let the native state-one branch advance in this call.
    return adv_operations::PollDelay(complete, state,
        [&]() noexcept { return originalDelay(state, record, duration); },
        [&]() noexcept { CompleteTrack(record); });
}

int __cdecl Aggregate(uint32_t mask, int wait, uint32_t* output) noexcept {
    const bool skip = SkipPresentationWaits();
    // Preserve the output mask and all native polling; advance only this VM wait.
    return adv_operations::PollAndAdvance(skip, originalAggregate, mask, wait, output);
}

int __cdecl TalkCleanup(int* state, int operation, int arg3, int arg4, uint32_t arg5, uint32_t arg6) noexcept {
    const bool advance = SkipPresentationWaits() && operation < 0 && *state == 0;
    // Preserve both native cleanup stages. Positive creation/state-ten polling
    // and error returns never enter the bounded second call.
    return adv_operations::PollCleanup(advance, state, [&]() noexcept {
        return originalTalkCleanup(state, operation, arg3, arg4, arg5, arg6);
    });
}

int __cdecl CharacterCommand(uint32_t id, int arg, int expression, float duration) noexcept {
    const bool skip = SkipPresentationWaits();
    return adv_operations::PollAndAdvance(skip, originalCharacterCommand, id, arg, expression, duration);
}

uintptr_t __cdecl CgCommandManager() noexcept {
    const uintptr_t manager = originalManager();
    // Opcode 3243's native no-manager branch completes without using its CG child.
    // This never changes the shared getter, child state or lifetime ownership.
    return SkipPresentationWaits() ? 0 : manager;
}

int __cdecl CgReady(uintptr_t object, float duration) noexcept {
    const bool skip = SkipPresentationWaits();
    return adv_operations::PollAndAdvance(skip, originalCgReady, object, duration);
}

int __cdecl CharacterExit(int* state, uint32_t id, int arg, float duration) noexcept {
    const bool skip = SkipPresentationWaits();
    // Match the aggressive RB1 command contract: pending presentation may be
    // omitted, but the original first-stage effects and native owners remain.
    return adv_operations::PollAndAdvance(skip, originalCharacterExit, state, id, arg, duration);
}
int __cdecl CharacterAnimation(int* state, uint32_t id, int animation) noexcept {
    const bool skip = SkipPresentationWaits();
    // Command400 can wait on an actor queued before skip. Keep native effects
    // for ready actors; omit a pending visual operation, never fake actor readiness.
    return adv_operations::PollAndAdvance(skip, originalCharacterAnimation, state, id, animation);
}
int __fastcall StoryMovie(uintptr_t owner, void*) noexcept {
    const auto adv = *reinterpret_cast<const uintptr_t*>(gameBase + ActiveAdvPointerRva);
    // The movie command deliberately inhibits ADV skip. Its own native stop
    // transition is the authorization here; playing/unskipped movies are unchanged.
    const bool eligible = enabled && owner && adv &&
        *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0 &&
        *reinterpret_cast<const uint8_t*>(adv + 0x850c) == 6 &&
        *reinterpret_cast<const uint8_t*>(owner + 0xc) == 1;
    const auto request = eligible ? *reinterpret_cast<const uintptr_t*>(adv + AdvRequestOffset) : 0;
    const auto movie = request ? *reinterpret_cast<const uintptr_t*>(owner) : 0;
    const bool stopping = movie && *reinterpret_cast<const uint32_t*>(movie + 0x64) != 0;
    const int result = originalStoryMovie(owner);
    if (movie && !stopping && result == 0 &&
        *reinterpret_cast<const uintptr_t*>(gameBase + ActiveAdvPointerRva) == adv &&
        *reinterpret_cast<const uintptr_t*>(adv + AdvRequestOffset) == request &&
        *reinterpret_cast<const uintptr_t*>(owner) == movie &&
        *reinterpret_cast<const uint32_t*>(movie + 0x64) == 1) {
        // Only the remaining fade time changes. 0x494bd0 subtracts one native
        // frame then executes its existing decoder/resource release branch.
        // Keep the denominator at +0x68, handles, stop flag and result untouched.
        *reinterpret_cast<float*>(movie + 0x6c) = 0.0f;
    }
    return result;
}
constexpr uint32_t PresentationRva = 0x2f5660, SwapIatRva = 0x34602c;
using PreparedSite = owned_patch::PreparedCall;
using owned_patch::IsOriginalExecutable;
using owned_patch::BytesAt;
} // namespace

bool Install(const Context& context, const AdvFastForwardPatchOps* injectedOps) noexcept {
    const AdvFastForwardPatchOps productionOps{RetargetCalls, RetargetBytes};
    const auto& ops = injectedOps ? *injectedOps : productionOps;
    if (context.spec.id != GameId::SegaHardGirls) { Log("AdvFastForward sega-hard-girls wrong target"); return false; }
    if (!ops.retargetCalls || !ops.retargetBytes) { Log("AdvFastForward sega-hard-girls unavailable patch operations"); return false; }
    gameBase = reinterpret_cast<uintptr_t>(context.game);
    originalScaledTrack = reinterpret_cast<ScaledTrackFn>(gameBase + ScaledTrackRva);
    originalBackgroundLoad = reinterpret_cast<BackgroundLoadFn>(gameBase + BackgroundLoadRva);
    originalAdvSePoll = reinterpret_cast<AdvSePollFn>(gameBase + AdvSePollRva);
    originalFrameTrack = reinterpret_cast<FrameTrackFn>(gameBase + 0xb0bf0);
    originalDelay = reinterpret_cast<DelayFn>(gameBase + 0x87340);
    originalAggregate = reinterpret_cast<AggregateFn>(gameBase + 0x873c0);
    originalTalkCleanup = reinterpret_cast<TalkCleanupFn>(gameBase + 0x8a4a0);
    originalCharacterCommand = reinterpret_cast<CharacterCommandFn>(gameBase + 0x947b0);
    originalManager = reinterpret_cast<ManagerFn>(gameBase + 0xa5720);
    originalCgReady = reinterpret_cast<CgReadyFn>(gameBase + 0x94930);
    originalCharacterExit = reinterpret_cast<CharacterExitFn>(gameBase + 0x94ab0);
    originalCharacterAnimation = reinterpret_cast<CharacterAnimationFn>(gameBase + 0x94f30);
    originalStoryMovie = reinterpret_cast<StoryMovieFn>(gameBase + 0x8e950);
    originalAdvBinCreate = reinterpret_cast<AdvBinCreateFn>(gameBase + 0x97ac0);
    originalSetup = reinterpret_cast<SetupFn>(gameBase + 0x885c0);
    resolveTask = reinterpret_cast<ResolveFn>(gameBase + 0x79ef0);
    lookupCharacter = reinterpret_cast<LookupFn>(gameBase + 0x85020);
    InterlockedExchange(&enabled, 0);
    const auto swap = *reinterpret_cast<BOOL (WINAPI**)(HDC)>(gameBase + SwapIatRva);
    if (!swap) return false;
    ConfigureAdvBlackout(gameBase, swap, BlackoutActive,
        reinterpret_cast<AdvWindowSkinFn>(gameBase + 0x23c060), InfoHandle);
    const auto presentationExpected = AdvBlackoutExpectedCall(gameBase, SwapIatRva);
    const auto presentationReplacement = AdvBlackoutReplacement(gameBase + PresentationRva);
    std::array<PreparedSite, FastForwardSiteCount> sites{{
        {{0x82df0, CallTo(0x82df0, 0xb0b60), reinterpret_cast<void*>(&DelayTrack)}},
        {{0x96c0a, CallTo(0x96c0a, 0x968f0), reinterpret_cast<void*>(&BackgroundLoad)}},
        {{0x87f29, CallTo(0x87f29, 0x8fc60), reinterpret_cast<void*>(&AdvSePoll)}},
        {{0x96b4a, CallTo(0x96b4a, 0xb0b60), reinterpret_cast<void*>(&BackgroundTrack)}},
        {{0xb0271, CallTo(0xb0271, 0xb0b60), reinterpret_cast<void*>(&FadeTrack)}},
        {{0x9fef7, CallTo(0x9fef7, 0xb0bf0), reinterpret_cast<void*>(&FrameTrack)}},
        {{0x9ff03, CallTo(0x9ff03, 0xb0bf0), reinterpret_cast<void*>(&FrameTrack)}},
        {{0x9ff0f, CallTo(0x9ff0f, 0xb0bf0), reinterpret_cast<void*>(&FrameTrack)}},
        {{0x9598e, CallTo(0x9598e, 0x97ac0), reinterpret_cast<void*>(&AdvBinCreate)}},
        {{0x89e53, CallTo(0x89e53, 0x885c0), reinterpret_cast<void*>(&CharacterSetup)}},
        {{0x23a173, CallTo(0x23a173, 0x23c060), reinterpret_cast<void*>(&AdvBlackoutWindowSkin)}},
        {{0x23a351, CallTo(0x23a351, 0x23c060), reinterpret_cast<void*>(&AdvBlackoutWindowSkin)}},
        {{0x23d23d, CallTo(0x23d23d, 0x23c060), reinterpret_cast<void*>(&AdvBlackoutWindowSkin)}},
        {{0x23a72d, CallTo(0x23a72d, 0x23c060), reinterpret_cast<void*>(&AdvBlackoutWindowSkin)}},
        {{0x877c7, CallTo(0x877c7, 0x87340), reinterpret_cast<void*>(&Delay)}},
        {{0x877e6, CallTo(0x877e6, 0x873c0), reinterpret_cast<void*>(&Aggregate)}},
        {{0x8a74e, CallTo(0x8a74e, 0x8a4a0), reinterpret_cast<void*>(&TalkCleanup)}},
        {{0x89e8b, CallTo(0x89e8b, 0x947b0), reinterpret_cast<void*>(&CharacterCommand)}},
        {{0x8c8df, CallTo(0x8c8df, 0xa5720), reinterpret_cast<void*>(&CgCommandManager)}},
        {{0x8beb8, CallTo(0x8beb8, 0x94930), reinterpret_cast<void*>(&CgReady)}},
        {{0x89e39, CallTo(0x89e39, 0x94ab0), reinterpret_cast<void*>(&CharacterExit)}},
        {{0x8a021, CallTo(0x8a021, 0x94f30), reinterpret_cast<void*>(&CharacterAnimation)}},
        {{0x88392, CallTo(0x88392, 0x8e950), reinterpret_cast<void*>(&StoryMovie)}},
    }};
    const uintptr_t currentAdv = *reinterpret_cast<const uintptr_t*>(gameBase + ActiveAdvPointerRva);
    if (currentAdv && *reinterpret_cast<const uintptr_t*>(currentAdv + AdvRequestOffset)) {
        Log("AdvFastForward sega-hard-girls late install refused"); return false;
    }
    const owned_patch::BytePatch presentation{PresentationRva, presentationExpected.data(),
        presentationReplacement.data(), presentationExpected.size(), "blackout "};
    const PatchResult result = owned_patch::Install(context, sites, ops, "AdvFastForward sega-hard-girls", &presentation, 1);
    if (!result.Succeeded()) return false;
    InterlockedExchange(&enabled, 1);
    Log("AdvFastForward installed twenty-three guarded calls and blackout present hook game=sega-hard-girls");
    return true;
}

} // namespace sega_fast_forward

bool InstallSegaHardGirlsAdvFastForward(const Context& context, const AdvFastForwardPatchOps* ops) noexcept {
    return sega_fast_forward::Install(context, ops);
}

} // namespace rebirths

#ifdef REBIRTHS_TEST_CONTRACTS
namespace rebirths::testing::sega_fast_forward {
DelayTrackHook HookDelayTrack() noexcept { return &rebirths::sega_fast_forward::DelayTrack; }
void BindFrameTrack(FrameTrackFn callback) noexcept { rebirths::sega_fast_forward::originalFrameTrack = callback; }
void BindAdvBinCreate(AdvBinCreateFn callback) noexcept { rebirths::sega_fast_forward::originalAdvBinCreate = callback; }
void BindSetup(SetupFn callback) noexcept { rebirths::sega_fast_forward::originalSetup = callback; }
void BindResolveTask(ResolveFn callback) noexcept { rebirths::sega_fast_forward::resolveTask = callback; }
void BindLookupCharacter(LookupFn callback) noexcept { rebirths::sega_fast_forward::lookupCharacter = callback; }
void BindScaledTrack(ScaledTrackFn callback) noexcept { rebirths::sega_fast_forward::originalScaledTrack = callback; }
ScaledTrackFn NativeScaledTrack() noexcept { return rebirths::sega_fast_forward::originalScaledTrack; }
void BindBackgroundLoad(BackgroundLoadFn callback) noexcept { rebirths::sega_fast_forward::originalBackgroundLoad = callback; }
void BindAdvSePoll(AdvSePollFn callback) noexcept { rebirths::sega_fast_forward::originalAdvSePoll = callback; }
void BindDelay(DelayFn callback) noexcept { rebirths::sega_fast_forward::originalDelay = callback; }
void BindAggregate(AggregateFn callback) noexcept { rebirths::sega_fast_forward::originalAggregate = callback; }
void BindTalkCleanup(TalkCleanupFn callback) noexcept { rebirths::sega_fast_forward::originalTalkCleanup = callback; }
void BindCharacterCommand(CharacterCommandFn callback) noexcept { rebirths::sega_fast_forward::originalCharacterCommand = callback; }
void BindManager(ManagerFn callback) noexcept { rebirths::sega_fast_forward::originalManager = callback; }
void BindCgReady(CgReadyFn callback) noexcept { rebirths::sega_fast_forward::originalCgReady = callback; }
void BindCharacterExit(CharacterExitFn callback) noexcept { rebirths::sega_fast_forward::originalCharacterExit = callback; }
void BindCharacterAnimation(CharacterAnimationFn callback) noexcept { rebirths::sega_fast_forward::originalCharacterAnimation = callback; }
void BindStoryMovie(StoryMovieFn callback) noexcept { rebirths::sega_fast_forward::originalStoryMovie = callback; }
void SetActive(bool active) noexcept { InterlockedExchange(&rebirths::sega_fast_forward::enabled, active ? 1 : 0); }
LONG Active() noexcept { return InterlockedCompareExchange(&rebirths::sega_fast_forward::enabled, 0, 0); }
uintptr_t ActiveAdv() noexcept { return rebirths::sega_fast_forward::ActiveAdv(); }
void __cdecl BackgroundTrack(float* record, float scale) noexcept { return rebirths::sega_fast_forward::BackgroundTrack(record, scale); }
void __cdecl FadeTrack(float* record, float scale) noexcept { return rebirths::sega_fast_forward::FadeTrack(record, scale); }
void __cdecl DelayTrack(float* record, float scale) noexcept { return rebirths::sega_fast_forward::DelayTrack(record, scale); }
uint8_t __cdecl BackgroundLoad(uint32_t* record) noexcept { return rebirths::sega_fast_forward::BackgroundLoad(record); }
uint8_t __fastcall AdvSePoll(uintptr_t context, void* unused1) noexcept { return rebirths::sega_fast_forward::AdvSePoll(context, unused1); }
void __cdecl CharacterSetup(uint32_t mode, uint32_t character, int arg3, int arg4, uint32_t arg5) noexcept { return rebirths::sega_fast_forward::CharacterSetup(mode, character, arg3, arg4, arg5); }
void __cdecl AdvBinCreate(uintptr_t manager, uint32_t operation, uint32_t entry, uint32_t modeFlag) noexcept { return rebirths::sega_fast_forward::AdvBinCreate(manager, operation, entry, modeFlag); }
void __cdecl FrameTrack(float* record) noexcept { return rebirths::sega_fast_forward::FrameTrack(record); }
uintptr_t InfoHandle() noexcept { return rebirths::sega_fast_forward::InfoHandle(); }
int __cdecl Delay(int* state, float* record, float duration) noexcept { return rebirths::sega_fast_forward::Delay(state, record, duration); }
int __cdecl Aggregate(uint32_t mask, int wait, uint32_t* output) noexcept { return rebirths::sega_fast_forward::Aggregate(mask, wait, output); }
int __cdecl TalkCleanup(int* state, int operation, int arg3, int arg4, uint32_t arg5, uint32_t arg6) noexcept { return rebirths::sega_fast_forward::TalkCleanup(state, operation, arg3, arg4, arg5, arg6); }
int __cdecl CharacterCommand(uint32_t id, int arg, int expression, float duration) noexcept { return rebirths::sega_fast_forward::CharacterCommand(id, arg, expression, duration); }
uintptr_t __cdecl CgCommandManager() noexcept { return rebirths::sega_fast_forward::CgCommandManager(); }
int __cdecl CgReady(uintptr_t object, float duration) noexcept { return rebirths::sega_fast_forward::CgReady(object, duration); }
int __cdecl CharacterExit(int* state, uint32_t id, int arg, float duration) noexcept { return rebirths::sega_fast_forward::CharacterExit(state, id, arg, duration); }
int __cdecl CharacterAnimation(int* state, uint32_t id, int animation) noexcept { return rebirths::sega_fast_forward::CharacterAnimation(state, id, animation); }
int __fastcall StoryMovie(uintptr_t owner, void* unused1) noexcept { return rebirths::sega_fast_forward::StoryMovie(owner, unused1); }
}
#endif
