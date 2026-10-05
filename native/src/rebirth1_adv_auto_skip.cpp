#ifdef REBIRTHS_TEST_CONTRACTS
#include "adv_test_contracts.hpp"
#endif
#include "sega_adv_auto_skip.hpp"
#include "rebirth1_adv_auto_skip.hpp"
#include "rebirth3_adv_auto_skip.hpp"
#include "rebirth2_adv_auto_skip.hpp"

#include "owned_patch_install.hpp"
#include "adv_operations.hpp"

#include <array>
#include <cstring>

namespace rebirths {
namespace {
using owned_patch::CallTo;

constexpr uint32_t StoryInputRva = 0x00009860;
constexpr uint32_t StorySkipRva = 0x00008ed0;
constexpr uint32_t EventRequestRva = 0x0000d180;
constexpr uint32_t ActiveAdvPointerRva = 0x004591c4;
constexpr uint32_t AdvFlagsOffset = 0x48;
constexpr uint32_t AdvModeOffset = 0x10;
constexpr uint32_t InputInhibitOffset = 0x40;
constexpr uint32_t InputDisabledOffset = 0x84f5;

uintptr_t gameBase = 0;
using EventRequestFn = uint32_t (__thiscall*)(uintptr_t, uint32_t);
using StoryInputFn = int (__cdecl*)(uintptr_t);
using StorySkipFn = void (__cdecl*)(uintptr_t, uint32_t);
EventRequestFn originalEventRequest = nullptr;
StoryInputFn originalStoryInput = nullptr;
StorySkipFn setStorySkip = nullptr;
using StoryClearFn = void (__cdecl*)(uintptr_t);
StoryClearFn originalStoryClear = nullptr;
PVOID epochAdv = nullptr;
volatile LONG epochPending = 0;
volatile LONG epochActivated = 0;
volatile LONG activationCount = 0;
volatile LONG enabled = 0;

bool Eligible(uintptr_t adv) noexcept {
    // These are the input function's own mode/inhibition boundaries.  Do not
    // make a pre-load state-1 write or emulate an input device.
    return adv && *reinterpret_cast<const unsigned char*>(adv + InputDisabledOffset) == 0 &&
        (*reinterpret_cast<const uint32_t*>(adv + InputInhibitOffset) & 8) == 0 &&
        *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0 &&
        *reinterpret_cast<const unsigned char*>(adv + 0x84f4) == 6;
}
int ShouldAutoSkip(uintptr_t adv) noexcept {
    if (!enabled || reinterpret_cast<uintptr_t>(InterlockedCompareExchangePointer(&epochAdv, nullptr, nullptr)) != adv || !Eligible(adv)) return 0;
    // Consume before the transition. A player can clear the game's normal
    // fast flag afterward; the wrapper will then forward every later call in
    // this event rather than asserting it again.
    const LONG pending = adv_operations::ConsumeEpoch<true>(epochPending);
    if (!pending) return 0;
    // An already-active skip consumes this epoch but forwards this callback so
    // a player's ordinary input can still disable it.
    if (*reinterpret_cast<const uint32_t*>(adv + AdvFlagsOffset) & 8) return 0;
    setStorySkip(adv, 1);
    if (pending == 1) {
        InterlockedExchange(&epochActivated, 1);
        const LONG event = InterlockedIncrement(&activationCount);
        Log("AdvAutoSkip activation event=%ld", event);
    } else {
        Log("AdvAutoSkip resumption event=%ld", InterlockedCompareExchange(&activationCount, 0, 0));
    }
    return 1;
}
uint32_t __fastcall EventRequest(uintptr_t adv, void*, uint32_t script) noexcept {
    const uint32_t result = originalEventRequest(adv, script);
    // Only a successful normal request begins a new epoch. Failed requests and
    // continuation paths never re-arm an event.
    if (result) {
        InterlockedExchangePointer(&epochAdv, reinterpret_cast<PVOID>(adv));
        InterlockedExchange(&epochActivated, 0);
        InterlockedExchange(&epochPending, 1);
    }
    return result;
}
// 0x409860 and 0x408ed0 both take AdvPart as their cdecl stack argument;
// direct replacement preserves controller and keyboard-independent game state.
int __cdecl StoryInput(uintptr_t adv) noexcept {
    if (ShouldAutoSkip(adv)) return 1;
    return originalStoryInput(adv);
}
void __cdecl MovieClear(uintptr_t adv) noexcept {
    // Only the movie VM command uses this callsite. A manual toggle-off never
    // schedules a resumption, nor does an epoch we did not activate ourselves.
    const bool resume = enabled && adv &&
        reinterpret_cast<uintptr_t>(InterlockedCompareExchangePointer(&epochAdv, nullptr, nullptr)) == adv &&
        InterlockedCompareExchange(&epochActivated, 0, 0) != 0 &&
        *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0 &&
        (*reinterpret_cast<const uint32_t*>(adv + AdvFlagsOffset) & 8) != 0;
    originalStoryClear(adv);
    if (resume) InterlockedCompareExchange(&epochPending, 2, 0);
}
using PreparedSite = owned_patch::PreparedCall;
using owned_patch::IsOriginalExecutable;
using owned_patch::BytesAt;
}

bool InstallRebirth1AdvAutoSkip(const Context& context, const AdvAutoSkipPatchOps* injectedOps) noexcept {
    const AdvAutoSkipPatchOps productionOps{RetargetCalls, RetargetBytes};
    const auto& ops = injectedOps ? *injectedOps : productionOps;
    if (!ops.retargetCalls || !ops.retargetBytes) { Log("AdvAutoSkip unavailable patch operations"); return false; }
    gameBase = reinterpret_cast<uintptr_t>(context.game);
    originalEventRequest = reinterpret_cast<EventRequestFn>(gameBase + EventRequestRva);
    originalStoryInput = reinterpret_cast<StoryInputFn>(gameBase + StoryInputRva);
    setStorySkip = reinterpret_cast<StorySkipFn>(gameBase + StorySkipRva);
    originalStoryClear = reinterpret_cast<StoryClearFn>(gameBase + 0x8be0);
    InterlockedExchangePointer(&epochAdv, nullptr);
    InterlockedExchange(&epochPending, 0);
    InterlockedExchange(&epochActivated, 0);
    InterlockedExchange(&activationCount, 0);
    InterlockedExchange(&enabled, 0);
    std::array<PreparedSite, 6> sites{};
    sites[0].site = {0x00008b29, {0xe8, 0x52, 0x46, 0x00, 0x00}, reinterpret_cast<void*>(&EventRequest)};
    sites[1].site = {0x00008b9c, {0xe8, 0xdf, 0x45, 0x00, 0x00}, reinterpret_cast<void*>(&EventRequest)};
    sites[2].site = {0x0000a1ce, {0xe8, 0x8d, 0xf6, 0xff, 0xff}, reinterpret_cast<void*>(&StoryInput)};
    sites[3].site = {0x0000a206, {0xe8, 0x55, 0xf6, 0xff, 0xff}, reinterpret_cast<void*>(&StoryInput)};
    sites[4].site = {0x0000a2d8, {0xe8, 0x83, 0xf5, 0xff, 0xff}, reinterpret_cast<void*>(&StoryInput)};
    sites[5].site = {0x0001067c, {0xe8, 0x5f, 0x85, 0xff, 0xff}, reinterpret_cast<void*>(&MovieClear)};
    const uintptr_t currentAdv = *reinterpret_cast<uintptr_t*>(gameBase + ActiveAdvPointerRva);
    if (currentAdv && *reinterpret_cast<uint32_t*>(currentAdv + 0x14)) { Log("AdvAutoSkip late install refused"); return false; }
    const PatchResult result = owned_patch::Install(context, sites, ops, "AdvAutoSkip");
    if (!result.Succeeded()) return false;
    InterlockedExchange(&enabled, 1);
    Log("AdvAutoSkip installed six guarded lifecycle calls");
    return true;
}

bool InstallAdvAutoSkip(const Context& context, const AdvAutoSkipPatchOps* injectedOps) noexcept {
    switch (context.spec.id) {
    case GameId::Rebirth1:
        return InstallRebirth1AdvAutoSkip(context, injectedOps);
    case GameId::Rebirth2:
        return InstallRebirth2AdvAutoSkip(context, injectedOps);
    case GameId::SegaHardGirls:
        return InstallSegaHardGirlsAdvAutoSkip(context, injectedOps);
    case GameId::Rebirth3:
        return InstallRebirth3AdvAutoSkip(context, injectedOps);
    }
    Log("AdvAutoSkip unknown target");
    return false;
}

} // namespace rebirths

#ifdef REBIRTHS_TEST_CONTRACTS
namespace rebirths::testing::rebirth1_auto_skip {
void BindEventRequest(EventRequestFn callback) noexcept { rebirths::originalEventRequest = callback; }
void BindStoryInput(StoryInputFn callback) noexcept { rebirths::originalStoryInput = callback; }
void BindSetStorySkip(StorySkipFn callback) noexcept { rebirths::setStorySkip = callback; }
void BindStoryClear(StoryClearFn callback) noexcept { rebirths::originalStoryClear = callback; }
void SetActive(bool active) noexcept { InterlockedExchange(&rebirths::enabled, active ? 1 : 0); }
LONG Active() noexcept { return InterlockedCompareExchange(&rebirths::enabled, 0, 0); }
LONG Activations() noexcept { return InterlockedCompareExchange(&rebirths::activationCount, 0, 0); }
uint32_t __fastcall EventRequest(uintptr_t adv, void* unused1, uint32_t script) noexcept { return rebirths::EventRequest(adv, unused1, script); }
int __cdecl StoryInput(uintptr_t adv) noexcept { return rebirths::StoryInput(adv); }
void __cdecl MovieClear(uintptr_t adv) noexcept { return rebirths::MovieClear(adv); }
}
#endif
