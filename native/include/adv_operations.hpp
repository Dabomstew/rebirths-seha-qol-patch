#pragma once

#include <Windows.h>

namespace rebirths::adv_operations {

// The adapter validates the record and its independently verified owner/mode.
// These are the four games' identical native terminal stores. Inactive records
// (including negative zero) receive no writes; no resource state is changed.
inline void FinishTrack(float* record) noexcept {
    if (record[2] != 0.0f) {
        record[0] = record[1];
        record[2] = 0.0f;
        record[3] = record[1];
    }
}

// Eligibility is captured by the adapter BEFORE the native call. Always run
// that call, retaining its effects, output parameters, ABI and nonzero results.
template<class Native, class... Args>
inline int PollAndAdvance(bool eligible, Native native, Args... args) noexcept {
    const int result = native(args...);
    return eligible && result == 0 ? 1 : result;
}

// The adapter captures eligibility for negative cleanup from initial state 0.
// Poll at most twice, and only repeat the native pending -> state 1 transition.
template<class Poll>
inline int PollCleanup(bool advance, const int* state, Poll poll) noexcept {
    int result = poll();
    if (advance && result == 0 && *state == 1) result = poll();
    return result;
}

// Used by the newer games after native delay initialization. RB1's different
// producer remains in its adapter. Completion retains the adapter's track guard
// (or RB2's chapter-owned completion) and native state-one advancement.
template<class Poll, class Complete>
inline int PollDelay(bool complete, const int* state, Poll poll, Complete finish) noexcept {
    int result = poll();
    if (complete && result == 0 && *state == 1) {
        finish();
        result = poll();
    }
    return result;
}

struct EpochAtomics {
    static LONG CompareExchange(volatile LONG* pending, LONG value, LONG expected) noexcept {
        return InterlockedCompareExchange(pending, value, expected);
    }
};

// Consume before a native transition. 1 is a new request; 2 is an owned movie
// resumption. Request arming/reset, ownership, manual toggles and prompt handling
// stay game-specific. RB3 consumes only state 1 with its original single CAS.
template<bool AllowResumption, class Atomics = EpochAtomics>
inline LONG ConsumeEpoch(volatile LONG& epochPending) noexcept {
    if constexpr (AllowResumption) {
        const LONG pending = Atomics::CompareExchange(&epochPending, 0, 0);
        if ((pending != 1 && pending != 2) ||
            Atomics::CompareExchange(&epochPending, 0, pending) != pending) return 0;
        return pending;
    } else {
        return Atomics::CompareExchange(&epochPending, 0, 1) == 1 ? 1 : 0;
    }
}

} // namespace rebirths::adv_operations
