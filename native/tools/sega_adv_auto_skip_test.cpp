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
unsigned failOperation = 0;
unsigned requestCalls = 0, inputCalls = 0, skipCalls = 0, operations = 0;
bool requestSuccess = true, failAfterWrite = false, leaveWritableOnRollback = false;

uint32_t __fastcall OriginalRequest(uintptr_t adv, void*, uint32_t script) {
    ++requestCalls;
    assert(adv == reinterpret_cast<uintptr_t>(fakeBase + 0x500000));
    assert(script == 0x321);
    *reinterpret_cast<uintptr_t*>(adv + 0x14) = requestSuccess ? 0x1234 : 0;
    return requestSuccess ? 1 : 0;
}
int __cdecl OriginalInput(uintptr_t adv) { ++inputCalls; assert(adv == reinterpret_cast<uintptr_t>(fakeBase + 0x500000)); return 42; }
void __cdecl OriginalSkip(uintptr_t adv, uint32_t value) {
    ++skipCalls; assert(value == 1 && adv == reinterpret_cast<uintptr_t>(fakeBase + 0x500000));
    *reinterpret_cast<uint32_t*>(adv + 0x48) |= 8;
}
unsigned clearCalls = 0;
void __cdecl OriginalClear(uintptr_t adv) {
    ++clearCalls;
    *reinterpret_cast<uint32_t*>(adv + 0x48) &= ~0x1eu;
}

bool SetPage(void* address, DWORD protection) { DWORD old = 0; return VirtualProtect(address, 5, protection, &old) != FALSE; }
bool SetRange(void* address, size_t length, DWORD protection) { return fixture::Protect(address, length, protection); }
bool Mutate(uint32_t rva, const unsigned char* expected, const unsigned char* replacement, size_t count) {
    auto* at = fakeBase + rva;
    if (std::memcmp(at, expected, count) || !SetPage(at, PAGE_EXECUTE_READWRITE)) return false;
    std::memcpy(at, replacement, count);
    return SetPage(at, PAGE_EXECUTE_READ);
}
bool FakeRetargetCalls(const rebirths::Context&, const rebirths::CallSite* sites, size_t count) noexcept {
    assert(count == 1); ++operations;
    const auto replacement = fixture::CallBytes(reinterpret_cast<uintptr_t>(fakeBase + sites[0].rva), reinterpret_cast<uintptr_t>(sites[0].replacement));
    const bool wrote = Mutate(sites[0].rva, sites[0].expected.data(), replacement.data(), replacement.size());
    return wrote && !failAfterWrite && operations != failOperation;
}
bool FakeRetargetBytes(const rebirths::Context&, uint32_t rva, const unsigned char* expected,
                       const unsigned char* replacement, size_t count) noexcept {
    if (!Mutate(rva, expected, replacement, count)) return false;
    if (leaveWritableOnRollback) return SetPage(fakeBase + rva, PAGE_EXECUTE_READWRITE);
    return true;
}
const rebirths::AdvAutoSkipPatchOps FakeOps{FakeRetargetCalls, FakeRetargetBytes};
void Populate() {
    DWORD old = 0; assert(VirtualProtect(fakeBase, ImageSize, PAGE_EXECUTE_READWRITE, &old));
    std::memset(fakeBase, 0, ImageSize);
    const std::pair<uint32_t, uint32_t> sites[] = {
        {0x8cd1d, 0x80700}, {0x80649, 0x84e50}, {0x806bc, 0x84e50}, {0x81e0e, 0x81460}, {0x81e46, 0x81460}, {0x81f18, 0x81460},
    };
    for (const auto [source, target] : sites) { const auto call = fixture::CallBytes(source, target); std::memcpy(fakeBase + source, call.data(), call.size()); }
    assert(VirtualProtect(fakeBase, ImageSize, PAGE_EXECUTE_READ, &old));
    failOperation = 0;
    operations = requestCalls = inputCalls = skipCalls = 0; requestSuccess = true; failAfterWrite = leaveWritableOnRollback = false; capturedLog.clear();
}
void Bind() {
    rebirths::testing::sega_auto_skip::BindEventRequest(reinterpret_cast<rebirths::testing::sega_auto_skip::EventRequestFn>(OriginalRequest));
    rebirths::testing::sega_auto_skip::BindStoryInput(OriginalInput);
    rebirths::testing::sega_auto_skip::BindSetStorySkip(OriginalSkip);
    rebirths::testing::sega_auto_skip::BindStoryClear(OriginalClear);
}
void PrepareAdv(uintptr_t adv) {
    assert(SetRange(reinterpret_cast<void*>(adv), 0x8520, PAGE_READWRITE));
    assert(SetRange(fakeBase + 0x437b44, 4, PAGE_READWRITE));
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x437b44) = adv;
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 0;
    *reinterpret_cast<uint32_t*>(adv + 0x40) = 0;
    *reinterpret_cast<uint32_t*>(adv + 0x48) = 0;
    *reinterpret_cast<unsigned char*>(adv + 0x850c) = 6;
    *reinterpret_cast<unsigned char*>(adv + 0x850d) = 0;
}
}

