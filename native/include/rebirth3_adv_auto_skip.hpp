#pragma once

#include "rebirth1_adv_auto_skip.hpp"

namespace rebirths {

// Re;Birth3-only implementation. Addresses and object layout are validated
// independently from every other supported executable.
bool InstallRebirth3AdvAutoSkip(const Context& context, const AdvAutoSkipPatchOps* ops = nullptr) noexcept;

bool InstallRebirth3NepstationSkip(const Context& context, const AdvAutoSkipPatchOps* ops = nullptr) noexcept;

} // namespace rebirths
