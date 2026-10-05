#include "rebirth1_adv_visual_speed.hpp"

namespace rebirths {

extern "C" void __cdecl CompleteActiveInterpolation(float* record) noexcept {
    // The verified helper returns without writes for zero velocity. Preserve
    // that behavior so already-inactive records keep their original final-copy
    // word. Nonzero records receive exactly its observed terminal stores.
    if (!record || record[2] == 0.0f) return;
    const float target = record[1];
    record[0] = target;
    record[2] = 0.0f;
    record[3] = target;
}

extern "C" void __cdecl CompleteActiveInterpolationScaled(float* record, float) noexcept {
    CompleteActiveInterpolation(record);
}

}
