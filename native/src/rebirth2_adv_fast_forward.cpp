#ifdef REBIRTHS_TEST_CONTRACTS
#include "adv_test_contracts.hpp"
#endif
#include "rebirth2_adv_fast_forward.hpp"
#include "adv_blackout.hpp"

#include "owned_patch_install.hpp"
#include "adv_operations.hpp"

#include <array>
#include <cstring>

namespace rebirths {
namespace rebirth2_fast_forward {
namespace {
using owned_patch::CallTo;

constexpr uint32_t ActiveAdvPointerRva = 0x00443284;
constexpr uint32_t AdvFlagsOffset = 0x48;
constexpr uint32_t AdvModeOffset = 0x10;
constexpr uint32_t AdvRequestOffset = 0x14;
constexpr uint32_t ScenePointerRva = 0x0044f214;
constexpr uint32_t BattleTaskHandleOffset = 0x12a7c8;
constexpr uint32_t ScaledTrackRva = 0x000a47b0;
constexpr uint32_t BackgroundLoadRva = 0x0008a190;
constexpr uint32_t AdvSePollRva = 0x000835d0;
constexpr uint32_t TalkReadyRva = 0x0009f6e0;
constexpr size_t FastForwardSiteCount = 26;

uintptr_t gameBase = 0;
volatile LONG enabled = 0;
volatile LONG chapterEnabled = 0;
using ChapterUpdateFn = int (__cdecl*)(uint32_t*);
ChapterUpdateFn originalChapterUpdate = nullptr;
using TelopVisualCreateFn = uint8_t (__thiscall*)(uint32_t*, uint32_t, uint32_t);
TelopVisualCreateFn originalTelopVisualCreate = nullptr;
using ScriptParameterFn = const int32_t* (__cdecl*)(uintptr_t, uint32_t);
using TelopPayloadFn = uintptr_t (__cdecl*)();
ScriptParameterFn originalScriptParameter = nullptr;
TelopPayloadFn telopPayload = nullptr;
uintptr_t telopCommandVm = 0, skippedTelopVm = 0, skippedTelopRecord = 0;
uintptr_t skippedTelopHandle = 0, skippedTelopPayload = 0;
uintptr_t skippedTelopAdv = 0, skippedTelopRequest = 0;

const int32_t* __cdecl TelopParameter(uintptr_t vm, uint32_t index) noexcept {
    telopCommandVm = vm; // Exact visual-command operand call; original value retained.
    return originalScriptParameter(vm, index);
}

const int32_t* __cdecl TelopWaitParameter(uintptr_t vm, uint32_t index) noexcept {
    const int32_t* value = originalScriptParameter(vm, index);
    const uintptr_t adv = *reinterpret_cast<const uintptr_t*>(gameBase + ActiveAdvPointerRva);
    const uintptr_t scene = *reinterpret_cast<const uintptr_t*>(gameBase + ScenePointerRva);
    if (chapterEnabled && !index && value && *value > 0 && vm && vm == skippedTelopVm &&
        skippedTelopRecord && *reinterpret_cast<const uintptr_t*>(vm + 0x4004) == skippedTelopRecord &&
        adv && adv == skippedTelopAdv && skippedTelopRequest &&
        *reinterpret_cast<const uintptr_t*>(adv + AdvRequestOffset) == skippedTelopRequest &&
        *reinterpret_cast<const uint8_t*>(adv + 0x84fc) == 6 &&
        !*reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) &&
        !*reinterpret_cast<const uintptr_t*>(gameBase + 0x44328c) &&
        (!scene || !*reinterpret_cast<const uint32_t*>(scene + BattleTaskHandleOffset)) &&
        skippedTelopHandle && *reinterpret_cast<const uintptr_t*>(gameBase + 0x4432e8) == skippedTelopHandle &&
        telopPayload() == skippedTelopPayload) {
        // The native opcode7 handler still evaluates/completes the delay and
        // advances its own instruction. Never modify script data or the VM IP.
        static const int32_t completeDuration = 0;
        return &completeDuration;
    }
    return value;
}

uint8_t __fastcall TelopVisualCreate(uint32_t* record, void*, uint32_t banks, uint32_t textures) noexcept {
    // Only the AdvTelop command's visual allocation call is redirected. Keep
    // the initialized owner and outer commands; native empty-resource polls
    // and destruction already handle an absent optional visual object.
    if (chapterEnabled && record && !record[0] && !record[1] && !record[2] &&
        !record[3] && !record[4] && !record[5] && !record[6]) {
        const uintptr_t payload = telopPayload();
        const uintptr_t slot = reinterpret_cast<uintptr_t>(record);
        if (payload && slot >= payload + 4 && slot < payload + 4 + 10 * 0x48 &&
            (slot - payload - 4) % 0x48 == 0 && telopCommandVm) {
            skippedTelopVm = telopCommandVm;
            skippedTelopRecord = *reinterpret_cast<const uintptr_t*>(telopCommandVm + 0x4004);
            skippedTelopPayload = payload;
            skippedTelopHandle = *reinterpret_cast<const uintptr_t*>(gameBase + 0x4432e8);
            skippedTelopAdv = *reinterpret_cast<const uintptr_t*>(gameBase + ActiveAdvPointerRva);
            skippedTelopRequest = skippedTelopAdv ?
                *reinterpret_cast<const uintptr_t*>(skippedTelopAdv + AdvRequestOffset) : 0;
            return 0;
        }
    }
    return originalTelopVisualCreate(record, banks, textures);
}
using ChapterFinishFn = void (__cdecl*)();
ChapterFinishFn originalChapterFinish = nullptr;
uintptr_t chapterWaitOwner = 0, chapterWaitRequest = 0;

void __cdecl ChapterFinish() noexcept {
    chapterWaitOwner = chapterWaitRequest = 0;
    originalChapterFinish(); // Native task deletion and caller's mask restore.
}

uintptr_t ChapterWaitOwner() noexcept {
    const uintptr_t adv = *reinterpret_cast<const uintptr_t*>(gameBase + ActiveAdvPointerRva);
    if (!enabled || !chapterEnabled || !adv || adv != chapterWaitOwner || !chapterWaitRequest ||
        *reinterpret_cast<const uintptr_t*>(adv + AdvRequestOffset) != chapterWaitRequest ||
        *reinterpret_cast<const uint8_t*>(adv + 0x84fc) != 6 ||
        *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) != 0 ||
        *reinterpret_cast<const uintptr_t*>(gameBase + 0x44328c) ||
        *reinterpret_cast<const uintptr_t*>(gameBase + 0x4432e8)) return 0;
    const uintptr_t scene = *reinterpret_cast<const uintptr_t*>(gameBase + ScenePointerRva);
    return scene && *reinterpret_cast<const uint32_t*>(scene + BattleTaskHandleOffset) ? 0 : adv;
}

int __cdecl ChapterUpdate(uint32_t* record) noexcept {
    // The task callback has allocated/initialized its 0x18-byte payload. Omit
    // only the three optional presentation children. Retain the task at native
    // ready state3 until the script's close command sets state4; otherwise that
    // command would recreate a task whose ownership disappeared too early.
    if (chapterEnabled && record && !record[1] && !record[2] && !record[3]) {
        auto& state = reinterpret_cast<uint8_t*>(record)[0x14];
        if (state == 1) {
            // Keep provenance after the visual task closes: its native script
            // contains delay/fade commands before the negative chapter command.
            chapterWaitOwner = *reinterpret_cast<const uintptr_t*>(gameBase + ActiveAdvPointerRva);
            chapterWaitRequest = chapterWaitOwner ?
                *reinterpret_cast<const uintptr_t*>(chapterWaitOwner + AdvRequestOffset) : 0;
            state = 3;
        }
        else if (state == 4) state = 5;
    }
    return originalChapterUpdate(record);
}
using ScaledTrackFn = void (__cdecl*)(float*, float);
using BackgroundLoadFn = uint8_t (__cdecl*)(uint32_t*);
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
ScaledTrackFn originalScaledTrack = nullptr;
BackgroundLoadFn originalBackgroundLoad = nullptr;
AdvSePollFn originalAdvSePoll = nullptr;
using SoundHandlePollFn = uint8_t (__cdecl*)(uintptr_t);
using CgMotionPollFn = uint32_t (__thiscall*)(uintptr_t);
SoundHandlePollFn originalSoundHandlePoll = nullptr;
CgMotionPollFn originalCgMotionPoll = nullptr;
using AdvSoundUpdateFn = uint8_t (__thiscall*)(uint32_t*);
AdvSoundUpdateFn originalAdvSoundUpdate = nullptr;
using ScriptSoundPlayFn = uintptr_t (__cdecl*)(uintptr_t, uint32_t, uint32_t, int);
ScriptSoundPlayFn originalScriptSoundPlay = nullptr;

uintptr_t __cdecl ScriptSoundPlay(uintptr_t resource, uint32_t cue, uint32_t group, int priority) noexcept {
    const uintptr_t adv = *reinterpret_cast<const uintptr_t*>(gameBase + ActiveAdvPointerRva);
    const uintptr_t scene = *reinterpret_cast<const uintptr_t*>(gameBase + ScenePointerRva);
    if (enabled && adv && *reinterpret_cast<const uintptr_t*>(adv + AdvRequestOffset) &&
        *reinterpret_cast<const uint8_t*>(adv + 0x84fc) == 6 &&
        *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0 &&
        (!scene || !*reinterpret_cast<const uint32_t*>(scene + BattleTaskHandleOffset)))
        return 0; // Native no-handle path clears script output; no sound ownership exists.
    return originalScriptSoundPlay(resource, cue, group, priority);
}

uint8_t __fastcall AdvSoundUpdate(uint32_t* record, void*) noexcept {
    // This call belongs exclusively to ADV_SE children. Keep resource/task
    // initialization, but finish a fresh child before it creates an audio
    // handle. Callback-owned deletion still executes the original cleanup.
    // Independent of native skip: deferred sound must not start after the
    // VM has already entered a battle.
    if (enabled && record && !record[0] && reinterpret_cast<uint8_t*>(record)[0x10] == 1)
        reinterpret_cast<uint8_t*>(record)[0x10] = 5;
    return originalAdvSoundUpdate(record);
}
TalkReadyFn originalTalkReady = nullptr;
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
        *reinterpret_cast<const uint8_t*>(adv + 0x84fc) == 6 &&
        (*reinterpret_cast<const uint32_t*>(adv + AdvFlagsOffset) & 8) ? adv : 0;
}

