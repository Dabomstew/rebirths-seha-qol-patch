#include "../src/rebirth3_battle_delay.cpp"
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <cstdarg>

namespace rebirths {
static PatchOutcome injectedOutcome = PatchOutcome::Installed;
static DWORD injectedError = 0x2100000d;
static std::string lastLog;
void Log(const char* format, ...) noexcept {
    char buffer[512]; va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args); va_end(args); lastLog = buffer;
}
bool RetargetBytes(const Context& target, uint32_t rva, const unsigned char* before, const unsigned char* after, size_t count) noexcept {
    auto* bytes = reinterpret_cast<unsigned char*>(target.game) + rva;
    if (std::memcmp(bytes, before, count)) return false;
    std::memcpy(bytes, after, count); return true;
}
PatchResult RetargetCallsResult(const Context& target, const CallSite* sites, size_t count) noexcept {
    // Reproduce reason 13: writes succeeded, but peer resumption failed.
    const uintptr_t base = reinterpret_cast<uintptr_t>(target.game);
    const size_t writes = injectedOutcome == PatchOutcome::Installed ? count :
        injectedOutcome == PatchOutcome::IncompleteRecovery ? 1 : 0;
    for (size_t i = 0; i < writes; ++i) {
        const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sites[i].replacement) - base - sites[i].rva - 5);
        std::memcpy(reinterpret_cast<void*>(base + sites[i].rva + 1), &relative, 4);
    }
    SetLastError(injectedError);
    return {injectedOutcome, injectedError};
}
}

static void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static unsigned starts = 0, updates = 0, destroys = 0;
static void __cdecl Start(uintptr_t, uint32_t) { ++starts; }
static void __cdecl Update(uintptr_t) { ++updates; }
static void __cdecl Destroy(uintptr_t) { ++destroys; }
int main() {
    try {
        using namespace rebirths;
        for (const auto fault : {PatchOutcome::Installed, PatchOutcome::IncompleteRecovery,
                                PatchOutcome::Restored, PatchOutcome::Rejected}) {
        injectedOutcome = fault;
        enabled.store(false);
        std::vector<unsigned char> image(0x210000);
        std::memcpy(image.data() + DelayImmediateRva - 2, DelayInstruction.data(), DelayInstruction.size());
        const unsigned char update[]{0xe8, 0x0e, 0xff, 0xff, 0xff};
        const unsigned char destroy[]{0xe8, 0x01, 0xfb, 0xff, 0xff};
        const unsigned char start[]{0xe8, 0x91, 0xfa, 0xff, 0xff};
        std::memcpy(image.data() + BattleUpdateCallRva, update, 5);
        std::memcpy(image.data() + BattleDestroyCallRva, destroy, 5);
        std::memcpy(image.data() + BattleStartCallRva, start, 5);
        GameSpec spec{}; spec.id = GameId::Rebirth3;
        Context target(reinterpret_cast<HMODULE>(image.data()), spec, L"", L"");
        Require(!InstallRebirth3BattleDelay(target), "uncertain installation must not activate");
        Require(context.has_value() == (fault == PatchOutcome::Installed || fault == PatchOutcome::IncompleteRecovery),
                "hook context lifetime must follow ownership");
        Require(lastLog.find(PatchOutcomeName(fault)) != std::string::npos, "log must describe actual outcome");
        originalStart = Start; originalUpdate = Update; originalDestroy = Destroy;
        unsigned char battle[8]{}; *reinterpret_cast<uint32_t*>(battle + 4) = 1;
        BattleStart(reinterpret_cast<uintptr_t>(battle), 7);
        BattleUpdate(reinterpret_cast<uintptr_t>(battle));
        BattleDestroy(reinterpret_cast<uintptr_t>(battle));
        Require(!enabled.load() && !changed && image[DelayImmediateRva] == OriginalDelay,
                "uncertain wrappers must only forward");
        context.reset();
        }
        Require(starts == 4 && updates == 4 && destroys == 4, "all native lifecycle effects preserved");
        // Accepted installation still shortens/restores only inside its owned
        // loading window; update and destroy preserve their native effects.
        std::vector<unsigned char> image(0x210000);
        std::memcpy(image.data() + DelayImmediateRva - 2, DelayInstruction.data(), DelayInstruction.size());
        const unsigned char calls[][5]{{0xe8,0x0e,0xff,0xff,0xff},{0xe8,0x01,0xfb,0xff,0xff},{0xe8,0x91,0xfa,0xff,0xff}};
        const uint32_t rvas[]{BattleUpdateCallRva,BattleDestroyCallRva,BattleStartCallRva};
        for (size_t i=0;i<3;++i) std::memcpy(image.data()+rvas[i],calls[i],5);
        GameSpec spec{};spec.id=GameId::Rebirth3;
        Context target(reinterpret_cast<HMODULE>(image.data()),spec,L"",L"");
        injectedOutcome=PatchOutcome::Installed;injectedError=0;
        Require(InstallRebirth3BattleDelay(target), "complete installation enabled");
        originalStart=Start;originalUpdate=Update;originalDestroy=Destroy;
        unsigned char battle[8]{};*reinterpret_cast<uint32_t*>(battle+4)=1;
        BattleStart(reinterpret_cast<uintptr_t>(battle),7);
        Require(changed && image[DelayImmediateRva]==ShortDelay,"owned loading delay shortened");
        *reinterpret_cast<uint32_t*>(battle+4)=4;BattleUpdate(reinterpret_cast<uintptr_t>(battle));
        Require(!changed && image[DelayImmediateRva]==OriginalDelay,"ready owner restored");
        *reinterpret_cast<uint32_t*>(battle+4)=1;BattleStart(reinterpret_cast<uintptr_t>(battle),7);
        BattleDestroy(reinterpret_cast<uintptr_t>(battle));
        Require(!changed && image[DelayImmediateRva]==OriginalDelay,"destroy restored");
        context.reset();enabled.store(false);
        std::puts("Battle delay outcomes OK: retained forwarding state, disabled partial installs, native lifecycle, precise logs");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
