#pragma once

#include "rebirths_patch.hpp"
#include <cstring>

namespace rebirths::owned_patch {

// These operations keep the existing one-site transaction boundary. Their
// boolean result is deliberately separate from the audited ownership outcome.
struct PatchOps {
    bool (*retargetCalls)(const Context&, const CallSite*, size_t) noexcept;
    bool (*retargetBytes)(const Context&, uint32_t, const unsigned char*, const unsigned char*, size_t) noexcept;
};

struct Platform {
    SIZE_T (WINAPI* query)(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T) = VirtualQuery;
    void (WINAPI* sleep)(DWORD) = Sleep;
};

constexpr std::array<unsigned char, 5> CallTo(uintptr_t source, uintptr_t target) noexcept {
    const uint32_t displacement = static_cast<uint32_t>(target - source - 5);
    return {0xe8, static_cast<unsigned char>(displacement), static_cast<unsigned char>(displacement >> 8),
            static_cast<unsigned char>(displacement >> 16), static_cast<unsigned char>(displacement >> 24)};
}

struct PreparedCall {
    CallSite site;
    std::array<unsigned char, 5> replacement{};
};

struct BytePatch {
    uint32_t rva;
    const unsigned char* expected;
    const unsigned char* replacement;
    size_t size;
    const char* name;
};

// RB1 fast-forward historically compacts in order; the other seven installers
// swap in the last owned site. Preserve their retry order during extraction.
enum class RemovalOrder { SwapLast, Stable };
constexpr size_t MaximumSites = 64;

inline bool ReadExecutable(uintptr_t address, size_t size, const Platform& platform = {}) noexcept {
    if (!size || size > UINTPTR_MAX - address) return false;
    const uintptr_t end = address + size;
    while (address < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (platform.query(reinterpret_cast<const void*>(address), &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || memory.Protect != PAGE_EXECUTE_READ) return false;
        const uintptr_t region = reinterpret_cast<uintptr_t>(memory.BaseAddress);
        if (region > address || memory.RegionSize > UINTPTR_MAX - region) return false;
        const uintptr_t next = region + memory.RegionSize;
        if (next <= address) return false;
        address = next;
    }
    return true;
}

inline bool BytesAt(uintptr_t address, const unsigned char* expected, size_t size) noexcept {
    return std::memcmp(reinterpret_cast<const void*>(address), expected, size) == 0;
}

template <size_t Count>
bool BytesAt(uintptr_t address, const std::array<unsigned char, Count>& expected) noexcept {
    return BytesAt(address, expected.data(), expected.size());
}

inline bool IsOriginalExecutable(uintptr_t address, const unsigned char* expected, size_t size,
                                 const Platform& platform = {}) noexcept {
    // Query before reading, including the second region of a straddling site.
    return ReadExecutable(address, size, platform) && BytesAt(address, expected, size);
}

template <size_t Count>
bool IsOriginalExecutable(uintptr_t address, const std::array<unsigned char, Count>& expected) noexcept {
    return IsOriginalExecutable(address, expected.data(), expected.size());
}

inline PatchResult Install(const Context& context, PreparedCall* calls, size_t callCount,
                           const PatchOps& ops, const char* label,
                           const BytePatch* bytes = nullptr, size_t byteCount = 0,
                           RemovalOrder order = RemovalOrder::SwapLast,
                           const Platform& platform = {}) noexcept {
    const uintptr_t base = reinterpret_cast<uintptr_t>(context.game);
    const size_t total = callCount + byteCount;
    if (!base || !label || !ops.retargetCalls || !ops.retargetBytes || !platform.query || !platform.sleep ||
        callCount > MaximumSites || byteCount > MaximumSites - callCount || !total ||
        (callCount && !calls) || (byteCount && !bytes)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return {PatchOutcome::Rejected, ERROR_INVALID_PARAMETER};
    }

    // Descriptors remain alive for this synchronous call. Adapters must pin
    // native forwarding state independently, including on incomplete recovery.
    std::array<BytePatch, MaximumSites> sites{};
    for (size_t i = 0; i < callCount; ++i) {
        auto& prepared = calls[i];
        prepared.replacement = CallTo(base + prepared.site.rva, reinterpret_cast<uintptr_t>(prepared.site.replacement));
        sites[i] = {prepared.site.rva, prepared.site.expected.data(), prepared.replacement.data(), 5, ""};
    }
    for (size_t i = 0; i < byteCount; ++i) sites[callCount + i] = bytes[i];
    for (size_t i = 0; i < total; ++i) {
        const auto& site = sites[i];
        if (!site.expected || !site.replacement || !site.size || site.size > 16 ||
            site.rva > UINTPTR_MAX - base || site.size > UINTPTR_MAX - (base + site.rva) ||
            !IsOriginalExecutable(base + site.rva, site.expected, site.size, platform)) {
            Log("%s preflight failed rva=%#x; no calls changed", label, site.rva);
            SetLastError(ERROR_INVALID_DATA);
            return {PatchOutcome::Rejected, ERROR_INVALID_DATA};
        }
        for (size_t j = 0; j < i; ++j) {
            const auto& earlier = sites[j];
            if (static_cast<uint64_t>(site.rva) < static_cast<uint64_t>(earlier.rva) + earlier.size &&
                static_cast<uint64_t>(earlier.rva) < static_cast<uint64_t>(site.rva) + site.size) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return {PatchOutcome::Rejected, ERROR_INVALID_PARAMETER};
            }
        }
    }

    std::array<size_t, MaximumSites> ownedCalls{}, ownedBytes{};
    size_t ownedCallCount = 0, ownedByteCount = 0;
    for (size_t i = 0; i < total; ++i) {
        const auto& site = sites[i];
        bool patched = false;
        DWORD failure = ERROR_INVALID_DATA;
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            SetLastError(0);
            const bool transaction = i < callCount
                ? ops.retargetCalls(context, &calls[i].site, 1)
                : ops.retargetBytes(context, site.rva, site.expected, site.replacement, site.size);
            failure = GetLastError();
            if (!failure) failure = ERROR_INVALID_DATA;
            // Failed primitives may have written bytes or left a writable page.
            // Such a site belongs to recovery and must never activate a feature.
            const bool wrote = BytesAt(base + site.rva, site.replacement, site.size);
            if (wrote) {
                if (i < callCount) ownedCalls[ownedCallCount++] = i;
                else ownedBytes[ownedByteCount++] = i;
                patched = transaction && ReadExecutable(base + site.rva, site.size, platform);
                break;
            }
            if (attempt < 2) platform.sleep(25);
        }
        if (patched) continue;

        const size_t stage = ownedCallCount + ownedByteCount;
        const auto restoreGroup = [&](auto& owned, size_t& ownedCount, RemovalOrder removal) noexcept {
            for (size_t index = ownedCount; index-- > 0;) {
                const auto& restore = sites[owned[index]];
                ops.retargetBytes(context, restore.rva, restore.replacement, restore.expected, restore.size);
                if (IsOriginalExecutable(base + restore.rva, restore.expected, restore.size, platform)) {
                    if (removal == RemovalOrder::Stable) {
                        for (size_t move = index + 1; move < ownedCount; ++move) owned[move - 1] = owned[move];
                        --ownedCount;
                    } else owned[index] = owned[--ownedCount];
                } else Log("%s %srollback failed rva=%#x error=%#lx", label, restore.name, restore.rva, GetLastError());
            }
        };
        for (unsigned attempt = 0; attempt < 20 && (ownedCallCount || ownedByteCount); ++attempt) {
            // Presentation recovery stays ahead of every CALL recovery pass,
            // including when some CALLs recover before the presentation does.
            restoreGroup(ownedBytes, ownedByteCount, RemovalOrder::Stable);
            restoreGroup(ownedCalls, ownedCallCount, order);
            if (ownedCallCount || ownedByteCount) platform.sleep(1);
        }
        bool restored = ownedCallCount == 0 && ownedByteCount == 0;
        for (size_t index = 0; index < total; ++index) {
            const auto& original = sites[index];
            restored = IsOriginalExecutable(base + original.rva, original.expected, original.size, platform) && restored;
        }
        const PatchOutcome outcome = restored ? (stage ? PatchOutcome::Restored : PatchOutcome::Rejected)
                                             : PatchOutcome::IncompleteRecovery;
        Log("%s %sfailed rva=%#x stage=%zu error=%#lx rollback=%s outcome=%s", label, site.name, site.rva,
            stage, failure, restored ? "complete" : "incomplete", PatchOutcomeName(outcome));
        SetLastError(failure);
        return {outcome, failure};
    }
    SetLastError(0);
    return {PatchOutcome::Installed, 0};
}

template <size_t Count>
PatchResult Install(const Context& context, std::array<PreparedCall, Count>& calls, const PatchOps& ops,
                    const char* label, const BytePatch* bytes = nullptr, size_t byteCount = 0,
                    RemovalOrder order = RemovalOrder::SwapLast) noexcept {
    static_assert(Count <= MaximumSites, "Owned patch sequence exceeds bounded scratch capacity");
    return Install(context, calls.data(), calls.size(), ops, label, bytes, byteCount, order);
}

} // namespace rebirths::owned_patch
