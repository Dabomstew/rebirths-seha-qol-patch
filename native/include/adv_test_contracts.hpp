#pragma once
#ifndef REBIRTHS_TEST_CONTRACTS
#error "ADV fixture contracts are available only in test builds"
#endif
#include "rebirth1_adv_auto_skip.hpp"
#include "rebirth2_adv_auto_skip.hpp"
#include "rebirth3_adv_auto_skip.hpp"
#include "sega_adv_auto_skip.hpp"
#include "rebirth1_adv_fast_forward.hpp"
#include "rebirth2_adv_fast_forward.hpp"
#include "rebirth3_adv_fast_forward.hpp"
#include "sega_adv_fast_forward.hpp"
#include "adv_blackout.hpp"

// Typed fixture boundary. No mutable production state or implementation includes.
// Game-specific ABI differences are deliberately retained. Shipping builds omit
// all these adapters; test builds compile the same production translation units.

namespace rebirths::testing::rebirth1_auto_skip {
using EventRequestFn = uint32_t(__thiscall*)(uintptr_t, uint32_t);
using StoryInputFn = int(__cdecl*)(uintptr_t);
using StorySkipFn = void(__cdecl*)(uintptr_t, uint32_t);
using StoryClearFn = void(__cdecl*)(uintptr_t);
void BindEventRequest(EventRequestFn callback) noexcept;
void BindStoryInput(StoryInputFn callback) noexcept;
void BindSetStorySkip(StorySkipFn callback) noexcept;
void BindStoryClear(StoryClearFn callback) noexcept;
void SetActive(bool active) noexcept;
LONG Active() noexcept;
LONG Activations() noexcept;
uint32_t __fastcall EventRequest(uintptr_t adv, void* unused1, uint32_t script) noexcept;
int __cdecl StoryInput(uintptr_t adv) noexcept;
void __cdecl MovieClear(uintptr_t adv) noexcept;
} // namespace rebirths::testing::rebirth1_auto_skip

namespace rebirths::testing::rebirth1_fast_forward {
using DelayFn = int(__cdecl*)(int*, float*, float);
using BackgroundLoadFn = int(__cdecl*)(uint32_t*);
using TrackFn = void(__cdecl*)(float*);
using ScaledTrackFn = void(__cdecl*)(float*, float);
using SetupFn = void(__cdecl*)(uint32_t, uint32_t, int, int, uint32_t);
using LookupFn = uint8_t(__thiscall*)(int*, uint32_t, int*, int*);
using ContextFn = uintptr_t(__cdecl*)();
using ManagerFn = int*(__cdecl*)();
using ResolveFn = int*(__stdcall*)(int);
using AdvBinCreateFn = void(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t);
using AdvSePollFn = uint8_t(__thiscall*)(uintptr_t);
using CgMotionPollFn = uint32_t(__fastcall*)(uintptr_t);
using AggregateFn = int(__cdecl*)(uint32_t, int, uint32_t*);
using CgReadyFn = int(__cdecl*)(uintptr_t, float);
using TalkCleanupFn = int(__cdecl*)(int*, int, int, int, uint32_t, uint32_t);
using CharacterReadyFn = int(__cdecl*)(uint32_t, int, int, float);
using CharacterExitFn = int(__cdecl*)(int*, uint32_t, int, float);
void BindDelay(DelayFn callback) noexcept;
void BindBackgroundLoad(BackgroundLoadFn callback) noexcept;
void BindTrack(TrackFn callback) noexcept;
void BindScaledTrack(ScaledTrackFn callback) noexcept;
void BindSetup(SetupFn callback) noexcept;
void BindLookupCharacter(LookupFn callback) noexcept;
void BindBattleContext(ContextFn callback) noexcept;
void BindCharacterManager(ManagerFn callback) noexcept;
void BindResolveCharacterList(ResolveFn callback) noexcept;
void BindAdvBinCreate(AdvBinCreateFn callback) noexcept;
void BindAdvSePoll(AdvSePollFn callback) noexcept;
void BindCgMotionPoll(CgMotionPollFn callback) noexcept;
void BindAggregate(AggregateFn callback) noexcept;
void BindCgReady(CgReadyFn callback) noexcept;
void BindTalkCleanup(TalkCleanupFn callback) noexcept;
void BindCharacterReady(CharacterReadyFn callback) noexcept;
void BindCharacterExit(CharacterExitFn callback) noexcept;
void SetActive(bool active) noexcept;
LONG Active() noexcept;
int __cdecl Aggregate(uint32_t mask, int wait, uint32_t* output) noexcept;
int __cdecl CgReady(uintptr_t object, float duration) noexcept;
int __cdecl CharacterReady(uint32_t id, int arg, int expression, float duration) noexcept;
int __cdecl CharacterExit(int* state, uint32_t id, int arg, float duration) noexcept;
int __cdecl TalkCleanup(int* state, int operation, int arg3, int arg4, uint32_t arg5,
                        uint32_t arg6) noexcept;
int* __cdecl CgCommandManager() noexcept;
int __cdecl Delay(int* state, float* record, float duration) noexcept;
int __cdecl BackgroundLoad(uint32_t* record) noexcept;
uint32_t __fastcall CgMotionPoll(uintptr_t object) noexcept;
void __cdecl FrameTrack(float* record) noexcept;
void __cdecl CharacterSetup(uint32_t mode, uint32_t character, int arg3, int arg4,
                            uint32_t arg5) noexcept;
void __cdecl AdvBinCreate(uint32_t manager, uint32_t arg3, uint32_t arg4,
                          uint32_t modeFlag) noexcept;
uint8_t __fastcall AdvSePoll(uintptr_t context, void* unused1) noexcept;
} // namespace rebirths::testing::rebirth1_fast_forward

