#pragma once

#include "rebirths_patch.hpp"

namespace rebirths {

struct DungeonMovementFixPatchOps {
    bool (*retargetBytes)(const Context&, uint32_t, const unsigned char*,
                          const unsigned char*, size_t) noexcept;
};

bool InstallSegaDungeonMovementFix(const Context& context,
                                   const DungeonMovementFixPatchOps* ops = nullptr) noexcept;

}
