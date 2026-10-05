#pragma once

#include "rebirth1_adv_fast_forward.hpp"

namespace rebirths {

bool InstallRebirth2SkipChapterIntros(const Context& context,
                                    const AdvFastForwardPatchOps* ops = nullptr) noexcept;

bool InstallRebirth2AdvFastForward(const Context& context,
                                   const AdvFastForwardPatchOps* ops = nullptr) noexcept;

}
