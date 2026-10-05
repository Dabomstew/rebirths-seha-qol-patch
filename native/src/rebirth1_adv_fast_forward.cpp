#ifdef REBIRTHS_TEST_CONTRACTS
#include "adv_test_contracts.hpp"
#endif
#include "sega_adv_fast_forward.hpp"
#include "rebirth1_adv_fast_forward.hpp"
#include "adv_blackout.hpp"
#include "rebirth3_adv_fast_forward.hpp"
#include "rebirth2_adv_fast_forward.hpp"

#include "owned_patch_install.hpp"
#include "adv_operations.hpp"

#include <array>
#include <cstring>

namespace rebirths {
namespace {
using owned_patch::CallTo;

constexpr uint32_t ManagerRva = 0x2e020;
constexpr uint32_t AccessorRva = 0x2400;
constexpr uint32_t ActiveAdvPointerRva = 0x4591c4;
constexpr uint32_t AdvWindowSkinRva = 0x1e76d0;
constexpr size_t FastForwardSiteCount = 19;
uintptr_t gameBase = 0;
volatile LONG enabled = 0;

using DelayFn = int (__cdecl*)(int*, float*, float);
using BackgroundLoadFn = int (__cdecl*)(uint32_t*);
using TrackFn = void (__cdecl*)(float*);
using ScaledTrackFn = void (__cdecl*)(float*, float);
using SetupFn = void (__cdecl*)(uint32_t, uint32_t, int, int, uint32_t);
using LookupFn = uint8_t (__thiscall*)(int*, uint32_t, int*, int*);
using ContextFn = uintptr_t (__cdecl*)();
using ManagerFn = int* (__cdecl*)();
using ResolveFn = int* (__stdcall*)(int);
using AdvBinCreateFn = void (__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t);
using CgMotionPollFn = uint32_t (__fastcall*)(uintptr_t);
using AdvSePollFn = uint8_t (__thiscall*)(uintptr_t);
DelayFn originalDelay = nullptr;
BackgroundLoadFn originalBackgroundLoad = nullptr;
TrackFn originalTrack = nullptr;
ScaledTrackFn originalScaledTrack = nullptr;
SetupFn originalSetup = nullptr;
LookupFn lookupCharacter = nullptr;
ContextFn battleContext = nullptr;
ManagerFn characterManager = nullptr;
ResolveFn resolveCharacterList = nullptr;
AdvBinCreateFn originalAdvBinCreate = nullptr;
AdvSePollFn originalAdvSePoll = nullptr;
CgMotionPollFn originalCgMotionPoll = nullptr;
using AggregateFn = int (__cdecl*)(uint32_t, int, uint32_t*);
using CgReadyFn = int (__cdecl*)(uintptr_t, float);
using TalkCleanupFn = int (__cdecl*)(int*, int, int, int, uint32_t, uint32_t);
using CharacterReadyFn = int (__cdecl*)(uint32_t, int, int, float);
using CharacterExitFn = int (__cdecl*)(int*, uint32_t, int, float);
AggregateFn originalAggregate = nullptr;
CgReadyFn originalCgReady = nullptr;
TalkCleanupFn originalTalkCleanup = nullptr;
CharacterReadyFn originalCharacterReady = nullptr;
CharacterExitFn originalCharacterExit = nullptr;

uintptr_t ActiveAdv() noexcept {
    const uintptr_t adv = *reinterpret_cast<uintptr_t*>(gameBase + ActiveAdvPointerRva);
    return adv && (*reinterpret_cast<uint32_t*>(adv + 0x48) & 8) ? adv : 0;
}
bool SkipPresentationWaits() noexcept {
    const uintptr_t adv = ActiveAdv();
    return enabled && adv && *reinterpret_cast<uint32_t*>(adv + 0x10) == 0;
}
int __cdecl Aggregate(uint32_t mask, int wait, uint32_t* output) noexcept {
    const bool skip = SkipPresentationWaits();
    // Keep the reported mask and all resource owners. Only this VM wait advances.
    return adv_operations::PollAndAdvance(skip, originalAggregate, mask, wait, output);
}
int __cdecl CgReady(uintptr_t object, float duration) noexcept {
    const bool skip = SkipPresentationWaits();
    return adv_operations::PollAndAdvance(skip, originalCgReady, object, duration);
}
int __cdecl CharacterReady(uint32_t id, int arg, int expression, float duration) noexcept {
    const bool skip = SkipPresentationWaits();
    return adv_operations::PollAndAdvance(skip, originalCharacterReady, id, arg, expression, duration);
}
int __cdecl CharacterExit(int* state, uint32_t id, int arg, float duration) noexcept {
    const bool skip = SkipPresentationWaits();
    // Pending presentation transitions may be omitted; ADV teardown retains ownership.
    return adv_operations::PollAndAdvance(skip, originalCharacterExit, state, id, arg, duration);
}
int __cdecl TalkCleanup(int* state, int operation, int arg3, int arg4, uint32_t arg5, uint32_t arg6) noexcept {
    const bool advance = SkipPresentationWaits() && operation < 0 && *state == 0;
    return adv_operations::PollCleanup(advance, state, [&]() noexcept {
        return originalTalkCleanup(state, operation, arg3, arg4, arg5, arg6);
    });
}
int* __cdecl CgCommandManager() noexcept {
    // Opcode3243's no-manager branch completes without using its pending CG child.
    // Scope is this callsite, not the shared getter or resource state machine.
    int* manager = characterManager();
    return SkipPresentationWaits() ? nullptr : manager;
}
int __cdecl Delay(int* state, float* record, float duration) noexcept {
    const uintptr_t adv = ActiveAdv();
    if (enabled && adv && *reinterpret_cast<uint32_t*>(adv + 0x10) == 0 && record == reinterpret_cast<float*>(adv + 0x18)) {
        if (*state == 0 && duration > 0) {
            record[0] = record[1] = 2.0f;
            record[2] = 0.0f;
            *state = 1;
            return originalDelay(state, record, duration);
        }
        if (*state == 1 && record[0] != record[1]) {
            record[0] = record[1];
            record[2] = 0.0f;
        }
    }
    return originalDelay(state, record, duration);
}
int __cdecl BackgroundLoad(uint32_t* record) noexcept {
    // This exact 8076 guard intentionally does not require mode zero.
    if (enabled && ActiveAdv() && !record[3] && reinterpret_cast<unsigned char*>(record)[0x114] == 2 && record[2]) {
        record[2] = 0;
        return 1;
    }
    return originalBackgroundLoad(record);
}
bool CompleteTrack(float* record) noexcept {
    const uintptr_t adv = ActiveAdv();
    if (!enabled || !adv || *reinterpret_cast<uint32_t*>(adv + 0x10) != 0 || !record) return false;
    // Exact terminal stores from the original helpers. Inactive records receive no writes.
    adv_operations::FinishTrack(record);
    return true;
}
uint32_t __fastcall CgMotionPoll(uintptr_t object) noexcept {
    // This list-owned per-object predicate receives ECX and returns full EAX.
    // Finish only the two pan axes. Keep zoom, alpha and resource predicates,
    // initialization and cleanup in the original command/task lifecycles.
    CompleteTrack(reinterpret_cast<float*>(object + 0x30));
    CompleteTrack(reinterpret_cast<float*>(object + 0x40));
    return originalCgMotionPoll(object);
}
void __cdecl BackgroundTrack(float* record, float scale) noexcept { if (!CompleteTrack(record)) originalScaledTrack(record, scale); }
void __cdecl FrameTrack(float* record) noexcept { if (!CompleteTrack(record)) originalTrack(record); }
void __cdecl FadeTrack(float* record, float scale) noexcept { if (!CompleteTrack(record)) originalScaledTrack(record, scale); }
void __cdecl CharacterSetup(uint32_t mode, uint32_t character, int arg3, int arg4, uint32_t arg5) noexcept {
    const uintptr_t adv = ActiveAdv();
    if (!enabled || !adv || *reinterpret_cast<uint32_t*>(adv + 0x10) != 0 || (mode != 0 && mode != 2)) {
        originalSetup(mode, character, arg3, arg4, arg5);
        return;
    }
    int record = 0, child = 0;
    int* manager = characterManager();
    int* list = manager && manager[3] ? resolveCharacterList(manager[3]) : nullptr;
    // The original creation branch tests child only, ignoring AL. A list is
    // still required because the original helper returns early without one.
    if (list) lookupCharacter(list, character, &record, &child);
    const uintptr_t battle = battleContext();
    const bool battleGuard = battle && (*reinterpret_cast<uint32_t*>(battle + 0x48) & 0x400);
    if (list && !child && !battleGuard) return;
    originalSetup(mode, character, arg3, arg4, arg5);
}
void __cdecl AdvBinCreate(uint32_t manager, uint32_t arg3, uint32_t arg4, uint32_t modeFlag) noexcept {
    const uintptr_t adv = ActiveAdv();
    if (enabled && adv && *reinterpret_cast<uint32_t*>(adv + 0x10) == 0 && modeFlag == 0) return;
    originalAdvBinCreate(manager, arg3, arg4, modeFlag);
}
uint8_t __fastcall AdvSePoll(uintptr_t context, void*) noexcept {
    // Preserve the verified ECX __thiscall target and AL return contract.
    const uint8_t result = originalAdvSePoll(context);
    const uintptr_t adv = ActiveAdv();
    if (enabled && result && adv && *reinterpret_cast<uint32_t*>(adv + 0x10) == 0) return 0;
    return result;
}
bool BlackoutActive() noexcept {
    const uintptr_t adv = ActiveAdv();
    return enabled && adv && *reinterpret_cast<uint32_t*>(adv + 0x10) == 0;
}
using PreparedSite = owned_patch::PreparedCall;
using owned_patch::IsOriginalExecutable;
using owned_patch::BytesAt;
}

bool InstallRebirth1AdvFastForward(const Context& context, const AdvFastForwardPatchOps* injectedOps) noexcept {
    const AdvFastForwardPatchOps productionOps{RetargetCalls, RetargetBytes};
    const AdvFastForwardPatchOps& ops = injectedOps ? *injectedOps : productionOps;
    if (!ops.retargetCalls || !ops.retargetBytes) { Log("AdvFastForward unavailable patch operations"); return false; }
    InterlockedExchange(&enabled, 0);
    gameBase = reinterpret_cast<uintptr_t>(context.game);
    const auto originalSwapBuffers = *reinterpret_cast<BOOL (WINAPI**)(HDC)>(gameBase + 0x32f04c);
    if (!originalSwapBuffers) { Log("AdvFastForward blackout IAT target unavailable"); return false; }
    ConfigureAdvBlackout(gameBase, originalSwapBuffers, BlackoutActive,
                         reinterpret_cast<AdvWindowSkinFn>(gameBase + AdvWindowSkinRva));
    originalDelay = reinterpret_cast<DelayFn>(gameBase + 0xf5c0);
    originalBackgroundLoad = reinterpret_cast<BackgroundLoadFn>(gameBase + 0x1e800);
    originalTrack = reinterpret_cast<TrackFn>(gameBase + 0x39900);
    originalScaledTrack = reinterpret_cast<ScaledTrackFn>(gameBase + 0x39860);
    originalSetup = reinterpret_cast<SetupFn>(gameBase + 0x106d0);
    lookupCharacter = reinterpret_cast<LookupFn>(gameBase + 0xd350);
    battleContext = reinterpret_cast<ContextFn>(gameBase + 0x8ab0);
    characterManager = reinterpret_cast<ManagerFn>(gameBase + ManagerRva);
    resolveCharacterList = reinterpret_cast<ResolveFn>(gameBase + AccessorRva);
    originalAdvBinCreate = reinterpret_cast<AdvBinCreateFn>(gameBase + 0x1f9e0);
    originalAdvSePoll = reinterpret_cast<AdvSePollFn>(gameBase + 0x17c50);
    originalCgMotionPoll = reinterpret_cast<CgMotionPollFn>(gameBase + 0x15930);
    originalAggregate = reinterpret_cast<AggregateFn>(gameBase + 0xf640);
    originalCgReady = reinterpret_cast<CgReadyFn>(gameBase + 0x1c8b0);
    originalTalkCleanup = reinterpret_cast<TalkCleanupFn>(gameBase + 0x12530);
    originalCharacterReady = reinterpret_cast<CharacterReadyFn>(gameBase + 0x1c730);
    originalCharacterExit = reinterpret_cast<CharacterExitFn>(gameBase + 0x1ca30);
    std::array<PreparedSite, FastForwardSiteCount> sites{{
        {{0xfa47, CallTo(0xfa47, 0xf5c0), reinterpret_cast<void*>(&Delay)}},
        {{0x1eb1e, CallTo(0x1eb1e, 0x1e800), reinterpret_cast<void*>(&BackgroundLoad)}},
        {{0x11f43, CallTo(0x11f43, 0x106d0), reinterpret_cast<void*>(&CharacterSetup)}},
        {{0x1d8ee, CallTo(0x1d8ee, 0x1f9e0), reinterpret_cast<void*>(&AdvBinCreate)}},
        {{0x10039, CallTo(0x10039, 0x17c50), reinterpret_cast<void*>(&AdvSePoll)}},
        {{0x1ea7a, CallTo(0x1ea7a, 0x39860), reinterpret_cast<void*>(&BackgroundTrack)}},
        {{0x28567, CallTo(0x28567, 0x39900), reinterpret_cast<void*>(&FrameTrack)}},
        {{0x28573, CallTo(0x28573, 0x39900), reinterpret_cast<void*>(&FrameTrack)}},
        {{0x2857f, CallTo(0x2857f, 0x39900), reinterpret_cast<void*>(&FrameTrack)}},
        {{0x38f61, CallTo(0x38f61, 0x39860), reinterpret_cast<void*>(&FadeTrack)}},
        {{0x1e59ae, CallTo(0x1e59ae, AdvWindowSkinRva), reinterpret_cast<void*>(&AdvBlackoutWindowSkin)}},
        {{0x1e5e5d, CallTo(0x1e5e5d, AdvWindowSkinRva), reinterpret_cast<void*>(&AdvBlackoutWindowSkin)}},
        {{0x15a3a, CallTo(0x15a3a, 0x15930), reinterpret_cast<void*>(&CgMotionPoll)}},
        {{0xfa66, CallTo(0xfa66, 0xf640), reinterpret_cast<void*>(&Aggregate)}},
        {{0x13f48, CallTo(0x13f48, 0x1c8b0), reinterpret_cast<void*>(&CgReady)}},
        {{0x127de, CallTo(0x127de, 0x12530), reinterpret_cast<void*>(&TalkCleanup)}},
        {{0x11f7b, CallTo(0x11f7b, 0x1c730), reinterpret_cast<void*>(&CharacterReady)}},
        {{0x11f29, CallTo(0x11f29, 0x1ca30), reinterpret_cast<void*>(&CharacterExit)}},
        {{0x1494f, CallTo(0x1494f, 0x2e020), reinterpret_cast<void*>(&CgCommandManager)}},
    }};
    // Match the experiment's early-install boundary. AdvPart may exist during
    // startup, but a loaded script resource means this proxy arrived too late.
    const uintptr_t currentAdv = *reinterpret_cast<uintptr_t*>(gameBase + ActiveAdvPointerRva);
    if (currentAdv && *reinterpret_cast<uint32_t*>(currentAdv + 0x14)) {
        Log("AdvFastForward late install refused");
        return false;
    }
    const auto presentationExpected = AdvBlackoutExpectedCall(gameBase);
    const auto presentationReplacement = AdvBlackoutReplacement(gameBase + 0x2970c8);
    const owned_patch::BytePatch presentation{0x2970c8, presentationExpected.data(),
        presentationReplacement.data(), presentationExpected.size(), "blackout "};
    const PatchResult result = owned_patch::Install(context, sites, ops, "AdvFastForward", &presentation, 1, owned_patch::RemovalOrder::Stable);
    if (!result.Succeeded()) return false;
    InterlockedExchange(&enabled, 1);
    Log("AdvFastForward installed nineteen guarded calls and blackout present hook");
    return true;
}

bool InstallAdvFastForward(const Context& context, const AdvFastForwardPatchOps* injectedOps) noexcept {
    switch (context.spec.id) {
    case GameId::Rebirth1:
        return InstallRebirth1AdvFastForward(context, injectedOps);
    case GameId::Rebirth3:
        return InstallRebirth3AdvFastForward(context, injectedOps);
    case GameId::Rebirth2:
        return InstallRebirth2AdvFastForward(context, injectedOps);
    case GameId::SegaHardGirls:
        return InstallSegaHardGirlsAdvFastForward(context, injectedOps);
    }
    Log("AdvFastForward unknown target");
    return false;
}

}

