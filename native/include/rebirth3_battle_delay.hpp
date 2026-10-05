#pragma once

#include "rebirths_patch.hpp"

namespace rebirths {

// Re;Birth3 only. The one-frame wait is active only during battle loading.
bool InstallRebirth3BattleDelay(const Context& context) noexcept;

} // namespace rebirths
