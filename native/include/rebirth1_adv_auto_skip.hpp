#pragma once

#include "owned_patch_install.hpp"

namespace rebirths {

using AdvAutoSkipPatchOps = owned_patch::PatchOps;

// Installs the separately guarded RB1 story-ADV input-boundary wrappers.
// The option never simulates keyboard or controller input; each wrapper uses
// the executable's normal native skip-state transition at most once per
// observed event epoch.
bool InstallRebirth1AdvAutoSkip(const Context& context, const AdvAutoSkipPatchOps* ops = nullptr) noexcept;
bool InstallAdvAutoSkip(const Context& context, const AdvAutoSkipPatchOps* ops = nullptr) noexcept;

} // namespace rebirths
