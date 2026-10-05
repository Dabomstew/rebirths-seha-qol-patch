#ifdef REBIRTHS_TEST_CONTRACTS
#include "adv_test_contracts.hpp"
#endif
#include "rebirth3_adv_fast_forward.hpp"
#include "adv_blackout.hpp"

#include "owned_patch_install.hpp"
#include "adv_operations.hpp"

#include <array>
#include <cstring>

namespace rebirths {
namespace rebirth3_fast_forward {
namespace {
using owned_patch::CallTo;

constexpr uint32_t ActiveAdvPointerRva = 0x0049a484;
constexpr uint32_t AdvFlagsOffset = 0x48;
constexpr uint32_t AdvModeOffset = 0x10;
constexpr uint32_t AdvRequestOffset = 0x14;
constexpr uint32_t ScaledTrackRva = 0x000a7db0;
constexpr uint32_t BackgroundLoadRva = 0x0008d6c0;
constexpr uint32_t AdvSePollRva = 0x00086b00;
constexpr uint32_t TalkReadyRva = 0x000a2cd0;
constexpr size_t FastForwardSiteCount = 22;

uintptr_t gameBase = 0;
volatile LONG enabled = 0;
using ScaledTrackFn = void (__cdecl*)(float*, float);
using BackgroundLoadFn = int (__cdecl*)(uint32_t*);
using AdvSePollFn = uint8_t (__thiscall*)(uintptr_t);
using TrackGroupFn = void (__thiscall*)(uintptr_t, float);
TrackGroupFn originalTrackGroup = nullptr;
using AdvBinCreateFn = void (__cdecl*)(uintptr_t, uint32_t, uint32_t, uint32_t);
AdvBinCreateFn originalAdvBinCreate = nullptr;
using SetupFn = void (__cdecl*)(uint32_t, uint32_t, int, int, uint32_t);
using ResolveFn = uintptr_t (__stdcall*)(uintptr_t);
using LookupFn = uint8_t (__thiscall*)(uintptr_t, uint32_t, uintptr_t*, uintptr_t*);
SetupFn originalSetup = nullptr;
ResolveFn resolveTask = nullptr;
LookupFn lookupCharacter = nullptr;
using TalkReadyFn = uint8_t (__cdecl*)(uintptr_t);
using CharacterReadyFn = uint32_t (__cdecl*)(uintptr_t);
ScaledTrackFn originalScaledTrack = nullptr;
BackgroundLoadFn originalBackgroundLoad = nullptr;
AdvSePollFn originalAdvSePoll = nullptr;
TalkReadyFn originalTalkReady = nullptr;
CharacterReadyFn originalCharacterReady = nullptr;
CharacterReadyFn originalCharacterUpdate = nullptr;
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


uintptr_t ActiveAdv() noexcept {
    const uintptr_t adv = *reinterpret_cast<const uintptr_t*>(gameBase + ActiveAdvPointerRva);
    return adv && *reinterpret_cast<const uintptr_t*>(adv + AdvRequestOffset) &&
        *reinterpret_cast<const uint8_t*>(adv + 0x850c) == 6 &&
        (*reinterpret_cast<const uint32_t*>(adv + AdvFlagsOffset) & 8) ? adv : 0;
}

bool CompleteTrack(float* record) noexcept {
    const uintptr_t adv = ActiveAdv();
    if (!enabled || !adv || *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) != 0 || !record) return false;
    // Exact terminal stores from 0x004a7db0. An inactive record is untouched.
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

int __cdecl BackgroundLoad(uint32_t* record) noexcept {
    // Match the independently validated Re;Birth3 equivalent of the accepted
    // Re;Birth1 guard. This resource path intentionally is not mode-zero-only.
    if (enabled && ActiveAdv() && record && !record[3] &&
        reinterpret_cast<const unsigned char*>(record)[0x114] == 2 && record[2]) {
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

uint8_t __cdecl TalkReady(uintptr_t talk) noexcept {
    // The mode-zero native skip caller already completes the first track.
    // Complete both presentation tracks, retaining the original root/child
    // readiness checks and the caller's native progression and cleanup.
    if (talk) {
        CompleteTrack(reinterpret_cast<float*>(talk + 0x50));
        CompleteTrack(reinterpret_cast<float*>(talk + 0x60));
    }
    return originalTalkReady(talk);
}

uint32_t __cdecl CharacterReady(uintptr_t character) noexcept {
    // Independently validated scalar setters; leave resource state bytes and
    // the unvalidated +0x1628 track untouched. Preserve the full readiness mask.
    if (character) for (const size_t offset : {0x1410u, 0x152cu, 0x153cu, 0x158cu, 0x1618u})
        CompleteTrack(reinterpret_cast<float*>(character + offset));
    return originalCharacterReady(character);
}

uint32_t __cdecl CharacterUpdate(uintptr_t character) noexcept {
    const uintptr_t adv = ActiveAdv();
    if (enabled && adv && character && *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0 &&
        *reinterpret_cast<const uint8_t*>(character + 0x28) == 2) {
        auto* mp = reinterpret_cast<uint32_t*>(character + 0x288);
        auto& state = *reinterpret_cast<uint8_t*>(character + 0x4d0);
        // A queued visual with no object, buffer, decoder or request can take
        // the native missing-ID path. Never cancel an in-flight resource.
        if ((state == 1 || state == 2) && !mp[2] && !mp[3] && !mp[4] && !mp[5]) {
            mp[0] = 0;
            state = 2;
        }
    }
    return originalCharacterUpdate(character);
}

void __cdecl CharacterSetup(uint32_t mode, uint32_t character, int arg3, int arg4, uint32_t arg5) noexcept {
    const uintptr_t adv = ActiveAdv();
    if (enabled && adv && *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0 &&
        !( *reinterpret_cast<const uint32_t*>(adv + AdvFlagsOffset) & 0x400) && (mode == 0 || mode == 2)) {
        const uintptr_t manager = *reinterpret_cast<const uintptr_t*>(gameBase + 0x49a4c8);
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

void __fastcall TrackGroup(uintptr_t record, void*, float scale) noexcept {
    // 0x004a0c40 advances eleven scalar tracks, then two rotation records.
    // Preserve the original routine, rotations and all downstream resource setup.
    if (record) for (size_t index = 0; index < 11; ++index)
        CompleteTrack(reinterpret_cast<float*>(record + index * 0x10));
    originalTrackGroup(record, scale);
}

bool BlackoutActive() noexcept {
    const uintptr_t adv = ActiveAdv();
    return enabled && adv && *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0;
}
uintptr_t InfoHandle() noexcept {
    const uintptr_t manager = *reinterpret_cast<const uintptr_t*>(gameBase + 0x49a4c8);
    // Inline AdvWndInfo record begins at manager+0x6c, not a pointer at +0x6c.
    if (manager && *reinterpret_cast<const uintptr_t*>(manager) &&
        (*reinterpret_cast<const uint32_t*>(manager + 0x27c) & 1)) {
        const auto widget = *reinterpret_cast<const uintptr_t*>(manager + 0x70);
        if (widget) return widget;
    }
    // Script-owned ADV_INFO records are separate from the inline manager
    // record. Follow the same visible-widget list used by 0x0066e020.
    const auto windows = *reinterpret_cast<const uintptr_t*>(gameBase + 0x684ee4);
    if (!windows) return 0;
    auto widget = *reinterpret_cast<const uintptr_t*>(windows + 0x59a38);
    for (size_t count = 0; widget && count < 4096; ++count) {
        const auto name = reinterpret_cast<const char*>(widget + 0x74);
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
constexpr uint32_t PresentationRva = 0x32ae82, SwapIatRva = 0x38304c;
using PreparedSite = owned_patch::PreparedCall;
using owned_patch::IsOriginalExecutable;
using owned_patch::BytesAt;
} // namespace

bool Install(const Context& context, const AdvFastForwardPatchOps* injectedOps) noexcept {
    const AdvFastForwardPatchOps productionOps{RetargetCalls, RetargetBytes};
    const auto& ops = injectedOps ? *injectedOps : productionOps;
    if (context.spec.id != GameId::Rebirth3) { Log("AdvFastForward rebirth3 wrong target"); return false; }
    if (!ops.retargetCalls || !ops.retargetBytes) { Log("AdvFastForward rebirth3 unavailable patch operations"); return false; }
    gameBase = reinterpret_cast<uintptr_t>(context.game);
    originalScaledTrack = reinterpret_cast<ScaledTrackFn>(gameBase + ScaledTrackRva);
    originalBackgroundLoad = reinterpret_cast<BackgroundLoadFn>(gameBase + BackgroundLoadRva);
    originalAdvSePoll = reinterpret_cast<AdvSePollFn>(gameBase + AdvSePollRva);
    originalTrackGroup = reinterpret_cast<TrackGroupFn>(gameBase + 0xa0c40);
    originalTalkReady = reinterpret_cast<TalkReadyFn>(gameBase + TalkReadyRva);
    originalCharacterReady = reinterpret_cast<CharacterReadyFn>(gameBase + 0x90fc0);
    originalCharacterUpdate = reinterpret_cast<CharacterReadyFn>(gameBase + 0x914f0);
    originalDelay = reinterpret_cast<DelayFn>(gameBase + 0x7e210);
    originalAggregate = reinterpret_cast<AggregateFn>(gameBase + 0x7e290);
    originalTalkCleanup = reinterpret_cast<TalkCleanupFn>(gameBase + 0x81350);
    originalCharacterCommand = reinterpret_cast<CharacterCommandFn>(gameBase + 0x8b610);
    originalManager = reinterpret_cast<ManagerFn>(gameBase + 0x9c620);
    originalCgReady = reinterpret_cast<CgReadyFn>(gameBase + 0x8b790);
    originalCharacterExit = reinterpret_cast<CharacterExitFn>(gameBase + 0x8b910);
    originalAdvBinCreate = reinterpret_cast<AdvBinCreateFn>(gameBase + 0x8e890);
    originalSetup = reinterpret_cast<SetupFn>(gameBase + 0x7f4d0);
    resolveTask = reinterpret_cast<ResolveFn>(gameBase + 0x70eb0);
    lookupCharacter = reinterpret_cast<LookupFn>(gameBase + 0x7bf30);
    InterlockedExchange(&enabled, 0);
    const auto swap = *reinterpret_cast<BOOL (WINAPI**)(HDC)>(gameBase + SwapIatRva);
    if (!swap) return false;
    ConfigureAdvBlackout(gameBase, swap, BlackoutActive,
        reinterpret_cast<AdvWindowSkinFn>(gameBase + 0x26ff60), InfoHandle);
    const auto presentationExpected = AdvBlackoutExpectedCall(gameBase, SwapIatRva);
    const auto presentationReplacement = AdvBlackoutReplacement(gameBase + PresentationRva);
    std::array<PreparedSite, FastForwardSiteCount> sites{{
        {{0x00079d60, {0xe8,0x4b,0xe0,0x02,0x00}, reinterpret_cast<void*>(&DelayTrack)}},
        {{0x0008d9da, {0xe8,0xe1,0xfc,0xff,0xff}, reinterpret_cast<void*>(&BackgroundLoad)}},
        {{0x0007ee19, {0xe8,0xe2,0x7c,0x00,0x00}, reinterpret_cast<void*>(&AdvSePoll)}},
        {{0x0008d91a, {0xe8,0x91,0xa4,0x01,0x00}, reinterpret_cast<void*>(&BackgroundTrack)}},
        {{0x000a74c1, {0xe8,0xea,0x08,0x00,0x00}, reinterpret_cast<void*>(&FadeTrack)}},
        {{0x000a0bec, {0xe8,0x4f,0x00,0x00,0x00}, reinterpret_cast<void*>(&TrackGroup)}},
        {{0x000785c0, {0xe8,0x0b,0xa7,0x02,0x00}, reinterpret_cast<void*>(&TalkReady)}},
        {{0x0008c79e, {0xe8,0xed,0x20,0x00,0x00}, reinterpret_cast<void*>(&AdvBinCreate)}},
        {{0x00080d63, {0xe8,0x68,0xe7,0xff,0xff}, reinterpret_cast<void*>(&CharacterSetup)}},
        {{0x0009480c, {0xe8,0xaf,0xc7,0xff,0xff}, reinterpret_cast<void*>(&CharacterReady)}},
        {{0x00091bfd, {0xe8,0xee,0xf8,0xff,0xff}, reinterpret_cast<void*>(&CharacterUpdate)}},
        {{0x26e113, {0xe8,0x48,0x1e,0x00,0x00}, reinterpret_cast<void*>(&AdvBlackoutWindowSkin)}},
        {{0x26e2f1, {0xe8,0x6a,0x1c,0x00,0x00}, reinterpret_cast<void*>(&AdvBlackoutWindowSkin)}},
        {{0x27113d, {0xe8,0x1e,0xee,0xff,0xff}, reinterpret_cast<void*>(&AdvBlackoutWindowSkin)}},
        {{0x26e6e5, {0xe8,0x76,0x18,0x00,0x00}, reinterpret_cast<void*>(&AdvBlackoutWindowSkin)}},
        {{0x7e697, CallTo(0x7e697, 0x7e210), reinterpret_cast<void*>(&Delay)}},
        {{0x7e6b6, CallTo(0x7e6b6, 0x7e290), reinterpret_cast<void*>(&Aggregate)}},
        {{0x815fe, CallTo(0x815fe, 0x81350), reinterpret_cast<void*>(&TalkCleanup)}},
        {{0x80d9b, CallTo(0x80d9b, 0x8b610), reinterpret_cast<void*>(&CharacterCommand)}},
        {{0x8378f, CallTo(0x8378f, 0x9c620), reinterpret_cast<void*>(&CgCommandManager)}},
        {{0x82d68, CallTo(0x82d68, 0x8b790), reinterpret_cast<void*>(&CgReady)}},
        {{0x80d49, CallTo(0x80d49, 0x8b910), reinterpret_cast<void*>(&CharacterExit)}},
    }};
    const uintptr_t currentAdv = *reinterpret_cast<const uintptr_t*>(gameBase + ActiveAdvPointerRva);
    if (currentAdv && *reinterpret_cast<const uintptr_t*>(currentAdv + AdvRequestOffset)) {
        Log("AdvFastForward rebirth3 late install refused"); return false;
    }
    const owned_patch::BytePatch presentation{PresentationRva, presentationExpected.data(),
        presentationReplacement.data(), presentationExpected.size(), "blackout "};
    const PatchResult result = owned_patch::Install(context, sites, ops, "AdvFastForward rebirth3", &presentation, 1);
    if (!result.Succeeded()) return false;
    InterlockedExchange(&enabled, 1);
    Log("AdvFastForward installed twenty-two guarded calls and blackout present hook game=rebirth3");
    return true;
}

} // namespace rebirth3_fast_forward

bool InstallRebirth3AdvFastForward(const Context& context, const AdvFastForwardPatchOps* ops) noexcept {
    return rebirth3_fast_forward::Install(context, ops);
}

} // namespace rebirths

#ifdef REBIRTHS_TEST_CONTRACTS
namespace rebirths::testing::rebirth3_fast_forward {
DelayTrackHook HookDelayTrack() noexcept { return &rebirths::rebirth3_fast_forward::DelayTrack; }
void BindTrackGroup(TrackGroupFn callback) noexcept { rebirths::rebirth3_fast_forward::originalTrackGroup = callback; }
void BindAdvBinCreate(AdvBinCreateFn callback) noexcept { rebirths::rebirth3_fast_forward::originalAdvBinCreate = callback; }
void BindSetup(SetupFn callback) noexcept { rebirths::rebirth3_fast_forward::originalSetup = callback; }
void BindResolveTask(ResolveFn callback) noexcept { rebirths::rebirth3_fast_forward::resolveTask = callback; }
void BindLookupCharacter(LookupFn callback) noexcept { rebirths::rebirth3_fast_forward::lookupCharacter = callback; }
void BindScaledTrack(ScaledTrackFn callback) noexcept { rebirths::rebirth3_fast_forward::originalScaledTrack = callback; }
ScaledTrackFn NativeScaledTrack() noexcept { return rebirths::rebirth3_fast_forward::originalScaledTrack; }
void BindBackgroundLoad(BackgroundLoadFn callback) noexcept { rebirths::rebirth3_fast_forward::originalBackgroundLoad = callback; }
void BindAdvSePoll(AdvSePollFn callback) noexcept { rebirths::rebirth3_fast_forward::originalAdvSePoll = callback; }
void BindTalkReady(TalkReadyFn callback) noexcept { rebirths::rebirth3_fast_forward::originalTalkReady = callback; }
void BindCharacterReady(CharacterReadyFn callback) noexcept { rebirths::rebirth3_fast_forward::originalCharacterReady = callback; }
void BindCharacterUpdate(CharacterReadyFn callback) noexcept { rebirths::rebirth3_fast_forward::originalCharacterUpdate = callback; }
void BindDelay(DelayFn callback) noexcept { rebirths::rebirth3_fast_forward::originalDelay = callback; }
void BindAggregate(AggregateFn callback) noexcept { rebirths::rebirth3_fast_forward::originalAggregate = callback; }
void BindTalkCleanup(TalkCleanupFn callback) noexcept { rebirths::rebirth3_fast_forward::originalTalkCleanup = callback; }
void BindCharacterCommand(CharacterCommandFn callback) noexcept { rebirths::rebirth3_fast_forward::originalCharacterCommand = callback; }
void BindManager(ManagerFn callback) noexcept { rebirths::rebirth3_fast_forward::originalManager = callback; }
void BindCgReady(CgReadyFn callback) noexcept { rebirths::rebirth3_fast_forward::originalCgReady = callback; }
void BindCharacterExit(CharacterExitFn callback) noexcept { rebirths::rebirth3_fast_forward::originalCharacterExit = callback; }
void SetActive(bool active) noexcept { InterlockedExchange(&rebirths::rebirth3_fast_forward::enabled, active ? 1 : 0); }
LONG Active() noexcept { return InterlockedCompareExchange(&rebirths::rebirth3_fast_forward::enabled, 0, 0); }
uintptr_t ActiveAdv() noexcept { return rebirths::rebirth3_fast_forward::ActiveAdv(); }
void __cdecl BackgroundTrack(float* record, float scale) noexcept { return rebirths::rebirth3_fast_forward::BackgroundTrack(record, scale); }
void __cdecl FadeTrack(float* record, float scale) noexcept { return rebirths::rebirth3_fast_forward::FadeTrack(record, scale); }
void __cdecl DelayTrack(float* record, float scale) noexcept { return rebirths::rebirth3_fast_forward::DelayTrack(record, scale); }
int __cdecl BackgroundLoad(uint32_t* record) noexcept { return rebirths::rebirth3_fast_forward::BackgroundLoad(record); }
uint8_t __fastcall AdvSePoll(uintptr_t context, void* unused1) noexcept { return rebirths::rebirth3_fast_forward::AdvSePoll(context, unused1); }
uint8_t __cdecl TalkReady(uintptr_t talk) noexcept { return rebirths::rebirth3_fast_forward::TalkReady(talk); }
uint32_t __cdecl CharacterReady(uintptr_t character) noexcept { return rebirths::rebirth3_fast_forward::CharacterReady(character); }
uint32_t __cdecl CharacterUpdate(uintptr_t character) noexcept { return rebirths::rebirth3_fast_forward::CharacterUpdate(character); }
void __cdecl CharacterSetup(uint32_t mode, uint32_t character, int arg3, int arg4, uint32_t arg5) noexcept { return rebirths::rebirth3_fast_forward::CharacterSetup(mode, character, arg3, arg4, arg5); }
void __cdecl AdvBinCreate(uintptr_t manager, uint32_t operation, uint32_t entry, uint32_t modeFlag) noexcept { return rebirths::rebirth3_fast_forward::AdvBinCreate(manager, operation, entry, modeFlag); }
void __fastcall TrackGroup(uintptr_t record, void* unused1, float scale) noexcept { return rebirths::rebirth3_fast_forward::TrackGroup(record, unused1, scale); }
uintptr_t InfoHandle() noexcept { return rebirths::rebirth3_fast_forward::InfoHandle(); }
int __cdecl Delay(int* state, float* record, float duration) noexcept { return rebirths::rebirth3_fast_forward::Delay(state, record, duration); }
int __cdecl Aggregate(uint32_t mask, int wait, uint32_t* output) noexcept { return rebirths::rebirth3_fast_forward::Aggregate(mask, wait, output); }
int __cdecl TalkCleanup(int* state, int operation, int arg3, int arg4, uint32_t arg5, uint32_t arg6) noexcept { return rebirths::rebirth3_fast_forward::TalkCleanup(state, operation, arg3, arg4, arg5, arg6); }
int __cdecl CharacterCommand(uint32_t id, int arg, int expression, float duration) noexcept { return rebirths::rebirth3_fast_forward::CharacterCommand(id, arg, expression, duration); }
uintptr_t __cdecl CgCommandManager() noexcept { return rebirths::rebirth3_fast_forward::CgCommandManager(); }
int __cdecl CgReady(uintptr_t object, float duration) noexcept { return rebirths::rebirth3_fast_forward::CgReady(object, duration); }
int __cdecl CharacterExit(int* state, uint32_t id, int arg, float duration) noexcept { return rebirths::rebirth3_fast_forward::CharacterExit(state, id, arg, duration); }
}
#endif