int main() {
    fixture::Image image(ImageSize); fakeBase = image.data();
    const rebirths::Context context{reinterpret_cast<HMODULE>(fakeBase), rebirths::GameSpecFor(rebirths::GameId::SegaHardGirls), L"", L""};
    const uintptr_t adv = reinterpret_cast<uintptr_t>(fakeBase + 0x500000);

    Populate(); assert(rebirths::InstallSegaHardGirlsAdvAutoSkip(context, &FakeOps)); assert(operations == 6); Bind(); PrepareAdv(adv);
    // The thiscall request forwards its ECX object and RET 4 script argument;
    // only a nonzero original return arms an epoch.
    assert(rebirths::testing::sega_auto_skip::EventRequest(adv, nullptr, 0x321) == 1 && requestCalls == 1);
    assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 1 && skipCalls == 1 && inputCalls == 0);
    assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 42 && skipCalls == 1 && inputCalls == 1);
    // Manual normal input clears the fast bit, and the consumed epoch never
    // asserts it again. Repeated script IDs create fresh successful epochs.
    *reinterpret_cast<uint32_t*>(adv + 0x48) &= ~8u;
    assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 42 && skipCalls == 1 && inputCalls == 2);
    assert(rebirths::testing::sega_auto_skip::EventRequest(adv, nullptr, 0x321) == 1);
    assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 1 && skipCalls == 2);

    PrepareAdv(adv); requestSuccess = false;
    assert(rebirths::testing::sega_auto_skip::EventRequest(adv, nullptr, 0x321) == 0 && rebirths::testing::sega_auto_skip::StoryInput(adv) == 42 && skipCalls == 2);
    requestSuccess = true;
    // Eligibility is deferred, never consumed, until the normal state reaches
    // the same mode/state/input boundary as the original consumer.
    assert(rebirths::testing::sega_auto_skip::EventRequest(adv, nullptr, 0x321));
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 1; assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 42);
    *reinterpret_cast<uint32_t*>(adv + 0x10) = 0; *reinterpret_cast<unsigned char*>(adv + 0x850c) = 5; assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 42);
    *reinterpret_cast<unsigned char*>(adv + 0x850c) = 6; *reinterpret_cast<uint32_t*>(adv + 0x40) = 8; assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 42);
    *reinterpret_cast<uint32_t*>(adv + 0x40) = 0; assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 1 && skipCalls == 3);
    // An event that is already skipping consumes its chance but forwards the
    // first callback, so the player's regular toggle-off action still works.
    PrepareAdv(adv); assert(rebirths::testing::sega_auto_skip::EventRequest(adv, nullptr, 0x321)); *reinterpret_cast<uint32_t*>(adv + 0x48) = 8;
    assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 42 && inputCalls >= 6 && skipCalls == 3);

    // New Game: a later movie starts after this request's auto activation.
    // Its scripted clear must retain the native inhibition until completion,
    // then resume exactly once without creating another request activation.
    PrepareAdv(adv); assert(rebirths::testing::sega_auto_skip::EventRequest(adv, nullptr, 0x321));
    assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 1);
    const unsigned beforeMovie = skipCalls;
    const LONG beforeEvents = rebirths::testing::sega_auto_skip::Activations();
    *reinterpret_cast<uint32_t*>(adv + 0x40) = 15;
    rebirths::testing::sega_auto_skip::PresentationClear(adv);
    assert(clearCalls == 1 && (*reinterpret_cast<uint32_t*>(adv + 0x48) & 8) == 0);
    assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 42 && skipCalls == beforeMovie);
    *reinterpret_cast<uint32_t*>(adv + 0x40) = 0;
    assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 1 && skipCalls == beforeMovie + 1);
    assert(rebirths::testing::sega_auto_skip::Activations() == beforeEvents);
    assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 42 && skipCalls == beforeMovie + 1);
    // A manual cancellation and a subsequent movie must not revive skip.
    OriginalClear(adv);
    rebirths::testing::sega_auto_skip::PresentationClear(adv);
    assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 42 && skipCalls == beforeMovie + 1);
    // A skip inherited from native/manual state was not activated by us.
    PrepareAdv(adv); assert(rebirths::testing::sega_auto_skip::EventRequest(adv, nullptr, 0x321));
    *reinterpret_cast<uint32_t*>(adv + 0x48) = 8;
    assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 42);
    rebirths::testing::sega_auto_skip::PresentationClear(adv);
    assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 42 && skipCalls == beforeMovie + 1);
    // Disabled and non-story modes always retain the original clear only.
    for (int guard = 0; guard < 2; ++guard) {
        PrepareAdv(adv); assert(rebirths::testing::sega_auto_skip::EventRequest(adv, nullptr, 0x321));
        assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 1);
        if (guard == 0) rebirths::testing::sega_auto_skip::SetActive(0);
        else *reinterpret_cast<uint32_t*>(adv + 0x10) = 1;
        rebirths::testing::sega_auto_skip::PresentationClear(adv);
        rebirths::testing::sega_auto_skip::SetActive(1); *reinterpret_cast<uint32_t*>(adv + 0x10) = 0;
        assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 42);
    }

    // Stale ownership and disabled input cannot consume the pending epoch.
    PrepareAdv(adv); rebirths::testing::sega_auto_skip::EventRequest(adv, nullptr, 0x321);
    *reinterpret_cast<uintptr_t*>(adv + 0x14) = 0x9999;
    assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 42);
    *reinterpret_cast<uintptr_t*>(adv + 0x14) = 0x1234;
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x437b44) = 0;
    assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 42);
    *reinterpret_cast<uintptr_t*>(fakeBase + 0x437b44) = adv;
    *reinterpret_cast<unsigned char*>(adv + 0x850d) = 1;
    assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 42);
    *reinterpret_cast<unsigned char*>(adv + 0x850d) = 0;
    assert(rebirths::testing::sega_auto_skip::StoryInput(adv) == 1);
    const std::pair<uint32_t,uint32_t> sites[]={{0x80649,0x84e50},{0x806bc,0x84e50},
        {0x81e0e,0x81460},{0x81e46,0x81460},{0x81f18,0x81460},{0x8cd1d,0x80700}};
    for (unsigned failure=1; failure<=6; ++failure) {
        Populate(); failOperation=failure;
        assert(!rebirths::InstallSegaHardGirlsAdvAutoSkip(context,&FakeOps));
        for (const auto [r,t]:sites) assert(rebirths::owned_patch::IsOriginalExecutable(
            reinterpret_cast<uintptr_t>(fakeBase+r),fixture::CallBytes(r,t)));
    }
    Populate(); PrepareAdv(adv); *reinterpret_cast<uintptr_t*>(adv+0x14)=1;
    assert(!rebirths::InstallSegaHardGirlsAdvAutoSkip(context,&FakeOps) && operations==0);
    const rebirths::Context wrong{reinterpret_cast<HMODULE>(fakeBase),
        rebirths::GameSpecFor(rebirths::GameId::Rebirth3),L"",L""};
    assert(!rebirths::InstallSegaHardGirlsAdvAutoSkip(wrong,&FakeOps));
    // A false transaction after its write must restore every owned site and
    // keep wrappers disabled. An RX audit rejects a writable "rollback".
    Populate(); failAfterWrite = true;
    assert(!rebirths::InstallSegaHardGirlsAdvAutoSkip(context, &FakeOps)); assert(rebirths::testing::sega_auto_skip::Active() == 0); Bind(); PrepareAdv(adv);
    assert(rebirths::testing::sega_auto_skip::EventRequest(adv, nullptr, 0x321) == 1 && rebirths::testing::sega_auto_skip::StoryInput(adv) == 42);
    Populate(); failAfterWrite = true; leaveWritableOnRollback = true;
    assert(!rebirths::InstallSegaHardGirlsAdvAutoSkip(context, &FakeOps)); assert(rebirths::testing::sega_auto_skip::Active() == 0); Bind(); PrepareAdv(adv);
    assert(rebirths::testing::sega_auto_skip::EventRequest(adv, nullptr, 0x321) == 1 && rebirths::testing::sega_auto_skip::StoryInput(adv) == 42);
    assert(capturedLog.find("rollback=incomplete") != std::string::npos);
    std::puts("ADV auto-skip ABI, one-event latch, forwarding, and rollback tests passed");
}
