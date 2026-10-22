#pragma once
#include "rebirths_patch.hpp"
#include "load_timing_core.hpp"
namespace rebirths {
// Family-specific ABI1. Only completed confirmed intervals are exported; no
// consumer may extrapolate loading through missing owner callbacks.
struct LoadTimingPayload {
    uint32_t status, coverage, reason, activeOwners;
    uint32_t generation, transitions, focused, fault;
    uint32_t reserved[4];
    uint64_t frequency, completedTicks, openSinceQpc, lastQpc;
    uint32_t reserved2[2];
    uint64_t reserved3[2];
};
struct alignas(8) LoadTimingState { volatile LONG sequence=0; uint32_t padding=0; LoadTimingPayload data{}; };
struct LoadTimingDescriptor { uint32_t magic0, magic1, version, bytes; LoadTimingState* state; uint32_t stateBytes; };
constexpr uint32_t LoadMagic0=0x544c4252; // RBLT
constexpr uint32_t LoadMagic1=0x31303030; // 0001
static_assert(sizeof(LoadTimingPayload)==104 && sizeof(LoadTimingState)==112);
static_assert(sizeof(LoadTimingDescriptor)==24);
static_assert(offsetof(LoadTimingState,data)==8 && offsetof(LoadTimingPayload,frequency)==48);
bool InstallLoadTiming(const Context&) noexcept;
}