#ifdef REBIRTHS_TEST_CONTRACTS
namespace rebirths::testing::rebirth1_fast_forward {
void BindDelay(DelayFn callback) noexcept { rebirths::originalDelay = callback; }
void BindBackgroundLoad(BackgroundLoadFn callback) noexcept { rebirths::originalBackgroundLoad = callback; }
void BindTrack(TrackFn callback) noexcept { rebirths::originalTrack = callback; }
void BindScaledTrack(ScaledTrackFn callback) noexcept { rebirths::originalScaledTrack = callback; }
void BindSetup(SetupFn callback) noexcept { rebirths::originalSetup = callback; }
void BindLookupCharacter(LookupFn callback) noexcept { rebirths::lookupCharacter = callback; }
void BindBattleContext(ContextFn callback) noexcept { rebirths::battleContext = callback; }
void BindCharacterManager(ManagerFn callback) noexcept { rebirths::characterManager = callback; }
void BindResolveCharacterList(ResolveFn callback) noexcept { rebirths::resolveCharacterList = callback; }
void BindAdvBinCreate(AdvBinCreateFn callback) noexcept { rebirths::originalAdvBinCreate = callback; }
void BindAdvSePoll(AdvSePollFn callback) noexcept { rebirths::originalAdvSePoll = callback; }
void BindCgMotionPoll(CgMotionPollFn callback) noexcept { rebirths::originalCgMotionPoll = callback; }
void BindAggregate(AggregateFn callback) noexcept { rebirths::originalAggregate = callback; }
void BindCgReady(CgReadyFn callback) noexcept { rebirths::originalCgReady = callback; }
void BindTalkCleanup(TalkCleanupFn callback) noexcept { rebirths::originalTalkCleanup = callback; }
void BindCharacterReady(CharacterReadyFn callback) noexcept { rebirths::originalCharacterReady = callback; }
void BindCharacterExit(CharacterExitFn callback) noexcept { rebirths::originalCharacterExit = callback; }
void SetActive(bool active) noexcept { InterlockedExchange(&rebirths::enabled, active ? 1 : 0); }
LONG Active() noexcept { return InterlockedCompareExchange(&rebirths::enabled, 0, 0); }
int __cdecl Aggregate(uint32_t mask, int wait, uint32_t* output) noexcept { return rebirths::Aggregate(mask, wait, output); }
int __cdecl CgReady(uintptr_t object, float duration) noexcept { return rebirths::CgReady(object, duration); }
int __cdecl CharacterReady(uint32_t id, int arg, int expression, float duration) noexcept { return rebirths::CharacterReady(id, arg, expression, duration); }
int __cdecl CharacterExit(int* state, uint32_t id, int arg, float duration) noexcept { return rebirths::CharacterExit(state, id, arg, duration); }
int __cdecl TalkCleanup(int* state, int operation, int arg3, int arg4, uint32_t arg5, uint32_t arg6) noexcept { return rebirths::TalkCleanup(state, operation, arg3, arg4, arg5, arg6); }
int* __cdecl CgCommandManager() noexcept { return rebirths::CgCommandManager(); }
int __cdecl Delay(int* state, float* record, float duration) noexcept { return rebirths::Delay(state, record, duration); }
int __cdecl BackgroundLoad(uint32_t* record) noexcept { return rebirths::BackgroundLoad(record); }
uint32_t __fastcall CgMotionPoll(uintptr_t object) noexcept { return rebirths::CgMotionPoll(object); }
void __cdecl FrameTrack(float* record) noexcept { return rebirths::FrameTrack(record); }
void __cdecl CharacterSetup(uint32_t mode, uint32_t character, int arg3, int arg4, uint32_t arg5) noexcept { return rebirths::CharacterSetup(mode, character, arg3, arg4, arg5); }
void __cdecl AdvBinCreate(uint32_t manager, uint32_t arg3, uint32_t arg4, uint32_t modeFlag) noexcept { return rebirths::AdvBinCreate(manager, arg3, arg4, modeFlag); }
uint8_t __fastcall AdvSePoll(uintptr_t context, void* unused1) noexcept { return rebirths::AdvSePoll(context, unused1); }
}
#endif