namespace rebirths::testing::rebirth2_auto_skip {
void SetActive(bool active) noexcept;
using EventRequestFn = uint32_t(__thiscall*)(uintptr_t, uint32_t);
using StoryInputFn = int(__cdecl*)(uintptr_t);
using StorySkipFn = void(__cdecl*)(uintptr_t, uint32_t);
using StoryClearFn = void(__cdecl*)(uintptr_t);
void BindEventRequest(EventRequestFn callback) noexcept;
void BindStoryInput(StoryInputFn callback) noexcept;
void BindSetStorySkip(StorySkipFn callback) noexcept;
void BindStoryClear(StoryClearFn callback) noexcept;
LONG Active() noexcept;
LONG Activations() noexcept;
uint32_t __fastcall EventRequest(uintptr_t adv, void* unused1, uint32_t script) noexcept;
int __cdecl StoryInput(uintptr_t adv) noexcept;
void __cdecl PresentationClear(uintptr_t adv) noexcept;
auto HookPresentationClear() noexcept -> decltype(&PresentationClear);
} // namespace rebirths::testing::rebirth2_auto_skip

namespace rebirths::testing::rebirth2_fast_forward {
using DelayTrackHook = void(__cdecl*)(float*, float) noexcept;
DelayTrackHook HookDelayTrack() noexcept;
using ChapterUpdateFn = int(__cdecl*)(uint32_t*);
using TelopVisualCreateFn = uint8_t(__thiscall*)(uint32_t*, uint32_t, uint32_t);
using ScriptParameterFn = const int32_t*(__cdecl*)(uintptr_t, uint32_t);
using TelopPayloadFn = uintptr_t(__cdecl*)();
using ChapterFinishFn = void(__cdecl*)();
using TrackGroupFn = void(__thiscall*)(uintptr_t, float);
using AdvBinCreateFn = void(__cdecl*)(uintptr_t, uint32_t, uint32_t, uint32_t);
using SetupFn = void(__cdecl*)(uint32_t, uint32_t, int, int, uint32_t);
using ResolveFn = uintptr_t(__stdcall*)(uintptr_t);
using LookupFn = uint8_t(__thiscall*)(uintptr_t, uint32_t, uintptr_t*, uintptr_t*);
using ScaledTrackFn = void(__cdecl*)(float*, float);
using BackgroundLoadFn = uint8_t(__cdecl*)(uint32_t*);
using AdvSePollFn = uint8_t(__thiscall*)(uintptr_t);
using SoundHandlePollFn = uint8_t(__cdecl*)(uintptr_t);
using CgMotionPollFn = uint32_t(__thiscall*)(uintptr_t);
using AdvSoundUpdateFn = uint8_t(__thiscall*)(uint32_t*);
using ScriptSoundPlayFn = uintptr_t(__cdecl*)(uintptr_t, uint32_t, uint32_t, int);
using TalkReadyFn = uint8_t(__cdecl*)(uintptr_t);
using DelayFn = int(__cdecl*)(int*, float*, float);
using AggregateFn = int(__cdecl*)(uint32_t, int, uint32_t*);
using TalkCleanupFn = int(__cdecl*)(int*, int, int, int, uint32_t, uint32_t);
using CharacterCommandFn = int(__cdecl*)(uint32_t, int, int, float);
using ManagerFn = uintptr_t(__cdecl*)();
using CgReadyFn = int(__cdecl*)(uintptr_t, float);
using CharacterExitFn = int(__cdecl*)(int*, uint32_t, int, float);
void BindChapterUpdate(ChapterUpdateFn callback) noexcept;
void BindTelopVisualCreate(TelopVisualCreateFn callback) noexcept;
void BindScriptParameter(ScriptParameterFn callback) noexcept;
void BindTelopPayload(TelopPayloadFn callback) noexcept;
void BindChapterFinish(ChapterFinishFn callback) noexcept;
void BindTrackGroup(TrackGroupFn callback) noexcept;
void BindAdvBinCreate(AdvBinCreateFn callback) noexcept;
void BindSetup(SetupFn callback) noexcept;
void BindResolveTask(ResolveFn callback) noexcept;
void BindLookupCharacter(LookupFn callback) noexcept;
void BindScaledTrack(ScaledTrackFn callback) noexcept;
ScaledTrackFn NativeScaledTrack() noexcept;
void BindBackgroundLoad(BackgroundLoadFn callback) noexcept;
void BindAdvSePoll(AdvSePollFn callback) noexcept;
void BindSoundHandlePoll(SoundHandlePollFn callback) noexcept;
void BindCgMotionPoll(CgMotionPollFn callback) noexcept;
void BindAdvSoundUpdate(AdvSoundUpdateFn callback) noexcept;
void BindScriptSoundPlay(ScriptSoundPlayFn callback) noexcept;
void BindTalkReady(TalkReadyFn callback) noexcept;
void BindDelay(DelayFn callback) noexcept;
void BindAggregate(AggregateFn callback) noexcept;
void BindTalkCleanup(TalkCleanupFn callback) noexcept;
void BindCharacterCommand(CharacterCommandFn callback) noexcept;
void BindManager(ManagerFn callback) noexcept;
void BindCgReady(CgReadyFn callback) noexcept;
void BindCharacterExit(CharacterExitFn callback) noexcept;
void SetActive(bool active) noexcept;
LONG Active() noexcept;
void SetChapterActive(bool active) noexcept;
LONG ChapterActive() noexcept;
const int32_t* __cdecl TelopParameter(uintptr_t vm, uint32_t index) noexcept;
const int32_t* __cdecl TelopWaitParameter(uintptr_t vm, uint32_t index) noexcept;
uint8_t __fastcall TelopVisualCreate(uint32_t* record, void* unused1, uint32_t banks,
                                     uint32_t textures) noexcept;
void __cdecl ChapterFinish() noexcept;
uintptr_t ChapterWaitOwner() noexcept;
int __cdecl ChapterUpdate(uint32_t* record) noexcept;
uintptr_t __cdecl ScriptSoundPlay(uintptr_t resource, uint32_t cue, uint32_t group,
                                  int priority) noexcept;
uint8_t __fastcall AdvSoundUpdate(uint32_t* record, void* unused1) noexcept;
uintptr_t ActiveAdv() noexcept;
void __cdecl BackgroundTrack(float* record, float scale) noexcept;
void __cdecl FadeTrack(float* record, float scale) noexcept;
void __cdecl DelayTrack(float* record, float scale) noexcept;
uint8_t __cdecl BackgroundLoad(uint32_t* record) noexcept;
uint8_t __fastcall AdvSePoll(uintptr_t context, void* unused1) noexcept;
uint8_t __cdecl TalkReady(uintptr_t talk) noexcept;
void __cdecl CharacterSetup(uint32_t mode, uint32_t character, int arg3, int arg4,
                            uint32_t arg5) noexcept;
void __cdecl AdvBinCreate(uintptr_t manager, uint32_t operation, uint32_t entry,
                          uint32_t modeFlag) noexcept;
void __fastcall TrackGroup(uintptr_t record, void* unused1, float scale) noexcept;
int __cdecl Delay(int* state, float* record, float duration) noexcept;
int __cdecl Aggregate(uint32_t mask, int wait, uint32_t* output) noexcept;
int __cdecl TalkCleanup(int* state, int operation, int arg3, int arg4, uint32_t arg5,
                        uint32_t arg6) noexcept;
int __cdecl CharacterCommand(uint32_t id, int arg, int expression, float duration) noexcept;
uintptr_t __cdecl CgCommandManager() noexcept;
int __cdecl CgReady(uintptr_t object, float duration) noexcept;
int __cdecl CharacterExit(int* state, uint32_t id, int arg, float duration) noexcept;
bool BlackoutActive() noexcept;
uint8_t __cdecl SoundHandlePoll(uintptr_t sound) noexcept;
uint32_t __fastcall CgMotionPoll(uintptr_t object, void* unused1) noexcept;
uintptr_t InfoHandle() noexcept;
} // namespace rebirths::testing::rebirth2_fast_forward

