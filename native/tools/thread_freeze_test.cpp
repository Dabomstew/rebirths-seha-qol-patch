#include "thread_freeze.hpp"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>

static bool forbidAllocation = false;
static unsigned allocations = 0;
void* operator new(size_t size) {
    if (forbidAllocation) { ++allocations; throw std::bad_alloc(); }
    if (void* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, size_t) noexcept { std::free(value); }
void operator delete[](void* value, size_t) noexcept { std::free(value); }

namespace {
constexpr size_t Limit = rebirths::ThreadFreeze::Capacity;
struct Fixture {
    std::array<std::array<DWORD, Limit + 2>, 2> ids{};
    std::array<size_t, 2> counts{};
    std::array<unsigned, Limit + 2> suspends{}, resumes{}, closes{};
    std::array<unsigned, 2> snapshotCloses{};
    std::array<size_t, 2 * Limit + 4> resumeOrder{};
    unsigned snapshots = 0, opens = 0, contexts = 0, totalResumes = 0;
    size_t cursor = 0, active = 0;
    unsigned failSnapshot = 0, failFirst = 0, failNext = 0;
    unsigned failOpen = 0, failSuspend = 0, failContext = 0;
    bool persistentResumeFailure = false, terminated = false;
    unsigned failResume = 0;
    uintptr_t instruction = 0;
} state;

void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
size_t PeerIndex(HANDLE handle) { return reinterpret_cast<uintptr_t>(handle) - 0x10000; }
HANDLE WINAPI Snapshot(DWORD flags, DWORD pid) {
    if (flags != TH32CS_SNAPTHREAD || pid) std::abort();
    if (++state.snapshots == state.failSnapshot) return INVALID_HANDLE_VALUE;
    state.active = state.snapshots - 1;
    return reinterpret_cast<HANDLE>(state.snapshots);
}
void Entry(LPTHREADENTRY32 entry) {
    entry->th32OwnerProcessID = GetCurrentProcessId();
    entry->th32ThreadID = state.ids[state.active][state.cursor];
}
BOOL WINAPI First(HANDLE, LPTHREADENTRY32 entry) {
    state.cursor = 0;
    if (state.snapshots == state.failFirst || !state.counts[state.active]) {
        SetLastError(ERROR_NO_MORE_FILES); return FALSE;
    }
    Entry(entry); return TRUE;
}
BOOL WINAPI Next(HANDLE, LPTHREADENTRY32 entry) {
    if (state.snapshots == state.failNext) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    if (++state.cursor == state.counts[state.active]) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
    Entry(entry); return TRUE;
}
HANDLE WINAPI Open(DWORD access, BOOL inherit, DWORD id) {
    if (access != (THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION) || inherit) std::abort();
    if (++state.opens == state.failOpen) return nullptr;
    return reinterpret_cast<HANDLE>(0x10000 + id - GetCurrentThreadId());
}
DWORD WINAPI Suspend(HANDLE handle) {
    const size_t index = PeerIndex(handle);
    if (state.opens == state.failSuspend) return DWORD(-1);
    ++state.suspends[index]; return 0;
}
BOOL WINAPI Context(HANDLE, LPCONTEXT context) {
    if (context->ContextFlags != CONTEXT_CONTROL) std::abort();
    if (++state.contexts == state.failContext) return FALSE;
    context->Eip = static_cast<DWORD>(state.instruction); return TRUE;
}
DWORD WINAPI Resume(HANDLE handle) {
    const size_t index = PeerIndex(handle);
    state.resumeOrder[state.totalResumes++] = index;
    ++state.resumes[index];
    if (state.totalResumes == state.failResume || state.persistentResumeFailure) return DWORD(-1);
    return 1;
}
DWORD WINAPI Wait(HANDLE, DWORD timeout) {
    if (timeout) std::abort();
    return state.terminated ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}
BOOL WINAPI Close(HANDLE handle) {
    const auto value = reinterpret_cast<uintptr_t>(handle);
    if (value <= 2) ++state.snapshotCloses[value - 1];
    else ++state.closes[PeerIndex(handle)];
    SetLastError(ERROR_INVALID_HANDLE); // Cleanup must preserve transaction diagnostics.
    return TRUE;
}
const rebirths::ThreadApi api{Snapshot, First, Next, Open, Suspend, Context, Resume, Wait, Close};
const rebirths::InstructionRange ranges[]{{0x1000, 5}, {0x2000, 6}};

void Reset(size_t peers = 3) {
    state = {};
    for (size_t list = 0; list < 2; ++list) {
        state.counts[list] = peers + 1;
        for (size_t i = 0; i <= peers; ++i) state.ids[list][i] = GetCurrentThreadId() + static_cast<DWORD>(i);
    }
}
unsigned checks = 0;
unsigned Run(rebirths::FreezeFailure expected, bool release = true) {
    rebirths::FreezeFailure result;
    unsigned failures = 0;
    allocations = 0;
    forbidAllocation = true; // Simulate exhaustion before even constructing scratch storage.
    {
        rebirths::ThreadFreeze freeze(api);
        result = freeze.Acquire(ranges, 2);
        if (release && result == rebirths::FreezeFailure::None) failures = freeze.Release();
        SetLastError(0x21345678);
    }
    forbidAllocation = false;
    Require(!allocations, "freeze attempted a C++ heap allocation");
    Require(GetLastError() == 0x21345678, "cleanup changed diagnostic");
    Require(result == expected, "unexpected freeze result");
    for (size_t i = 1; i <= state.opens; ++i) {
        Require(state.closes[i] == (i == state.failOpen ? 0u : 1u), "peer handle ownership");
        Require(state.resumes[i] >= state.suspends[i], "suspended peer omitted from cleanup");
        Require(!state.suspends[i] || state.resumes[i] <= 2, "unbounded resume retries");
    }
    for (size_t i = 0; i < state.snapshots; ++i)
        Require(state.snapshotCloses[i] == (i + 1 == state.failSnapshot ? 0u : 1u), "snapshot ownership");
    ++checks;
    return failures;
}
}

int main() {
    try {
        using Failure = rebirths::FreezeFailure;
        Reset(); Run(Failure::None);
        Require(state.totalResumes == 3 && state.resumeOrder[0] == 3 &&
                state.resumeOrder[1] == 2 && state.resumeOrder[2] == 1, "reverse resumption");
        Reset(); Run(Failure::None, false); // Destructor owns normal early exits too.
        Reset(0); Run(Failure::None);
        Reset(Limit - 1); Run(Failure::None);
        Reset(Limit); Run(Failure::Enumeration);
        Require(!state.opens, "overflow must reject before suspension");
        Reset(); state.counts[1] = Limit + 1;
        for (size_t i = 0; i <= Limit; ++i) state.ids[1][i] = GetCurrentThreadId() + static_cast<DWORD>(i);
        Run(Failure::Changed);
        for (unsigned pass = 1; pass <= 2; ++pass) {
            Reset(); state.failSnapshot = pass; Run(pass == 1 ? Failure::Enumeration : Failure::Changed);
            Reset(); state.failFirst = pass; Run(pass == 1 ? Failure::Enumeration : Failure::Changed);
            Reset(); state.failNext = pass; Run(pass == 1 ? Failure::Enumeration : Failure::Changed);
        }
        Reset(); state.counts[0] = 0; Run(Failure::Enumeration);
        Reset(); state.ids[0][0] = GetCurrentThreadId() + 4; Run(Failure::Enumeration);
        Reset(); state.ids[0][2] = state.ids[0][1]; Run(Failure::Enumeration);
        Reset(); state.ids[1][2] = state.ids[1][1]; Run(Failure::Changed);
        Reset(); ++state.counts[1]; state.ids[1][4] = GetCurrentThreadId() + 4; Run(Failure::Changed);
        Reset(); --state.counts[1]; Run(Failure::Changed);
        Reset(); state.ids[1][3] = GetCurrentThreadId() + 4; Run(Failure::Changed);
        Reset(); std::swap(state.ids[0][0], state.ids[0][3]); Run(Failure::None);
        for (unsigned peer = 1; peer <= 3; ++peer) {
            Reset(); state.failOpen = peer; Run(Failure::Open);
            Reset(); state.failSuspend = peer; Run(Failure::Suspend);
            Reset(); state.failContext = peer; Run(Failure::Context);
        }
        for (const auto& range : ranges) {
            for (size_t offset = 0; offset < range.size; ++offset) {
                Reset(); state.instruction = range.start + offset; Run(Failure::InSite);
            }
            Reset(); state.instruction = range.start - 1; Run(Failure::None);
            Reset(); state.instruction = range.start + range.size; Run(Failure::None);
        }
        Reset(); state.failResume = 1; Require(Run(Failure::None) == 1, "explicit resume failure");
        Require(state.resumes[3] == 2 && state.resumes[1] == 1, "retry only failed peer");
        Reset(); state.persistentResumeFailure = true;
        Require(Run(Failure::None) == 3, "persistent failures counted");
        Require(state.totalResumes == 6, "persistent failure bounded retry");
        Reset(); state.persistentResumeFailure = state.terminated = true;
        Require(Run(Failure::None) == 0 && state.totalResumes == 3, "terminated peers accounted for");
        std::printf("Thread freeze OK: %u bounded allocation-free contracts, overflow/membership/IP/platform faults, reverse cleanup and retained diagnostics\n", checks);
        return 0;
    } catch (const std::exception& error) {
        forbidAllocation = false;
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
