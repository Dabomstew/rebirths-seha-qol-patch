#ifdef REBIRTHS_TEST_CONTRACTS
#include "adv_test_contracts.hpp"
#endif
#include "rebirth2_adv_auto_skip.hpp"

#include "owned_patch_install.hpp"
#include "adv_operations.hpp"

#include <array>
#include <cstring>

namespace rebirths {
namespace rebirth2_auto_skip {
namespace {
using owned_patch::CallTo;

constexpr uint32_t StoryInputRva = 0x00074fe0;
constexpr uint32_t StorySkipRva = 0x00074680;
constexpr uint32_t EventRequestRva = 0x000788f0;
constexpr uint32_t ActiveAdvPointerRva = 0x00443284;
constexpr uint32_t AdvFlagsOffset = 0x48;
constexpr uint32_t AdvModeOffset = 0x10;
constexpr uint32_t InputInhibitOffset = 0x40;
constexpr uint32_t InputDisabledOffset = 0x84fd;

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
uintptr_t epochRequest = 0;
volatile LONG epochPending = 0;
volatile LONG epochActivated = 0;
volatile LONG activationCount = 0;
volatile LONG enabled = 0;

bool Owns(uintptr_t adv) noexcept {
    return adv && *reinterpret_cast<const uintptr_t*>(gameBase + ActiveAdvPointerRva) == adv &&
        reinterpret_cast<uintptr_t>(InterlockedCompareExchangePointer(&epochAdv, nullptr, nullptr)) == adv &&
        epochRequest && *reinterpret_cast<const uintptr_t*>(adv + 0x14) == epochRequest;
}
bool Eligible(uintptr_t adv) noexcept {
    // These are the input function's own mode/inhibition boundaries.  Do not
    // make a pre-load state-1 write or emulate an input device.
    return adv && *reinterpret_cast<const unsigned char*>(adv + InputDisabledOffset) == 0 &&
        (*reinterpret_cast<const uint32_t*>(adv + InputInhibitOffset) & 8) == 0 &&
        *reinterpret_cast<const uint32_t*>(adv + AdvModeOffset) == 0 &&
        *reinterpret_cast<const unsigned char*>(adv + 0x84fc) == 6;
}
int ShouldAutoSkip(uintptr_t adv) noexcept {
    if (!enabled || !Owns(adv) || !Eligible(adv)) return 0;
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
        Log("AdvAutoSkip rebirth2 activation event=%ld", event);
    } else {
        Log("AdvAutoSkip rebirth2 resumption event=%ld", InterlockedCompareExchange(&activationCount, 0, 0));
    }
    return 1;
}
uint32_t __fastcall EventRequest(uintptr_t adv, void*, uint32_t script) noexcept {
    const uint32_t result = originalEventRequest(adv, script);
    // Only a successful normal request begins a new epoch. Failed requests and
    // continuation paths never re-arm an event.
    epochRequest = result ? *reinterpret_cast<const uintptr_t*>(adv + 0x14) : 0;
    InterlockedExchange(&epochPending, 0);
    InterlockedExchange(&epochActivated, 0);
    InterlockedExchangePointer(&epochAdv, nullptr);
    if (result && epochRequest) {
        InterlockedExchangePointer(&epochAdv, reinterpret_cast<PVOID>(adv));
        InterlockedExchange(&epochActivated, 0);
        InterlockedExchange(&epochPending, 1);
    }
    return result;
}
// RB2 0x474fe0 and 0x474680 both take AdvPart as their cdecl stack argument;
// direct replacement preserves controller and keyboard-independent game state.
int __cdecl StoryInput(uintptr_t adv) noexcept {
    if (ShouldAutoSkip(adv)) return 1;
    return originalStoryInput(adv);
}
void __cdecl PresentationClear(uintptr_t adv) noexcept {
    // Only movie/tutorial VM commands use these callsites. A manual toggle-off never
    // schedules a resumption, nor does an epoch we did not activate ourselves.
    const bool resume = enabled && adv &&
        Owns(adv) &&
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

bool Install(const Context& context, const AdvAutoSkipPatchOps* injectedOps) noexcept {
    if (context.spec.id != GameId::Rebirth2) { Log("AdvAutoSkip rebirth2 wrong target"); return false; }
    const AdvAutoSkipPatchOps productionOps{RetargetCalls, RetargetBytes};
    const auto& ops = injectedOps ? *injectedOps : productionOps;
    if (!ops.retargetCalls || !ops.retargetBytes) { Log("AdvAutoSkip rebirth2 unavailable patch operations"); return false; }
    gameBase = reinterpret_cast<uintptr_t>(context.game);
    originalEventRequest = reinterpret_cast<EventRequestFn>(gameBase + EventRequestRva);
    originalStoryInput = reinterpret_cast<StoryInputFn>(gameBase + StoryInputRva);
    setStorySkip = reinterpret_cast<StorySkipFn>(gameBase + StorySkipRva);
    originalStoryClear = reinterpret_cast<StoryClearFn>(gameBase + 0x74390);
    epochRequest = 0;
    InterlockedExchangePointer(&epochAdv, nullptr);
    InterlockedExchange(&epochPending, 0);
    InterlockedExchange(&epochActivated, 0);
    InterlockedExchange(&activationCount, 0);
    InterlockedExchange(&enabled, 0);
    std::array<PreparedSite, 7> sites{};
    sites[0].site = {0x000742d9, {0xe8,0x12,0x46,0x00,0x00}, reinterpret_cast<void*>(&EventRequest)};
    sites[1].site = {0x0007434c, {0xe8,0x9f,0x45,0x00,0x00}, reinterpret_cast<void*>(&EventRequest)};
    sites[2].site = {0x0007594e, {0xe8,0x8d,0xf6,0xff,0xff}, reinterpret_cast<void*>(&StoryInput)};
    sites[3].site = {0x0007598a, {0xe8,0x51,0xf6,0xff,0xff}, reinterpret_cast<void*>(&StoryInput)};
    sites[4].site = {0x00075a48, {0xe8,0x93,0xf5,0xff,0xff}, reinterpret_cast<void*>(&StoryInput)};
    sites[5].site = {0x0007bf7c, {0xe8,0x0f,0x84,0xff,0xff}, reinterpret_cast<void*>(&PresentationClear)};
    sites[6].site = {0x7f69c, CallTo(0x7f69c, 0x74390), reinterpret_cast<void*>(&PresentationClear)};
    const uintptr_t currentAdv = *reinterpret_cast<uintptr_t*>(gameBase + ActiveAdvPointerRva);
    if (currentAdv && *reinterpret_cast<uint32_t*>(currentAdv + 0x14)) { Log("AdvAutoSkip rebirth2 late install refused"); return false; }
    const PatchResult result = owned_patch::Install(context, sites, ops, "AdvAutoSkip rebirth2");
    if (!result.Succeeded()) return false;
    InterlockedExchange(&enabled, 1);
    Log("AdvAutoSkip rebirth2 installed seven guarded lifecycle calls");
    return true;
}

} // namespace rebirth2_auto_skip

bool InstallRebirth2AdvAutoSkip(const Context& context, const AdvAutoSkipPatchOps* ops) noexcept {
    return rebirth2_auto_skip::Install(context, ops);
}
} // namespace rebirths

#ifdef REBIRTHS_TEST_CONTRACTS
namespace rebirths::testing::rebirth2_auto_skip {
void SetActive(bool active) noexcept { InterlockedExchange(&rebirths::rebirth2_auto_skip::enabled, active ? 1 : 0); }
void BindEventRequest(EventRequestFn callback) noexcept { rebirths::rebirth2_auto_skip::originalEventRequest = callback; }
void BindStoryInput(StoryInputFn callback) noexcept { rebirths::rebirth2_auto_skip::originalStoryInput = callback; }
void BindSetStorySkip(StorySkipFn callback) noexcept { rebirths::rebirth2_auto_skip::setStorySkip = callback; }
void BindStoryClear(StoryClearFn callback) noexcept { rebirths::rebirth2_auto_skip::originalStoryClear = callback; }
LONG Active() noexcept { return InterlockedCompareExchange(&rebirths::rebirth2_auto_skip::enabled, 0, 0); }
LONG Activations() noexcept { return InterlockedCompareExchange(&rebirths::rebirth2_auto_skip::activationCount, 0, 0); }
uint32_t __fastcall EventRequest(uintptr_t adv, void* unused1, uint32_t script) noexcept { return rebirths::rebirth2_auto_skip::EventRequest(adv, unused1, script); }
int __cdecl StoryInput(uintptr_t adv) noexcept { return rebirths::rebirth2_auto_skip::StoryInput(adv); }
void __cdecl PresentationClear(uintptr_t adv) noexcept { return rebirths::rebirth2_auto_skip::PresentationClear(adv); }
auto HookPresentationClear() noexcept -> decltype(&PresentationClear) { return &rebirths::rebirth2_auto_skip::PresentationClear; }
}
#endif