void FinishTrack(float* record) noexcept {
    // Exact terminal stores from 0x004a47b0. An inactive record is untouched.
    adv_operations::FinishTrack(record);
}

bool CompleteTrack(float* record) noexcept {
    const uintptr_t adv = ActiveAdv();
    if (!enabled || !adv || *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) != 0 || !record) return false;
    FinishTrack(record);
    return true;
}

void __cdecl BackgroundTrack(float* record, float scale) noexcept {
    if (!CompleteTrack(record)) originalScaledTrack(record, scale);
}

void __cdecl FadeTrack(float* record, float scale) noexcept {
    if (record && ChapterWaitOwner()) { FinishTrack(record); return; }
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
    // Match the independently validated Re;Birth2 equivalent of the accepted
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

void __cdecl CharacterSetup(uint32_t mode, uint32_t character, int arg3, int arg4, uint32_t arg5) noexcept {
    const uintptr_t adv = ActiveAdv();
    if (enabled && adv && *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0 &&
        !( *reinterpret_cast<const uint32_t*>(adv + AdvFlagsOffset) & 0x400) && (mode == 0 || mode == 2)) {
        const uintptr_t manager = *reinterpret_cast<const uintptr_t*>(gameBase + 0x4432c8);
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
    if (enabled && adv && *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0 && operation == 1 && static_cast<uint8_t>(modeFlag) == 0) return;
    originalAdvBinCreate(manager, operation, entry, modeFlag);
}

void __fastcall TrackGroup(uintptr_t record, void*, float scale) noexcept {
    // 0x0049d650 advances eleven scalar tracks, then two rotation records.
    // Preserve the original routine, rotations and all downstream resource setup.
    if (record) for (size_t index = 0; index < 11; ++index)
        CompleteTrack(reinterpret_cast<float*>(record + index * 0x10));
    originalTrackGroup(record, scale);
}

bool SkipPresentationWaits() noexcept {
    const uintptr_t adv = ActiveAdv();
    return enabled && adv && *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0;
}

int __cdecl Delay(int* state, float* record, float duration) noexcept {
    const uintptr_t adv = SkipPresentationWaits() ? ActiveAdv() : ChapterWaitOwner();
    const bool complete = adv && record == reinterpret_cast<float*>(adv + 0x18);
    // Preserve the native producer, including its initial track setup. Complete
    // its scalar track and let the native state-one branch advance in this call.
    return adv_operations::PollDelay(complete, state,
        [&]() noexcept { return originalDelay(state, record, duration); },
        [&]() noexcept { FinishTrack(record); });
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
bool BlackoutActive() noexcept {
    const uintptr_t adv = ActiveAdv();
    if (!enabled || !adv || *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) != 0) return false;
    // FILE4's ADV request can remain live throughout battle. Match RB2's
    // native battle-presence predicate (VA 0x00597850), independently of ADV
    // skip/cleanup ordering. Only suppress our render clear; leave native
    // fade tracks, skip flags, script effects and task ownership untouched.
    const uintptr_t scene = *reinterpret_cast<const uintptr_t*>(gameBase + ScenePointerRva);
    return !scene || !*reinterpret_cast<const uint32_t*>(scene + BattleTaskHandleOffset);
}

uint8_t __cdecl SoundHandlePoll(uintptr_t sound) noexcept {
    // Command 3020's explicit-handle branch, not the shared audio predicate.
    const uint8_t result = originalSoundHandlePoll(sound);
    return SkipPresentationWaits() && result ? 0 : result;
}

uint32_t __fastcall CgMotionPoll(uintptr_t object, void*) noexcept {
    // RB2 0x004812b0 reads these pan axes before its other tracks/resources.
    if (object) {
        CompleteTrack(reinterpret_cast<float*>(object + 0x30));
        CompleteTrack(reinterpret_cast<float*>(object + 0x40));
    }
    return originalCgMotionPoll(object);
}
uintptr_t InfoHandle() noexcept {
    const uintptr_t manager = *reinterpret_cast<const uintptr_t*>(gameBase + 0x4432c8);
    // Inline AdvWndInfo record begins at manager+0x6c, not a pointer at +0x6c.
    if (manager && *reinterpret_cast<const uintptr_t*>(manager) &&
        (*reinterpret_cast<const uint32_t*>(manager + 0x27c) & 1)) {
        const auto widget = *reinterpret_cast<const uintptr_t*>(manager + 0x70);
        if (widget) return widget;
    }
    // Script-owned ADV_INFO records are separate from the inline manager
    // record. Follow the same visible-widget list used by 0x00630890.
    const auto windows = *reinterpret_cast<const uintptr_t*>(gameBase + 0x5cffdc);
    if (!windows) return 0;
    auto widget = *reinterpret_cast<const uintptr_t*>(windows + 0x59138);
    for (size_t count = 0; widget && count < 4096; ++count) {
        const auto name = reinterpret_cast<const char*>(widget + 0x70);
        if (std::memcmp(name, "AdvWndInfo", sizeof("AdvWndInfo")) == 0 ||
            std::memcmp(name, "AdvWndInfoMoney", sizeof("AdvWndInfoMoney")) == 0) return widget;
        widget = *reinterpret_cast<const uintptr_t*>(widget + 0x10);
    }
    return 0;
}

constexpr uint32_t PresentationRva = 0x2e82a2, SwapIatRva = 0x33b04c;
using PreparedSite = owned_patch::PreparedCall;
using owned_patch::IsOriginalExecutable;
using owned_patch::BytesAt;
} // namespace

bool Install(const Context& context, const AdvFastForwardPatchOps* injectedOps) noexcept {
    const AdvFastForwardPatchOps productionOps{RetargetCalls, RetargetBytes};
    const auto& ops = injectedOps ? *injectedOps : productionOps;
    if (context.spec.id != GameId::Rebirth2) { Log("AdvFastForward rebirth2 wrong target"); return false; }
    if (!ops.retargetCalls || !ops.retargetBytes) { Log("AdvFastForward rebirth2 unavailable patch operations"); return false; }
    gameBase = reinterpret_cast<uintptr_t>(context.game);
    originalScaledTrack = reinterpret_cast<ScaledTrackFn>(gameBase + ScaledTrackRva);
    originalBackgroundLoad = reinterpret_cast<BackgroundLoadFn>(gameBase + BackgroundLoadRva);
    originalAdvSePoll = reinterpret_cast<AdvSePollFn>(gameBase + AdvSePollRva);
    originalSoundHandlePoll = reinterpret_cast<SoundHandlePollFn>(gameBase + 0x22aa70);
    originalCgMotionPoll = reinterpret_cast<CgMotionPollFn>(gameBase + 0x812b0);
    originalAdvSoundUpdate = reinterpret_cast<AdvSoundUpdateFn>(gameBase + 0x9bc60);
    originalScriptSoundPlay = reinterpret_cast<ScriptSoundPlayFn>(gameBase + 0x228800);
    originalTrackGroup = reinterpret_cast<TrackGroupFn>(gameBase + 0x9d650);
    originalTalkReady = reinterpret_cast<TalkReadyFn>(gameBase + TalkReadyRva);
    originalDelay = reinterpret_cast<DelayFn>(gameBase + 0x7acf0);
    originalChapterFinish = reinterpret_cast<ChapterFinishFn>(gameBase + 0x8beb0);
    chapterWaitOwner = chapterWaitRequest = 0;
    originalAggregate = reinterpret_cast<AggregateFn>(gameBase + 0x7ad70);
    originalTalkCleanup = reinterpret_cast<TalkCleanupFn>(gameBase + 0x7de50);
    originalCharacterCommand = reinterpret_cast<CharacterCommandFn>(gameBase + 0x880e0);
    originalManager = reinterpret_cast<ManagerFn>(gameBase + 0x99080);
    originalCgReady = reinterpret_cast<CgReadyFn>(gameBase + 0x88260);
    originalCharacterExit = reinterpret_cast<CharacterExitFn>(gameBase + 0x883e0);
    originalAdvBinCreate = reinterpret_cast<AdvBinCreateFn>(gameBase + 0x8b340);
    originalSetup = reinterpret_cast<SetupFn>(gameBase + 0x7bfd0);
    resolveTask = reinterpret_cast<ResolveFn>(gameBase + 0x6dba0);
    lookupCharacter = reinterpret_cast<LookupFn>(gameBase + 0x78ad0);
    InterlockedExchange(&enabled, 0);
    const auto swap = *reinterpret_cast<BOOL (WINAPI**)(HDC)>(gameBase + SwapIatRva);
    if (!swap) return false;
    ConfigureAdvBlackout(gameBase, swap, BlackoutActive,
        reinterpret_cast<AdvWindowSkinFn>(gameBase + 0x2327b0), InfoHandle);
    const auto presentationExpected = AdvBlackoutExpectedCall(gameBase, SwapIatRva);
    const auto presentationReplacement = AdvBlackoutReplacement(gameBase + PresentationRva);
    std::array<PreparedSite, FastForwardSiteCount> sites{{
        {{0x804b9, CallTo(0x804b9, 0x8beb0), reinterpret_cast<void*>(&ChapterFinish)}},
        {{0x291a32, CallTo(0x291a32, 0x228800), reinterpret_cast<void*>(&ScriptSoundPlay)}},
        {{0x294ed9, CallTo(0x294ed9, 0x228800), reinterpret_cast<void*>(&ScriptSoundPlay)}},
        {{0x9b90e, CallTo(0x9b90e, 0x9bc60), reinterpret_cast<void*>(&AdvSoundUpdate)}},
        {{0x7b89f, CallTo(0x7b89f, 0x22aa70), reinterpret_cast<void*>(&SoundHandlePoll)}},
        {{0x813ba, CallTo(0x813ba, 0x812b0), reinterpret_cast<void*>(&CgMotionPoll)}},
        {{0x00076900, {0xe8,0xab,0xde,0x02,0x00}, reinterpret_cast<void*>(&DelayTrack)}},
        {{0x0008a48e, {0xe8,0xfd,0xfc,0xff,0xff}, reinterpret_cast<void*>(&BackgroundLoad)}},
        {{0x0007b8d9, {0xe8,0xf2,0x7c,0x00,0x00}, reinterpret_cast<void*>(&AdvSePoll)}},
        {{0x0008a3ea, {0xe8,0xc1,0xa3,0x01,0x00}, reinterpret_cast<void*>(&BackgroundTrack)}},
        {{0x000a3ec1, {0xe8,0xea,0x08,0x00,0x00}, reinterpret_cast<void*>(&FadeTrack)}},
        {{0x0009d5fc, {0xe8,0x4f,0x00,0x00,0x00}, reinterpret_cast<void*>(&TrackGroup)}},
        {{0x000751b0, {0xe8,0x2b,0xa5,0x02,0x00}, reinterpret_cast<void*>(&TalkReady)}},
        {{0x0008926e, {0xe8,0xcd,0x20,0x00,0x00}, reinterpret_cast<void*>(&AdvBinCreate)}},
        {{0x0007d863, {0xe8,0x68,0xe7,0xff,0xff}, reinterpret_cast<void*>(&CharacterSetup)}},
        {{0x0007b177, {0xe8,0x74,0xfb,0xff,0xff}, reinterpret_cast<void*>(&Delay)}},
        {{0x0007b196, {0xe8,0xd5,0xfb,0xff,0xff}, reinterpret_cast<void*>(&Aggregate)}},
        {{0x0007e0fe, {0xe8,0x4d,0xfd,0xff,0xff}, reinterpret_cast<void*>(&TalkCleanup)}},
        {{0x0007d89b, {0xe8,0x40,0xa8,0x00,0x00}, reinterpret_cast<void*>(&CharacterCommand)}},
        {{0x0008028f, {0xe8,0xec,0x8d,0x01,0x00}, reinterpret_cast<void*>(&CgCommandManager)}},
        {{0x0007f868, {0xe8,0xf3,0x89,0x00,0x00}, reinterpret_cast<void*>(&CgReady)}},
        {{0x0007d849, {0xe8,0x92,0xab,0x00,0x00}, reinterpret_cast<void*>(&CharacterExit)}},
        {{0x230983, CallTo(0x230983, 0x2327b0), reinterpret_cast<void*>(&AdvBlackoutWindowSkin)}},
        {{0x230b61, CallTo(0x230b61, 0x2327b0), reinterpret_cast<void*>(&AdvBlackoutWindowSkin)}},
        {{0x23398d, CallTo(0x23398d, 0x2327b0), reinterpret_cast<void*>(&AdvBlackoutWindowSkin)}},
        {{0x230f3d, CallTo(0x230f3d, 0x2327b0), reinterpret_cast<void*>(&AdvBlackoutWindowSkin)}},
    }};
    const uintptr_t currentAdv = *reinterpret_cast<const uintptr_t*>(gameBase + ActiveAdvPointerRva);
    if (currentAdv && *reinterpret_cast<const uintptr_t*>(currentAdv + AdvRequestOffset)) {
        Log("AdvFastForward rebirth2 late install refused"); return false;
    }
    const owned_patch::BytePatch presentation{PresentationRva, presentationExpected.data(),
        presentationReplacement.data(), presentationExpected.size(), "blackout "};
    const PatchResult result = owned_patch::Install(context, sites, ops, "AdvFastForward rebirth2", &presentation, 1);
    if (!result.Succeeded()) return false;
    InterlockedExchange(&enabled, 1);
    Log("AdvFastForward installed twenty-six guarded calls and blackout present hook game=rebirth2");
    return true;
}

} // namespace rebirth2_fast_forward