namespace rebirths::testing::rebirth3_auto_skip {
using EventRequestFn = uint32_t(__thiscall*)(uintptr_t, uint32_t);
using StoryInputFn = int(__cdecl*)(uintptr_t);
using StorySkipFn = void(__cdecl*)(uintptr_t, uint32_t);
using InEngineRequestFn = int(__cdecl*)(uintptr_t, uint32_t);
using PromptResultFn = int(__thiscall*)(uintptr_t, uint32_t);
using PromptReadyFn = unsigned char(__cdecl*)(uintptr_t);
using PromptCloseFn = uintptr_t(__cdecl*)(uintptr_t);
using UiSoundFn = void(__cdecl*)(uint32_t);
using StoryClearFn = void(__cdecl*)(uintptr_t);
using NepSkipFn = int(__thiscall*)(uintptr_t);
using InputQueryFn = int(__thiscall*)(uintptr_t, uint32_t);
using NepCleanupFn = void(__thiscall*)(uintptr_t);
void ResetOptions() noexcept;
void BindNepDialogue(NepSkipFn callback) noexcept;
void BindNepCleanup(NepCleanupFn callback) noexcept;
int __fastcall NepDialogue(uintptr_t owner, void* unused) noexcept;
int __fastcall NepDialogueInput(uintptr_t input, void* unused, uint32_t mask) noexcept;
void __fastcall NepCleanup(uintptr_t owner, void* unused) noexcept;
void BindStoryClear(StoryClearFn callback) noexcept;
void BindNepSkip(NepSkipFn callback) noexcept;
void BindNepInput(NepSkipFn callback) noexcept;
int __fastcall NepStoryInput(uintptr_t owner, void* unused) noexcept;
void BindInputQuery(InputQueryFn callback) noexcept;
void __cdecl TutorialClear(uintptr_t adv) noexcept;
int __fastcall NepSkip(uintptr_t owner, void* unused) noexcept;
int __fastcall NepSkipInput(uintptr_t input, void* unused, uint32_t mask) noexcept;
int __fastcall NepPromptResult(uintptr_t slot, void* unused, uint32_t sound) noexcept;
void BindEventRequest(EventRequestFn callback) noexcept;
void BindStoryInput(StoryInputFn callback) noexcept;
void BindSetStorySkip(StorySkipFn callback) noexcept;
void BindRequestInEngineSkip(InEngineRequestFn callback) noexcept;
void BindPromptResult(PromptResultFn callback) noexcept;
void BindPromptReady(PromptReadyFn callback) noexcept;
void BindClosePrompt(PromptCloseFn callback) noexcept;
void BindPlayUiSound(UiSoundFn callback) noexcept;
uint32_t __fastcall EventRequest(uintptr_t adv, void* unused1, uint32_t script) noexcept;
int __cdecl StoryInput(uintptr_t adv) noexcept;
int __fastcall InEnginePromptResult(uintptr_t prompt, void* unused1, uint32_t sound) noexcept;
} // namespace rebirths::testing::rebirth3_auto_skip

