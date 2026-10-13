#pragma once
#include "rebirths_patch.hpp"
#include <atomic>
#include <cstring>
#include <intrin.h>

namespace rebirths::enemy_turn {
// Only the independently validated native trampoline receives this private
// record. It is never inserted in the engine's independently reaped slot list.
struct WorkerArguments {
    HANDLE unusedHandle = nullptr;
    unsigned char unusedActive[4]{};
    uint32_t unusedId = 0, unusedThreadId = 0;
    char name[32]{};
    void* callback = nullptr;
    size_t size = sizeof(void*);
    void* arguments = nullptr;
};
static_assert(offsetof(WorkerArguments, callback) == 0x30);
static_assert(offsetof(WorkerArguments, arguments) == 0x38);
static_assert(sizeof(WorkerArguments) == 0x3c);

using Trampoline = DWORD (WINAPI*)(void*);
#ifdef REBIRTHS_TEST_CONTRACTS
struct WorkerApi {
    decltype(&::CreateThread) create = ::CreateThread;
    decltype(&::WaitForSingleObject) wait = ::WaitForSingleObject;
    decltype(&::CloseHandle) close = ::CloseHandle;
};
inline WorkerApi workerApi;
#endif

class WorkerOwner {
public:
    enum class Phase : unsigned { Idle, Pending, Gated, Ready, Running, Cancelled };
    WorkerOwner() noexcept = default;
    WorkerOwner(const WorkerOwner&) = delete;
    WorkerOwner& operator=(const WorkerOwner&) = delete;

    // Caller methods serialize submission, polling and destruction. The worker
    // uses only the gate and immutable request until its actual OS termination.
    void Configure(Trampoline trampoline, void* callback, unsigned busyOffset) noexcept {
        trampoline_ = trampoline;
        request_.callback = callback;
        std::memcpy(request_.name, "RB_HexCreate", 13);
        busyOffset_ = busyOffset;
    }
    void Begin(unsigned char* planner) noexcept {
        Lock lock(mutex_);
        DrainLocked();
        planner_ = planner;
        request_.arguments = &planner_;
        _InterlockedExchange8(Busy(), 1);
        phase_.store(Phase::Pending, std::memory_order_release);
        retryAt_ = 0;
        TryCreateLocked();
    }
    bool Poll() noexcept {
        Lock lock(mutex_);
        if (!planner_) return false;
        if (phase_.load(std::memory_order_acquire) == Phase::Pending) TryCreateLocked();
        if (!handle_) return true;
        const DWORD waited = Wait(handle_, 0);
        if (waited != WAIT_OBJECT_0) return true;
        // A signaled worker alone is not proof that native results were
        // published. Never turn an abnormal callback exit into stale readiness.
        if (*Busy() != 0) return true;
        return !RetireLocked();
    }
    void Drain() noexcept {
        Lock lock(mutex_);
        DrainLocked();
    }
#ifdef REBIRTHS_TEST_CONTRACTS
    Phase TestPhase() const noexcept { return phase_.load(std::memory_order_acquire); }
    HANDLE TestHandle() const noexcept { return handle_; }
    unsigned char* TestPlanner() const noexcept { return planner_; }
#endif

private:
    struct Lock {
        explicit Lock(SRWLOCK& mutex) noexcept : mutex_(mutex) { AcquireSRWLockExclusive(&mutex_); }
        ~Lock() noexcept { ReleaseSRWLockExclusive(&mutex_); }
        SRWLOCK& mutex_;
    };
    static HANDLE Create(LPTHREAD_START_ROUTINE entry, void* argument) noexcept {
#ifdef REBIRTHS_TEST_CONTRACTS
        return workerApi.create(nullptr, 0, entry, argument, 0, nullptr);
#else
        return ::CreateThread(nullptr, 0, entry, argument, 0, nullptr);
#endif
    }
    static DWORD Wait(HANDLE handle, DWORD timeout) noexcept {
#ifdef REBIRTHS_TEST_CONTRACTS
        return workerApi.wait(handle, timeout);
#else
        return ::WaitForSingleObject(handle, timeout);
#endif
    }
    static BOOL Close(HANDLE handle) noexcept {
#ifdef REBIRTHS_TEST_CONTRACTS
        return workerApi.close(handle);
#else
        return ::CloseHandle(handle);
#endif
    }
    volatile char* Busy() const noexcept { return reinterpret_cast<volatile char*>(planner_ + busyOffset_); }
    static DWORD WINAPI Entry(void* opaque) noexcept {
        auto* owner = static_cast<WorkerOwner*>(opaque);
        for (;;) {
            auto expected = Phase::Ready;
            if (owner->phase_.compare_exchange_strong(expected, Phase::Running,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                // Native callback calls ExitThread. No cleanup is placed after
                // this call; the submitting owner joins and releases the record.
                return owner->trampoline_(&owner->request_);
            }
            if (expected == Phase::Cancelled) return 0;
            Sleep(1);
        }
    }
    void TryCreateLocked() noexcept {
        const auto now = GetTickCount64();
        if (now < retryAt_) return;
        phase_.store(Phase::Gated, std::memory_order_release);
        handle_ = Create(Entry, this);
        if (!handle_) {
            // No worker owns the stable request on a NULL return. Keep it
            // pending, without another allocation or fabricated result.
            phase_.store(Phase::Pending, std::memory_order_release);
            retryAt_ = now + 10;
            return;
        }
        SetThreadPriority(handle_, THREAD_PRIORITY_BELOW_NORMAL);
        phase_.store(Phase::Ready, std::memory_order_release);
    }
    bool RetireLocked() noexcept {
        if (handle_ && !Close(handle_)) return false;
        handle_ = nullptr;
        planner_ = nullptr;
        request_.arguments = nullptr;
        phase_.store(Phase::Idle, std::memory_order_release);
        return true;
    }
    void DrainLocked() noexcept {
        if (!planner_) return;
        auto phase = phase_.load(std::memory_order_acquire);
        while (phase != Phase::Running && phase != Phase::Cancelled &&
               !phase_.compare_exchange_weak(phase, Phase::Cancelled,
                   std::memory_order_acq_rel, std::memory_order_acquire)) {}
        if (handle_) {
            // Timeout/failure retains ownership. No CloseHandle/free/busy-clear
            // can substitute for a signaled thread. The worker needs no mutex.
            while (Wait(handle_, INFINITE) != WAIT_OBJECT_0) Sleep(1);
        }
        _InterlockedExchange8(Busy(), 0); // explicit teardown only, after join
        while (!RetireLocked()) Sleep(1);
    }
    SRWLOCK mutex_ = SRWLOCK_INIT;
    std::atomic<Phase> phase_{Phase::Idle};
    HANDLE handle_ = nullptr;
    unsigned char* planner_ = nullptr;
    WorkerArguments request_{};
    Trampoline trampoline_ = nullptr;
    unsigned busyOffset_ = 0;
    ULONGLONG retryAt_ = 0;
};
}
