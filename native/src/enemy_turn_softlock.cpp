#include "enemy_turn_softlock.hpp"
#include "enemy_turn_profiles.hpp"
#include "enemy_turn_worker.hpp"
#include <atomic>
#include <cstring>
#include <intrin.h>

namespace rebirths::enemy_turn {
using Start = int (__cdecl*)(int, size_t, void*);
Start originalStart = nullptr;
using Creator = int (__cdecl*)(const char*, void*, int, int, int, int, int);
using BusyQuery = unsigned char (__cdecl*)();
using BattleAccessor = void* (__cdecl*)();
Creator originalCreator = nullptr;
BusyQuery originalBusy = nullptr;
BattleAccessor originalBattle = nullptr;
WorkerOwner worker;
#ifdef REBIRTHS_TEST_CONTRACTS
void (*afterBegin)() = nullptr;
#endif
const Profile* selected = nullptr;
uintptr_t continuation = 0;
std::atomic<bool> enabled{false};

int __cdecl ReservePlanning(const char* name, void* callback, int priority,
        int a, int b, int c, int d) noexcept {
    if (!enabled.load(std::memory_order_acquire))
        return originalCreator(name, callback, priority, a, b, c, d);
    // A ticket for this producer only, never a native thread-list slot.
    // Completed old storage is joined before another stable request is written.
    worker.Drain();
    return 0;
}
unsigned char __cdecl PollPlanning() noexcept {
    if (enabled.load(std::memory_order_acquire) && worker.Poll()) return 1;
    return originalBusy();
}
unsigned char __cdecl DrainPlanning() noexcept {
    if (enabled.load(std::memory_order_acquire)) worker.Drain();
    return originalBusy();
}
void* __cdecl DrainBattlePlanning() noexcept {
    if (enabled.load(std::memory_order_acquire)) worker.Drain();
    return originalBattle();
}

int __cdecl StartPlanning(int slot, size_t size, void* arguments) noexcept {
    // This adapter is reachable only from the independently guarded producer
    // CALL. Its four-byte argument contains the battle-owned request pointer.
    auto* planner = *static_cast<unsigned char**>(arguments);
    auto* busy = reinterpret_cast<volatile char*>(planner + selected->busyOffset);
    const bool active = enabled.load(std::memory_order_acquire);
    if (active) {
        worker.Begin(planner);
#ifdef REBIRTHS_TEST_CONTRACTS
        if (afterBegin) afterBegin();
#endif
        return 0; // accepted pending request; never a fabricated completed result
    }
    const int result = originalStart(slot, size, arguments);
    if (result == 0) {
        // A redirect retained after uncertain install cleanup must forward the
        // original behavior, including the late store removed from its caller.
        *busy = 1;
    }
    return result;
}

std::array<unsigned char,5> ExpectedCall(uint32_t site, uint32_t target) noexcept {
    std::array<unsigned char,5> bytes{0xe8,0,0,0,0};
    const uint32_t offset = target - site - 5;
    std::memcpy(bytes.data()+1, &offset, 4);
    return bytes;
}

__declspec(naked) void PlanningBridge() {
    __asm {
        mov dword ptr [ebp-14h], esi
        push ecx
        push 4
        push eax
        call StartPlanning
        add esp, 0ch
        test eax, eax
        jmp dword ptr [continuation]
    }
}

std::array<unsigned char,7> Replacement(uintptr_t site) noexcept {
    std::array<unsigned char,7> bytes{0xe9,0,0,0,0,0x90,0x90};
    const uint32_t displacement = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&PlanningBridge)-site-5);
    std::memcpy(bytes.data()+1, &displacement, 4);
    return bytes;
}
}

namespace rebirths {
bool InstallEnemyTurnSoftlockFix(const Context& context) noexcept {
    using namespace enemy_turn;
    const auto* profile = For(context.spec.id);
    if (!profile) return false;
    const uintptr_t base = reinterpret_cast<uintptr_t>(context.game);
    const std::array<uint32_t,4> targets{profile->creatorRva,profile->busyRva,
        profile->busyRva,profile->battleAccessorRva};
    const std::array<void*,4> replacements{reinterpret_cast<void*>(&ReservePlanning),
        reinterpret_cast<void*>(&PollPlanning),reinterpret_cast<void*>(&DrainPlanning),
        reinterpret_cast<void*>(&DrainBattlePlanning)};
    std::array<CallSite,4> calls{};
    for (size_t i=0; i<calls.size(); ++i) {
        calls[i] = {profile->ownedCalls[i],ExpectedCall(profile->ownedCalls[i],targets[i]),replacements[i]};
        if (std::memcmp(reinterpret_cast<void*>(base+calls[i].rva),calls[i].expected.data(),5))
            return false;
    }
    if (std::memcmp(reinterpret_cast<const void*>(base+profile->callRva),
                    profile->original.data(), profile->original.size()) ||
        std::memcmp(reinterpret_cast<const void*>(base+profile->callRva-7),
                    profile->setup.data(), profile->setup.size())) {
        Log("EnemyTurnSoftlockFix unsupported producer game=%s",GameIdName(context.spec.id));
        return false;
    }
    // Startup identity is shared by all features. The transaction independently
    // checks image ownership, bounds, signatures and peers at this entire block.
    selected = profile;
    originalStart = reinterpret_cast<Start>(base+profile->startRva);
    originalCreator = reinterpret_cast<Creator>(base+profile->creatorRva);
    originalBusy = reinterpret_cast<BusyQuery>(base+profile->busyRva);
    originalBattle = reinterpret_cast<BattleAccessor>(base+profile->battleAccessorRva);
    worker.Configure(reinterpret_cast<Trampoline>(base+profile->trampolineRva),
        reinterpret_cast<void*>(base+profile->callbackRva),profile->busyOffset);
    continuation = base+profile->callRva+16;
    enabled.store(false, std::memory_order_release);
    const auto owned = RetargetCallsResult(context,calls.data(),calls.size());
    if (!owned.Succeeded()) {
        Log("EnemyTurnSoftlockFix game=%s ownership outcome=%s error=%#lx; inactive",
            GameIdName(context.spec.id), PatchOutcomeName(owned.outcome), owned.error);
        return false; // forwarding state remains valid for any retained redirect
    }
    const auto bytes = Replacement(base+profile->callRva-7);
    const auto result = RetargetBytesResult(context, profile->callRva-7,
        profile->setup.data(), bytes.data(), bytes.size());
    if (!result.Succeeded()) {
        // The preceding dependency redirects may remain; keep all forwarding
        // state even when this individual bridge transaction restored its bytes.
        Log("EnemyTurnSoftlockFix game=%s outcome=%s error=%#lx; inactive",
            GameIdName(context.spec.id), PatchOutcomeName(result.outcome), result.error);
        return false;
    }
    enabled.store(true, std::memory_order_release);
    Log("EnemyTurnSoftlockFix installed game=%s", GameIdName(context.spec.id));
    return true;
}
}