namespace rebirths::testing::rebirth3_fast_forward {
using DelayTrackHook = void(__cdecl*)(float*, float) noexcept;
DelayTrackHook HookDelayTrack() noexcept;
using TrackGroupFn = void(__thiscall*)(uintptr_t, float);
using AdvBinCreateFn = void(__cdecl*)(uintptr_t, uint32_t, uint32_t, uint32_t);
using SetupFn = void(__cdecl*)(uint32_t, uint32_t, int, int, uint32_t);
using ResolveFn = uintptr_t(__stdcall*)(uintptr_t);
using LookupFn = uint8_t(__thiscall*)(uintptr_t, uint32_t, uintptr_t*, uintptr_t*);
using ScaledTrackFn = void(__cdecl*)(float*, float);
using BackgroundLoadFn = int(__cdecl*)(uint32_t*);
using AdvSePollFn = uint8_t(__thiscall*)(uintptr_t);
using TalkReadyFn = uint8_t(__cdecl*)(uintptr_t);
using CharacterReadyFn = uint32_t(__cdecl*)(uintptr_t);
using DelayFn = int(__cdecl*)(int*, float*, float);
using AggregateFn = int(__cdecl*)(uint32_t, int, uint32_t*);
using TalkCleanupFn = int(__cdecl*)(int*, int, int, int, uint32_t, uint32_t);
using CharacterCommandFn = int(__cdecl*)(uint32_t, int, int, float);
using ManagerFn = uintptr_t(__cdecl*)();
using CgReadyFn = int(__cdecl*)(uintptr_t, float);
using CharacterExitFn = int(__cdecl*)(int*, uint32_t, int, float);
void BindTrackGroup(TrackGroupFn callback) noexcept;
void BindAdvBinCreate(AdvBinCreateFn callback) noexcept;
void BindSetup(SetupFn callback) noexcept;
void BindResolveTask(ResolveFn callback) noexcept;
void BindLookupCharacter(LookupFn callback) noexcept;
void BindScaledTrack(ScaledTrackFn callback) noexcept;
ScaledTrackFn NativeScaledTrack() noexcept;
void BindBackgroundLoad(BackgroundLoadFn callback) noexcept;
void BindAdvSePoll(AdvSePollFn callback) noexcept;
void BindTalkReady(TalkReadyFn callback) noexcept;
void BindCharacterReady(CharacterReadyFn callback) noexcept;
void BindCharacterUpdate(CharacterReadyFn callback) noexcept;
void BindDelay(DelayFn callback) noexcept;
void BindAggregate(AggregateFn callback) noexcept;
void BindTalkCleanup(TalkCleanupFn callback) noexcept;
void BindCharacterCommand(CharacterCommandFn callback) noexcept;
void BindManager(ManagerFn callback) noexcept;
void BindCgReady(CgReadyFn callback) noexcept;
void BindCharacterExit(CharacterExitFn callback) noexcept;
void SetActive(bool active) noexcept;
LONG Active() noexcept;
uintptr_t ActiveAdv() noexcept;
void __cdecl BackgroundTrack(float* record, float scale) noexcept;
void __cdecl FadeTrack(float* record, float scale) noexcept;
void __cdecl DelayTrack(float* record, float scale) noexcept;
int __cdecl BackgroundLoad(uint32_t* record) noexcept;
uint8_t __fastcall AdvSePoll(uintptr_t context, void* unused1) noexcept;
uint8_t __cdecl TalkReady(uintptr_t talk) noexcept;
uint32_t __cdecl CharacterReady(uintptr_t character) noexcept;
uint32_t __cdecl CharacterUpdate(uintptr_t character) noexcept;
void __cdecl CharacterSetup(uint32_t mode, uint32_t character, int arg3, int arg4,
                            uint32_t arg5) noexcept;
void __cdecl AdvBinCreate(uintptr_t manager, uint32_t operation, uint32_t entry,
                          uint32_t modeFlag) noexcept;
void __fastcall TrackGroup(uintptr_t record, void* unused1, float scale) noexcept;
uintptr_t InfoHandle() noexcept;
int __cdecl Delay(int* state, float* record, float duration) noexcept;
int __cdecl Aggregate(uint32_t mask, int wait, uint32_t* output) noexcept;
int __cdecl TalkCleanup(int* state, int operation, int arg3, int arg4, uint32_t arg5,
                        uint32_t arg6) noexcept;
int __cdecl CharacterCommand(uint32_t id, int arg, int expression, float duration) noexcept;
uintptr_t __cdecl CgCommandManager() noexcept;
int __cdecl CgReady(uintptr_t object, float duration) noexcept;
int __cdecl CharacterExit(int* state, uint32_t id, int arg, float duration) noexcept;
} // namespace rebirths::testing::rebirth3_fast_forward