bool InstallRebirth2AdvFastForward(const Context& context, const AdvFastForwardPatchOps* ops) noexcept {
    return rebirth2_fast_forward::Install(context, ops);
}

bool InstallRebirth2SkipChapterIntros(const Context& context, const AdvFastForwardPatchOps* injectedOps) noexcept {
    using namespace rebirth2_fast_forward;
    const AdvFastForwardPatchOps productionOps{RetargetCalls, RetargetBytes};
    const auto& ops = injectedOps ? *injectedOps : productionOps;
    if (context.spec.id != GameId::Rebirth2 || !ops.retargetCalls || !ops.retargetBytes) return false;
    gameBase = reinterpret_cast<uintptr_t>(context.game);
    InterlockedExchange(&chapterEnabled, 0);
    originalChapterUpdate = reinterpret_cast<ChapterUpdateFn>(gameBase + 0x8ccf0);
    originalTelopVisualCreate = reinterpret_cast<TelopVisualCreateFn>(gameBase + 0x81920);
    originalScriptParameter = reinterpret_cast<ScriptParameterFn>(gameBase + 0x24b3b0);
    telopPayload = reinterpret_cast<TelopPayloadFn>(gameBase + 0xa2730);
    telopCommandVm = skippedTelopVm = skippedTelopRecord = 0;
    skippedTelopHandle = skippedTelopPayload = skippedTelopAdv = skippedTelopRequest = 0;
    const std::array<CallSite, 5> sites{{
        {0x8ce0d, CallTo(0x8ce0d, 0x8ccf0), reinterpret_cast<void*>(&ChapterUpdate)},
        {0x80873, CallTo(0x80873, 0x81920), reinterpret_cast<void*>(&TelopVisualCreate)},
        {0x807ff, CallTo(0x807ff, 0x24b3b0), reinterpret_cast<void*>(&TelopParameter)},
        {0x287f87, CallTo(0x287f87, 0x24b3b0), reinterpret_cast<void*>(&TelopWaitParameter)},
        {0x288037, CallTo(0x288037, 0x24b3b0), reinterpret_cast<void*>(&TelopWaitParameter)},
    }};
    if (*reinterpret_cast<const uint32_t*>(gameBase + 0x4432a4) ||
        *reinterpret_cast<const uint32_t*>(gameBase + 0x4432e8)) {
        Log("SkipChapterIntros rebirth2 preflight/late install refused"); return false;
    }
    for (const auto& site : sites)
        if (!IsOriginalExecutable(gameBase + site.rva, site.expected)) {
            Log("SkipChapterIntros rebirth2 preflight failed; no calls changed"); return false;
        }
    for (const auto& site : sites) {
        const auto replacement = CallTo(gameBase + site.rva, reinterpret_cast<uintptr_t>(site.replacement));
        bool installed = false;
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            const bool patched = ops.retargetCalls(context, &site, 1);
            if (patched && IsOriginalExecutable(gameBase + site.rva, replacement)) { installed = true; break; }
            if (BytesAt(gameBase + site.rva, replacement)) break;
        }
        if (installed) continue;
        bool restored = true;
        for (auto it = sites.rbegin(); it != sites.rend(); ++it) {
            const auto patchedBytes = CallTo(gameBase + it->rva, reinterpret_cast<uintptr_t>(it->replacement));
            for (unsigned attempt = 0; attempt < 20 && !IsOriginalExecutable(gameBase + it->rva, it->expected); ++attempt)
                ops.retargetBytes(context, it->rva, patchedBytes.data(), it->expected.data(), 5);
            restored = IsOriginalExecutable(gameBase + it->rva, it->expected) && restored;
        }
        Log("SkipChapterIntros rebirth2 failed rollback=%s", restored ? "complete" : "incomplete");
        return false;
    }
    InterlockedExchange(&chapterEnabled, 1);
    Log("SkipChapterIntros installed chapter lifecycle and N-GEAR visual calls game=rebirth2");
    return true;
}

} // namespace rebirths

