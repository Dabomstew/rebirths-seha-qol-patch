#include "adv_operations.hpp"

#include <array>
#include <climits>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
using namespace rebirths::adv_operations;
unsigned checks = 0;
void Require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

void InfoWidgets() {
    alignas(uintptr_t) unsigned char manager[0x280]{}, windows[0x100]{}, first[0x100]{}, second[0x100]{};
    uintptr_t globals[2]{reinterpret_cast<uintptr_t>(manager),reinterpret_cast<uintptr_t>(windows)};
    const uintptr_t base=reinterpret_cast<uintptr_t>(globals);
    auto pointer=[](unsigned char* record,size_t offset,uintptr_t value){std::memcpy(record+offset,&value,sizeof(value));};
    for(uint32_t nameOffset:{0x70u,0x74u,0x78u}) {
        InfoWidgetLayout layout{0,sizeof(uintptr_t),0x20,nameOffset};
        pointer(windows,0x20,reinterpret_cast<uintptr_t>(first));
        pointer(first,0x10,reinterpret_cast<uintptr_t>(second));
        std::memset(first+nameOffset,0,16);std::memset(second+nameOffset,0,16);
        std::memcpy(second+nameOffset,"AdvWndInfo",sizeof("AdvWndInfo"));
        Require(InfoWidget(base,layout)==reinterpret_cast<uintptr_t>(second),"list did not find info widget");
        std::memcpy(second+nameOffset,"AdvWndInfoMoney",sizeof("AdvWndInfoMoney"));
        Require(InfoWidget(base,layout)==reinterpret_cast<uintptr_t>(second),"money widget not found");
        pointer(manager,0,1);pointer(manager,0x70,0x12345678);
        *reinterpret_cast<uint32_t*>(manager+0x27c)=1;
        Require(InfoWidget(base,layout)==0x12345678,"inline active widget lost precedence");
        *reinterpret_cast<uint32_t*>(manager+0x27c)=0;
        Require(InfoWidget(base,layout)==reinterpret_cast<uintptr_t>(second),"inactive inline did not fall back");
        *reinterpret_cast<uint32_t*>(manager+0x27c)=1;pointer(manager,0x70,0);
        Require(InfoWidget(base,layout)==reinterpret_cast<uintptr_t>(second),"empty inline did not fall back");
        globals[0]=0;
        Require(InfoWidget(base,layout)==reinterpret_cast<uintptr_t>(second),"absent manager did not fall back");
        std::memcpy(second+nameOffset,"AdvWndInfoOther",sizeof("AdvWndInfoOther"));
        pointer(second,0x10,reinterpret_cast<uintptr_t>(first));
        Require(InfoWidget(base,layout)==0,"cyclic nonmatching list escaped bound");
        pointer(second,0x10,0);
        Require(InfoWidget(base,layout)==0,"nonmatching list accepted name prefix");
        globals[1]=0;Require(InfoWidget(base,layout)==0,"absent windows changed behavior");
        globals[0]=reinterpret_cast<uintptr_t>(manager);globals[1]=reinterpret_cast<uintptr_t>(windows);
    }
}

void Tracks() {
    // Native terminal values are copied exactly, even for NaN/infinity; inactive
    // tracks and unrelated words must retain their original bytes.
    for (unsigned active : {0u, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x7fc12345u}) {
        for (unsigned target : {0x3f000000u, 0x80000000u, 0x7f800000u, 0x7fc54321u}) {
            std::array<unsigned, 6> before{0x12345678u, target, active, 0x98765432u, 0xabcdef01u, 0x24681357u};
            std::array<float, 6> record;
            std::memcpy(record.data(), before.data(), sizeof(record));
            FinishTrack(record.data());
            std::array<unsigned, 6> after;
            std::memcpy(after.data(), record.data(), sizeof(record));
            if (active == 0 || active == 0x80000000u) {
                Require(after == before, "inactive track changed");
            } else {
                Require(after[0] == target && after[1] == target && after[2] == 0 && after[3] == target,
                    "terminal stores lost native float bits");
                Require(after[4] == before[4] && after[5] == before[5], "track escaped four-word range");
            }
        }
    }
}

unsigned nativeCalls = 0;
int nativeResult = 0;
bool liveEligibility = false;
int __cdecl PollNative(unsigned mask, unsigned* output) {
    ++nativeCalls;
    *output = mask ^ 0xa5a5u;
    liveEligibility = !liveEligibility;
    return nativeResult;
}
struct ThiscallNative {
    unsigned calls = 0;
    int __thiscall Poll(int arg) { ++calls; return arg; }
};
void PendingPolls() {
    for (bool eligible : {false, true}) {
        for (int result : {0, 1, -1, 7, INT_MIN, INT_MAX}) {
            nativeCalls = 0;
            nativeResult = result;
            liveEligibility = eligible;
            unsigned output = 0;
            const int actual = PollAndAdvance(liveEligibility, &PollNative, 0x1234u, &output);
            Require(actual == (eligible && result == 0 ? 1 : result), "pending result/error changed");
            Require(nativeCalls == 1 && output == (0x1234u ^ 0xa5a5u), "native call or output lost");
            Require(liveEligibility != eligible, "test native side effect lost");
        }
    }
    ThiscallNative object;
    Require(PollAndAdvance(true, [&object](int arg) { return object.Poll(arg); }, 0) == 1 && object.calls == 1,
        "typed native member call lost");
}

