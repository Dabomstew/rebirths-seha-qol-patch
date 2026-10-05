#pragma once
#include "rebirths_patch.hpp"
#include <algorithm>
#include <tlhelp32.h>

namespace rebirths {

// Narrow platform contract for freeze fault fixtures. Production uses Win32;
// the caller supplies instruction ranges, without any game/ABI assumptions.
struct ThreadApi {
    decltype(&::CreateToolhelp32Snapshot) snapshot = &::CreateToolhelp32Snapshot;
    decltype(&::Thread32First) first = &::Thread32First;
    decltype(&::Thread32Next) next = &::Thread32Next;
    decltype(&::OpenThread) open = &::OpenThread;
    decltype(&::SuspendThread) suspend = &::SuspendThread;
    decltype(&::GetThreadContext) context = &::GetThreadContext;
    decltype(&::ResumeThread) resume = &::ResumeThread;
    decltype(&::WaitForSingleObject) wait = &::WaitForSingleObject;
    decltype(&::CloseHandle) close = &::CloseHandle;
};

struct InstructionRange { uintptr_t start; size_t size; };
enum class FreezeFailure { None, Enumeration, Open, Suspend, Context, InSite, Changed };

class ThreadFreeze {
public:
    // Includes the calling thread. Overflow rejects the transaction; no heap
    // fallback is permitted. Two ID lists and handle state use < 25 KiB on x86.
    static constexpr size_t Capacity = 1024;

    explicit ThreadFreeze(const ThreadApi& api = DefaultApi()) noexcept : api_(api) {}
    ThreadFreeze(const ThreadFreeze&) = delete;
    ThreadFreeze& operator=(const ThreadFreeze&) = delete;
    ~ThreadFreeze() noexcept {
        const DWORD error = GetLastError();
        // One bounded retry for peers not resumed by the explicit Release.
        Release();
        for (size_t i = peerCount_; i != 0; --i) api_.close(peers_[i - 1].handle);
        SetLastError(error);
    }

    FreezeFailure Acquire(const InstructionRange* ranges, size_t count) noexcept {
        // A freeze owns one transaction and cannot be acquired twice.
        if (acquired_) return FreezeFailure::Enumeration;
        acquired_ = true;
        size_t beforeCount = 0, afterCount = 0;
        const DWORD current = GetCurrentThreadId();
        if (!Enumerate(before_, beforeCount) ||
            !std::binary_search(before_.begin(), before_.begin() + beforeCount, current))
            return FreezeFailure::Enumeration;

        for (size_t i = 0; i < beforeCount; ++i) {
            const DWORD id = before_[i];
            if (id == current) continue;
            HANDLE handle = api_.open(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                      THREAD_QUERY_LIMITED_INFORMATION, FALSE, id);
            if (!handle) return Reject(FreezeFailure::Open);
            Peer& peer = peers_[peerCount_++];
            peer.handle = handle;
            if (api_.suspend(handle) == DWORD(-1)) return Reject(FreezeFailure::Suspend);
            peer.suspended = true;
            CONTEXT registers{};
            registers.ContextFlags = CONTEXT_CONTROL;
            if (!api_.context(handle, &registers)) return Reject(FreezeFailure::Context);
            for (size_t site = 0; site < count; ++site)
                if (registers.Eip >= ranges[site].start &&
                    registers.Eip - ranges[site].start < ranges[site].size)
                    return Reject(FreezeFailure::InSite);
        }
        // Only bounded stack storage and Win32 calls, including this recheck,
        // are used after suspension. Enumeration errors cannot mimic no peers.
        if (!Enumerate(after_, afterCount) || beforeCount != afterCount ||
            !std::equal(before_.begin(), before_.begin() + beforeCount, after_.begin()))
            return Reject(FreezeFailure::Changed);
        return FreezeFailure::None;
    }

    unsigned Release() noexcept {
        unsigned failures = 0;
        for (size_t i = peerCount_; i != 0; --i) {
            Peer& peer = peers_[i - 1];
            if (!peer.suspended) continue;
            if (api_.resume(peer.handle) == DWORD(-1) &&
                api_.wait(peer.handle, 0) != WAIT_OBJECT_0) ++failures;
            else peer.suspended = false;
        }
        return failures;
    }

private:
    struct Peer { HANDLE handle = nullptr; bool suspended = false; };
    ThreadApi api_;
    std::array<DWORD, Capacity> before_{};
    std::array<DWORD, Capacity> after_{};
    std::array<Peer, Capacity> peers_{};
    size_t peerCount_ = 0;
    bool acquired_ = false;

    static const ThreadApi& DefaultApi() noexcept {
        static const ThreadApi api{};
        return api;
    }

    FreezeFailure Reject(FreezeFailure reason) noexcept {
        Release();
        return reason;
    }

    bool Enumerate(std::array<DWORD, Capacity>& ids, size_t& count) noexcept {
        const HANDLE snapshot = api_.snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        THREADENTRY32 entry{sizeof(entry)};
        bool complete = api_.first(snapshot, &entry) != FALSE;
        const DWORD process = GetCurrentProcessId();
        if (complete) {
            do {
                if (entry.th32OwnerProcessID == process) {
                    if (count == Capacity) { complete = false; break; }
                    ids[count++] = entry.th32ThreadID;
                }
                if (!api_.next(snapshot, &entry)) {
                    complete = GetLastError() == ERROR_NO_MORE_FILES;
                    break;
                }
            } while (true);
        }
        if (!api_.close(snapshot)) complete = false;
        if (!complete || !count) return false;
        std::sort(ids.begin(), ids.begin() + count);
        // Duplicate entries would make suspend counts ambiguous.
        return std::adjacent_find(ids.begin(), ids.begin() + count) == ids.begin() + count;
    }
};

}