#ifdef REBIRTHS_TEST_CONTRACTS
namespace rebirths::testing::rebirth2_fast_forward {
DelayTrackHook HookDelayTrack() noexcept { return &rebirths::rebirth2_fast_forward::DelayTrack; }
void BindChapterUpdate(ChapterUpdateFn callback) noexcept { rebirths::rebirth2_fast_forward::originalChapterUpdate = callback; }
void BindTelopVisualCreate(TelopVisualCreateFn callback) noexcept { rebirths::rebirth2_fast_forward::originalTelopVisualCreate = callback; }
void BindScriptParameter(ScriptParameterFn callback) noexcept { rebirths::rebirth2_fast_forward::originalScriptParameter = callback; }
void BindTelopPayload(TelopPayloadFn callback) noexcept { rebirths::rebirth2_fast_forward::telopPayload = callback; }
void BindChapterFinish(ChapterFinishFn callback) noexcept { rebirths::rebirth2_fast_forward::originalChapterFinish = callback; }
void BindTrackGroup(TrackGroupFn callback) noexcept { rebirths::rebirth2_fast_forward::originalTrackGroup = callback; }
void BindAdvBinCreate(AdvBinCreateFn callback) noexcept { rebirths::rebirth2_fast_forward::originalAdvBinCreate = callback; }
void BindSetup(SetupFn callback) noexcept { rebirths::rebirth2_fast_forward::originalSetup = callback; }
void BindResolveTask(ResolveFn callback) noexcept { rebirths::rebirth2_fast_forward::resolveTask = callback; }
void BindLookupCharacter(LookupFn callback) noexcept { rebirths::rebirth2_fast_forward::lookupCharacter = callback; }
void BindScaledTrack(ScaledTrackFn callback) noexcept { rebirths::rebirth2_fast_forward::originalScaledTrack = callback; }
ScaledTrackFn NativeScaledTrack() noexcept { return rebirths::rebirth2_fast_forward::originalScaledTrack; }
void BindBackgroundLoad(BackgroundLoadFn callback) noexcept { rebirths::rebirth2_fast_forward::originalBackgroundLoad = callback; }
void BindAdvSePoll(AdvSePollFn callback) noexcept { rebirths::rebirth2_fast_forward::originalAdvSePoll = callback; }
void BindSoundHandlePoll(SoundHandlePollFn callback) noexcept { rebirths::rebirth2_fast_forward::originalSoundHandlePoll = callback; }
void BindCgMotionPoll(CgMotionPollFn callback) noexcept { rebirths::rebirth2_fast_forward::originalCgMotionPoll = callback; }
void BindAdvSoundUpdate(AdvSoundUpdateFn callback) noexcept { rebirths::rebirth2_fast_forward::originalAdvSoundUpdate = callback; }
void BindScriptSoundPlay(ScriptSoundPlayFn callback) noexcept { rebirths::rebirth2_fast_forward::originalScriptSoundPlay = callback; }
void BindTalkReady(TalkReadyFn callback) noexcept { rebirths::rebirth2_fast_forward::originalTalkReady = callback; }
void BindDelay(DelayFn callback) noexcept { rebirths::rebirth2_fast_forward::originalDelay = callback; }
void BindAggregate(AggregateFn callback) noexcept { rebirths::rebirth2_fast_forward::originalAggregate = callback; }
void BindTalkCleanup(TalkCleanupFn callback) noexcept { rebirths::rebirth2_fast_forward::originalTalkCleanup = callback; }
void BindCharacterCommand(CharacterCommandFn callback) noexcept { rebirths::rebirth2_fast_forward::originalCharacterCommand = callback; }
void BindManager(ManagerFn callback) noexcept { rebirths::rebirth2_fast_forward::originalManager = callback; }
void BindCgReady(CgReadyFn callback) noexcept { rebirths::rebirth2_fast_forward::originalCgReady = callback; }
void BindCharacterExit(CharacterExitFn callback) noexcept { rebirths::rebirth2_fast_forward::originalCharacterExit = callback; }
void SetActive(bool active) noexcept { InterlockedExchange(&rebirths::rebirth2_fast_forward::enabled, active ? 1 : 0); }
LONG Active() noexcept { return InterlockedCompareExchange(&rebirths::rebirth2_fast_forward::enabled, 0, 0); }
void SetChapterActive(bool active) noexcept { InterlockedExchange(&rebirths::rebirth2_fast_forward::chapterEnabled, active ? 1 : 0); }
LONG ChapterActive() noexcept { return InterlockedCompareExchange(&rebirths::rebirth2_fast_forward::chapterEnabled, 0, 0); }
const int32_t* __cdecl TelopParameter(uintptr_t vm, uint32_t index) noexcept { return rebirths::rebirth2_fast_forward::TelopParameter(vm, index); }
const int32_t* __cdecl TelopWaitParameter(uintptr_t vm, uint32_t index) noexcept { return rebirths::rebirth2_fast_forward::TelopWaitParameter(vm, index); }
uint8_t __fastcall TelopVisualCreate(uint32_t* record, void* unused1, uint32_t banks, uint32_t textures) noexcept { return rebirths::rebirth2_fast_forward::TelopVisualCreate(record, unused1, banks, textures); }
void __cdecl ChapterFinish() noexcept { return rebirths::rebirth2_fast_forward::ChapterFinish(); }
uintptr_t ChapterWaitOwner() noexcept { return rebirths::rebirth2_fast_forward::ChapterWaitOwner(); }
int __cdecl ChapterUpdate(uint32_t* record) noexcept { return rebirths::rebirth2_fast_forward::ChapterUpdate(record); }
uintptr_t __cdecl ScriptSoundPlay(uintptr_t resource, uint32_t cue, uint32_t group, int priority) noexcept { return rebirths::rebirth2_fast_forward::ScriptSoundPlay(resource, cue, group, priority); }
uint8_t __fastcall AdvSoundUpdate(uint32_t* record, void* unused1) noexcept { return rebirths::rebirth2_fast_forward::AdvSoundUpdate(record, unused1); }
uintptr_t ActiveAdv() noexcept { return rebirths::rebirth2_fast_forward::ActiveAdv(); }
void __cdecl BackgroundTrack(float* record, float scale) noexcept { return rebirths::rebirth2_fast_forward::BackgroundTrack(record, scale); }
void __cdecl FadeTrack(float* record, float scale) noexcept { return rebirths::rebirth2_fast_forward::FadeTrack(record, scale); }
void __cdecl DelayTrack(float* record, float scale) noexcept { return rebirths::rebirth2_fast_forward::DelayTrack(record, scale); }
uint8_t __cdecl BackgroundLoad(uint32_t* record) noexcept { return rebirths::rebirth2_fast_forward::BackgroundLoad(record); }
uint8_t __fastcall AdvSePoll(uintptr_t context, void* unused1) noexcept { return rebirths::rebirth2_fast_forward::AdvSePoll(context, unused1); }
uint8_t __cdecl TalkReady(uintptr_t talk) noexcept { return rebirths::rebirth2_fast_forward::TalkReady(talk); }
void __cdecl CharacterSetup(uint32_t mode, uint32_t character, int arg3, int arg4, uint32_t arg5) noexcept { return rebirths::rebirth2_fast_forward::CharacterSetup(mode, character, arg3, arg4, arg5); }
void __cdecl AdvBinCreate(uintptr_t manager, uint32_t operation, uint32_t entry, uint32_t modeFlag) noexcept { return rebirths::rebirth2_fast_forward::AdvBinCreate(manager, operation, entry, modeFlag); }
void __fastcall TrackGroup(uintptr_t record, void* unused1, float scale) noexcept { return rebirths::rebirth2_fast_forward::TrackGroup(record, unused1, scale); }
int __cdecl Delay(int* state, float* record, float duration) noexcept { return rebirths::rebirth2_fast_forward::Delay(state, record, duration); }
int __cdecl Aggregate(uint32_t mask, int wait, uint32_t* output) noexcept { return rebirths::rebirth2_fast_forward::Aggregate(mask, wait, output); }
int __cdecl TalkCleanup(int* state, int operation, int arg3, int arg4, uint32_t arg5, uint32_t arg6) noexcept { return rebirths::rebirth2_fast_forward::TalkCleanup(state, operation, arg3, arg4, arg5, arg6); }
int __cdecl CharacterCommand(uint32_t id, int arg, int expression, float duration) noexcept { return rebirths::rebirth2_fast_forward::CharacterCommand(id, arg, expression, duration); }
uintptr_t __cdecl CgCommandManager() noexcept { return rebirths::rebirth2_fast_forward::CgCommandManager(); }
int __cdecl CgReady(uintptr_t object, float duration) noexcept { return rebirths::rebirth2_fast_forward::CgReady(object, duration); }
int __cdecl CharacterExit(int* state, uint32_t id, int arg, float duration) noexcept { return rebirths::rebirth2_fast_forward::CharacterExit(state, id, arg, duration); }
bool BlackoutActive() noexcept { return rebirths::rebirth2_fast_forward::BlackoutActive(); }
uint8_t __cdecl SoundHandlePoll(uintptr_t sound) noexcept { return rebirths::rebirth2_fast_forward::SoundHandlePoll(sound); }
uint32_t __fastcall CgMotionPoll(uintptr_t object, void* unused1) noexcept { return rebirths::rebirth2_fast_forward::CgMotionPoll(object, unused1); }
uintptr_t InfoHandle() noexcept { return rebirths::rebirth2_fast_forward::InfoHandle(); }
}
#endif