void CleanupPolls() {
    // Independent matrix: capture eligibility before polling, then repeat only
    // negative cleanup from state 0 which returned pending and reached state 1.
    for (bool eligible : {false, true}) for (int operation : {-1, 0, 1})
        for (int initial : {0, 1, 10}) for (int next : {0, 1, 10})
            for (int result : {0, 1, -7}) {
                int state = initial;
                unsigned calls = 0;
                const bool advance = eligible && operation < 0 && state == 0;
                const int actual = PollCleanup(advance, &state, [&]() noexcept {
                    if (++calls == 1) { state = next; return result; }
                    state = 2;
                    return -9; // Second-call errors must be retained too.
                });
                const bool repeat = eligible && operation == -1 && initial == 0 && result == 0 && next == 1;
                Require(calls == (repeat ? 2u : 1u) && actual == (repeat ? -9 : result), "cleanup repeat/result changed");
                Require(state == (repeat ? 2 : next), "cleanup native state effect lost");
            }
}

void DelayPolls() {
    for (bool eligible : {false, true}) for (int next : {0, 1, 2, 10})
        for (int result : {0, 1, -7}) {
            int state = 0;
            unsigned calls = 0, finishes = 0;
            float track[4]{};
            const int actual = PollDelay(eligible, &state, [&]() noexcept {
                if (++calls == 1) {
                    track[0] = 0.0f; track[1] = 2.0f; track[2] = 3.0f; track[3] = 0.0f;
                    state = next;
                    return result;
                }
                Require(track[0] == 2.0f && track[2] == 0.0f && track[3] == 2.0f,
                    "delay repeated before finishing native initialized track");
                state = 2;
                return -9;
            }, [&]() noexcept {
                ++finishes;
                Require(calls == 1 && state == 1, "delay completed before native initialization");
                FinishTrack(track);
            });
            const bool repeat = eligible && next == 1 && result == 0;
            Require(calls == (repeat ? 2u : 1u) && finishes == (repeat ? 1u : 0u), "delay repeat/finish count changed");
            Require(actual == (repeat ? -9 : result) && state == (repeat ? 2 : next), "delay native result/state lost");
        }
}

struct RacedAtomics {
    static unsigned calls;
    static LONG replacement;
    static LONG CompareExchange(volatile LONG* pending, LONG value, LONG expected) noexcept {
        if (++calls == 2) *pending = replacement;
        return InterlockedCompareExchange(pending, value, expected);
    }
};
unsigned RacedAtomics::calls = 0;
LONG RacedAtomics::replacement = 0;
void Epochs() {
    for (LONG initial : {LONG(-1), LONG(0), LONG(1), LONG(2), LONG(3)}) {
        volatile LONG pending = initial;
        const LONG consumed = ConsumeEpoch<true>(pending);
        const bool valid = initial == 1 || initial == 2;
        Require(consumed == (valid ? initial : 0) && pending == (valid ? 0 : initial), "resumable consumption changed");
        Require(ConsumeEpoch<true>(pending) == 0, "resumable epoch consumed twice");
        pending = initial;
        Require(ConsumeEpoch<false>(pending) == (initial == 1 ? 1 : 0) && pending == (initial == 1 ? 0 : initial),
            "RB3 single-CAS consumed an unsupported resumption");
        Require(ConsumeEpoch<false>(pending) == 0, "RB3 request consumed twice");
    }
    for (LONG initial : {LONG(1), LONG(2)}) for (LONG raced : {LONG(0), LONG(1), LONG(2), LONG(3)}) {
        volatile LONG pending = initial;
        RacedAtomics::calls = 0;
        RacedAtomics::replacement = raced;
        const LONG consumed = ConsumeEpoch<true, RacedAtomics>(pending);
        const bool won = initial == raced;
        Require(consumed == (won ? initial : 0) && pending == (won ? 0 : raced), "lost CAS overwrote another epoch");
        Require(RacedAtomics::calls == 2, "resumable CAS sequence changed");
    }
    volatile LONG pending = 1;
    RacedAtomics::calls = 0;
    Require(ConsumeEpoch<false, RacedAtomics>(pending) == 1 && RacedAtomics::calls == 1,
        "RB3 consumption added a read before its single CAS");
}
}

int main() {
    try {
        InfoWidgets(); Tracks(); PendingPolls(); CleanupPolls(); DelayPolls(); Epochs();
        std::printf("ADV operations: %u independent contract checks passed\n", checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
