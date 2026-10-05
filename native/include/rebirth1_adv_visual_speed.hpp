#pragma once

namespace rebirths {

// ABI-compatible replacement for the verified one-argument cdecl calls to
// Re;Birth1's 0x00439900 interpolation helper. It intentionally changes only
// the four-float record passed by the original callback.
extern "C" void __cdecl CompleteActiveInterpolation(float* record) noexcept;

// ABI-compatible replacement for 0x00439860, whose second cdecl argument is
// an interpolation multiplier. Terminalization deliberately ignores it.
extern "C" void __cdecl CompleteActiveInterpolationScaled(float* record, float multiplier) noexcept;

}
