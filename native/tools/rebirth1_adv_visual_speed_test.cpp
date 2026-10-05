#include "rebirth1_adv_visual_speed.hpp"
#include <cassert>
#include <cmath>

int main() {
    float active[] = {0.25f, 0.75f, 0.10f, 0.25f};
    rebirths::CompleteActiveInterpolation(active);
    assert(active[0] == 0.75f && active[1] == 0.75f && active[2] == 0.0f && active[3] == 0.75f);

    float inactive[] = {0.25f, 0.75f, 0.0f, 0.125f};
    rebirths::CompleteActiveInterpolation(inactive);
    assert(inactive[0] == 0.25f && inactive[1] == 0.75f && inactive[2] == 0.0f && inactive[3] == 0.125f);

    // The helper must not dereference a malformed replacement call argument.
    rebirths::CompleteActiveInterpolation(nullptr);

    float scaled[] = {0.0f, 1.0f, -0.25f, 0.0f};
    rebirths::CompleteActiveInterpolationScaled(scaled, 2.0f);
    assert(scaled[0] == 1.0f && scaled[2] == 0.0f && scaled[3] == 1.0f);
    return 0;
}
