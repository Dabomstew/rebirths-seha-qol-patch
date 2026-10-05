#pragma once

#include "owned_patch_install.hpp"

namespace rebirths {

// Internal injection seam for offline tests. Production passes no operations
// and therefore uses the guarded patch primitives directly.
using AdvFastForwardPatchOps = owned_patch::PatchOps;

bool InstallRebirth1AdvFastForward(const Context& context, const AdvFastForwardPatchOps* ops = nullptr) noexcept;
bool InstallAdvFastForward(const Context& context, const AdvFastForwardPatchOps* ops = nullptr) noexcept;

}
