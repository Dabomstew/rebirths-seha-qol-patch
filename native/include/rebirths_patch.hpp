#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include "game_ids.hpp"

namespace rebirths {

using Digest = std::array<unsigned char, 32>;

struct GameSpec {
    GameId id;
    const wchar_t* executableName;
    Digest executableSha256;
    Digest executableSha256LargeAddressAware;
    Digest executableSha256LargeAddressAwareWithChecksum;
    uint32_t preferredImageBase;
    uint32_t textStartRva;
    uint32_t textEndRva;
};

const GameSpec* FindGameSpec(GameId id) noexcept;
// Invalid IDs are rejected; a failed selection must never masquerade as RB1.
const GameSpec& GameSpecFor(GameId id);
const GameSpec* IdentifyGame(const Digest& digest) noexcept;
const char* GameIdName(GameId id) noexcept;

struct PatchContext {
    HMODULE game;
    GameSpec spec;
    std::wstring directory;
    std::wstring ini;

    // The target must be selected explicitly; shared callers cannot default to RB1.
    PatchContext(HMODULE module, const GameSpec& selected, std::wstring patchDirectory,
                 std::wstring configuration)
        : game(module), spec(selected), directory(std::move(patchDirectory)), ini(std::move(configuration)) {}
};

using Context = PatchContext;
struct CallSite { uint32_t rva; std::array<unsigned char, 5> expected; void* replacement; };

// Outcome describes code ownership, independently of cleanup success. Installed
// with an error (e.g. failed peer resumption) must retain forwarding state but
// must not activate a feature. IncompleteRecovery is deliberately conservative.
enum class PatchOutcome { Rejected, Installed, Restored, IncompleteRecovery };
struct PatchResult {
    PatchOutcome outcome;
    DWORD error;
    bool Succeeded() const noexcept { return outcome == PatchOutcome::Installed && error == 0; }
    bool MayRedirect() const noexcept {
        return outcome == PatchOutcome::Installed || outcome == PatchOutcome::IncompleteRecovery;
    }
};
inline const char* PatchOutcomeName(PatchOutcome outcome) noexcept {
    switch (outcome) {
    case PatchOutcome::Rejected: return "rejected-no-writes";
    case PatchOutcome::Installed: return "installed";
    case PatchOutcome::Restored: return "restored";
    default: return "incomplete-recovery";
    }
}

std::wstring ModulePath(HMODULE module);
Digest HashFile(const std::wstring& path);
int Option(const Context& context, const wchar_t* section, const wchar_t* key, int fallback);
void Log(const char* format, ...) noexcept;
bool RetargetCalls(const Context& context, const CallSite* sites, size_t count) noexcept;
bool RetargetBytes(const Context& context, uint32_t rva, const unsigned char* expected, const unsigned char* replacement, size_t count) noexcept;
PatchResult RetargetCallsResult(const Context& context, const CallSite* sites, size_t count) noexcept;
PatchResult RetargetBytesResult(const Context& context, uint32_t rva, const unsigned char* expected, const unsigned char* replacement, size_t count) noexcept;
void Initialize(HMODULE proxy) noexcept;

}
