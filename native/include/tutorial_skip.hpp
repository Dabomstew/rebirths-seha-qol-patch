#pragma once
#include "rebirths_patch.hpp"

namespace rebirths {
struct TutorialPatchOps {
    bool (*retargetCalls)(const Context&, const CallSite*, size_t) noexcept;
};
bool InstallSkipTutorials(const Context&, const TutorialPatchOps* = nullptr) noexcept;
}
