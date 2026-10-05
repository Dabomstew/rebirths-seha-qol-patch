#include "rebirth3_battle_delay.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <optional>

namespace rebirths {
namespace {
constexpr uint32_t DelayImmediateRva = 0x001d5c5c;
constexpr uint32_t BattleStartCallRva = 0x0020897a;
constexpr uint32_t BattleUpdateCallRva = 0x0020895d;
constexpr uint32_t BattleDestroyCallRva = 0x0020896a;
constexpr unsigned char OriginalDelay = 0x14;
constexpr unsigned char ShortDelay = 0x00;
constexpr std::array<unsigned char, 5> DelayInstruction{{0x83, 0xf8, 0x14, 0x7f, 0x13}};

std::optional<Context> context;
uintptr_t owner = 0;
bool changed = false;
std::atomic<bool> enabled{false};
using BattleStartFn = void (__cdecl*)(uintptr_t, uint32_t);
using BattleUpdateFn = void (__cdecl*)(uintptr_t);
using BattleDestroyFn = void (__cdecl*)(uintptr_t);
BattleStartFn originalStart = nullptr;
BattleUpdateFn originalUpdate = nullptr;
BattleDestroyFn originalDestroy = nullptr;

bool DelayBytes(bool shortened) noexcept {
    if (!context) return false;
    const auto* bytes = reinterpret_cast<const unsigned char*>(reinterpret_cast<uintptr_t>(context->game) +
                                                               DelayImmediateRva - 2);
    return bytes[0] == 0x83 && bytes[1] == 0xf8 && bytes[2] == (shortened ? ShortDelay : OriginalDelay) &&
           bytes[3] == 0x7f && bytes[4] == 0x13;
}

bool ChangeDelay(unsigned char from, unsigned char to) noexcept {
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const bool transaction = RetargetBytes(*context, DelayImmediateRva, &from, &to, 1);
        if (DelayBytes(to == ShortDelay)) {
            if (!transaction) Log("BattleDelay byte changed despite transaction error game=rebirth3 error=%#lx", GetLastError());
            return true;
        }
        if (attempt < 2) Sleep(1);
    }
    Log("BattleDelay byte transaction failed game=rebirth3 from=%#x to=%#x error=%#lx", from, to, GetLastError());
    return false;
}

bool Restore() noexcept {
    if (!changed) { owner = 0; return true; }
    if (!ChangeDelay(ShortDelay, OriginalDelay)) return false;
    changed = false;
    owner = 0;
    Log("BattleDelay restored game=rebirth3");
    return true;
}

void __cdecl BattleStart(uintptr_t battle, uint32_t handle) noexcept {
    originalStart(battle, handle);
    if (!enabled.load(std::memory_order_acquire)) return;
    if (changed && !Restore()) return;
    if (!battle || *reinterpret_cast<const uint32_t*>(battle + 4) != 1 || !DelayBytes(false)) {
        Log("BattleDelay start guard refused game=rebirth3");
        return;
    }
    if (ChangeDelay(OriginalDelay, ShortDelay)) {
        changed = true;
        owner = battle;
        Log("BattleDelay shortened game=rebirth3 owner=%#x", static_cast<unsigned>(battle));
    }
}

void __cdecl BattleUpdate(uintptr_t battle) noexcept {
    if (changed && battle != owner) Restore();
    originalUpdate(battle);
    if (changed && battle == owner && *reinterpret_cast<const uint32_t*>(battle + 4) >= 4) Restore();
}

void __cdecl BattleDestroy(uintptr_t battle) noexcept {
    if (changed && !Restore()) Log("BattleDelay destroy restore pending game=rebirth3");
    originalDestroy(battle);
}
} // namespace

bool InstallRebirth3BattleDelay(const Context& target) noexcept {
    if (target.spec.id != GameId::Rebirth3) return false;
    const uintptr_t base = reinterpret_cast<uintptr_t>(target.game);
    if (std::memcmp(reinterpret_cast<const void*>(base + DelayImmediateRva - 2),
                    DelayInstruction.data(), DelayInstruction.size()) != 0) {
        Log("BattleDelay unsupported instruction game=rebirth3");
        return false;
    }
    constexpr std::array<unsigned char, 5> updateBytes{{0xe8, 0x0e, 0xff, 0xff, 0xff}};
    constexpr std::array<unsigned char, 5> destroyBytes{{0xe8, 0x01, 0xfb, 0xff, 0xff}};
    constexpr std::array<unsigned char, 5> startBytes{{0xe8, 0x91, 0xfa, 0xff, 0xff}};
    if (std::memcmp(reinterpret_cast<const void*>(base + BattleUpdateCallRva), updateBytes.data(), 5) ||
        std::memcmp(reinterpret_cast<const void*>(base + BattleDestroyCallRva), destroyBytes.data(), 5) ||
        std::memcmp(reinterpret_cast<const void*>(base + BattleStartCallRva), startBytes.data(), 5)) {
        Log("BattleDelay unsupported lifecycle calls game=rebirth3");
        return false;
    }
    originalStart = reinterpret_cast<BattleStartFn>(base + 0x00208410);
    originalUpdate = reinterpret_cast<BattleUpdateFn>(base + 0x00208870);
    originalDestroy = reinterpret_cast<BattleDestroyFn>(base + 0x00208470);
    context.emplace(target);
    enabled.store(false, std::memory_order_release);
    owner = 0;
    changed = false;
    const CallSite sites[] = {
        {BattleUpdateCallRva, updateBytes, reinterpret_cast<void*>(&BattleUpdate)},
        {BattleDestroyCallRva, destroyBytes, reinterpret_cast<void*>(&BattleDestroy)},
        {BattleStartCallRva, startBytes, reinterpret_cast<void*>(&BattleStart)},
    };
    const auto result = RetargetCallsResult(target, sites, 3);
    if (!result.Succeeded()) {
        if (!result.MayRedirect()) context.reset();
        Log("BattleDelay lifecycle install outcome=%s game=rebirth3 error=%#lx; forwarding", PatchOutcomeName(result.outcome), result.error);
        return false;
    }
    enabled.store(true, std::memory_order_release);
    Log("BattleDelay installed guarded lifecycle calls game=rebirth3");
    return true;
}

} // namespace rebirths