namespace rebirths::testing::sega_auto_skip {
void SetActive(bool active) noexcept;
using EventRequestFn = uint32_t(__thiscall*)(uintptr_t, uint32_t);
using StoryInputFn = int(__cdecl*)(uintptr_t);
using StorySkipFn = void(__cdecl*)(uintptr_t, uint32_t);
using StoryClearFn = void(__cdecl*)(uintptr_t);
void BindEventRequest(EventRequestFn callback) noexcept;
void BindStoryInput(StoryInputFn callback) noexcept;
void BindSetStorySkip(StorySkipFn callback) noexcept;
void BindStoryClear(StoryClearFn callback) noexcept;
LONG Active() noexcept;
LONG Activations() noexcept;
uint32_t __fastcall EventRequest(uintptr_t adv, void* unused1, uint32_t script) noexcept;
int __cdecl StoryInput(uintptr_t adv) noexcept;
void __cdecl PresentationClear(uintptr_t adv) noexcept;
} // namespace rebirths::testing::sega_auto_skip

namespace rebirths::testing::sega_fast_forward {
using DelayTrackHook = void(__cdecl*)(float*, float) noexcept;
DelayTrackHook HookDelayTrack() noexcept;
using FrameTrackFn = void(__cdecl*)(float*);
using AdvBinCreateFn = void(__cdecl*)(uintptr_t, uint32_t, uint32_t, uint32_t);
using SetupFn = void(__cdecl*)(uint32_t, uint32_t, int, int, uint32_t);
using ResolveFn = uintptr_t(__stdcall*)(uintptr_t);
using LookupFn = uint8_t(__thiscall*)(uintptr_t, uint32_t, uintptr_t*, uintptr_t*);
using ScaledTrackFn = void(__cdecl*)(float*, float);
using BackgroundLoadFn = uint8_t(__cdecl*)(uint32_t*);
using AdvSePollFn = uint8_t(__thiscall*)(uintptr_t);
using DelayFn = int(__cdecl*)(int*, float*, float);
using AggregateFn = int(__cdecl*)(uint32_t, int, uint32_t*);
using TalkCleanupFn = int(__cdecl*)(int*, int, int, int, uint32_t, uint32_t);
using CharacterCommandFn = int(__cdecl*)(uint32_t, int, int, float);
using ManagerFn = uintptr_t(__cdecl*)();
using CgReadyFn = int(__cdecl*)(uintptr_t, float);
using CharacterExitFn = int(__cdecl*)(int*, uint32_t, int, float);
using CharacterAnimationFn = int(__cdecl*)(int*, uint32_t, int);
using StoryMovieFn = int(__thiscall*)(uintptr_t);
void BindFrameTrack(FrameTrackFn callback) noexcept;
void BindAdvBinCreate(AdvBinCreateFn callback) noexcept;
void BindSetup(SetupFn callback) noexcept;
void BindResolveTask(ResolveFn callback) noexcept;
void BindLookupCharacter(LookupFn callback) noexcept;
void BindScaledTrack(ScaledTrackFn callback) noexcept;
ScaledTrackFn NativeScaledTrack() noexcept;
void BindBackgroundLoad(BackgroundLoadFn callback) noexcept;
void BindAdvSePoll(AdvSePollFn callback) noexcept;
void BindDelay(DelayFn callback) noexcept;
void BindAggregate(AggregateFn callback) noexcept;
void BindTalkCleanup(TalkCleanupFn callback) noexcept;
void BindCharacterCommand(CharacterCommandFn callback) noexcept;
void BindManager(ManagerFn callback) noexcept;
void BindCgReady(CgReadyFn callback) noexcept;
void BindCharacterExit(CharacterExitFn callback) noexcept;
void BindCharacterAnimation(CharacterAnimationFn callback) noexcept;
void BindStoryMovie(StoryMovieFn callback) noexcept;
void SetActive(bool active) noexcept;
LONG Active() noexcept;
uintptr_t ActiveAdv() noexcept;
void __cdecl BackgroundTrack(float* record, float scale) noexcept;
void __cdecl FadeTrack(float* record, float scale) noexcept;
void __cdecl DelayTrack(float* record, float scale) noexcept;
uint8_t __cdecl BackgroundLoad(uint32_t* record) noexcept;
uint8_t __fastcall AdvSePoll(uintptr_t context, void* unused1) noexcept;
void __cdecl CharacterSetup(uint32_t mode, uint32_t character, int arg3, int arg4,
                            uint32_t arg5) noexcept;
void __cdecl AdvBinCreate(uintptr_t manager, uint32_t operation, uint32_t entry,
                          uint32_t modeFlag) noexcept;
void __cdecl FrameTrack(float* record) noexcept;
uintptr_t InfoHandle() noexcept;
int __cdecl Delay(int* state, float* record, float duration) noexcept;
int __cdecl Aggregate(uint32_t mask, int wait, uint32_t* output) noexcept;
int __cdecl TalkCleanup(int* state, int operation, int arg3, int arg4, uint32_t arg5,
                        uint32_t arg6) noexcept;
int __cdecl CharacterCommand(uint32_t id, int arg, int expression, float duration) noexcept;
uintptr_t __cdecl CgCommandManager() noexcept;
int __cdecl CgReady(uintptr_t object, float duration) noexcept;
int __cdecl CharacterExit(int* state, uint32_t id, int arg, float duration) noexcept;
int __cdecl CharacterAnimation(int* state, uint32_t id, int animation) noexcept;
int __fastcall StoryMovie(uintptr_t owner, void* unused1) noexcept;
} // namespace rebirths::testing::sega_fast_forward
