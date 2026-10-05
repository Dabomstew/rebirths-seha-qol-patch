#pragma once
#include "rebirths_patch.hpp"

namespace rebirths {
struct FaceTexturePatchOps {
    bool (*retargetBytes)(const Context&, uint32_t, const unsigned char*, const unsigned char*, size_t) noexcept;
    bool (*retargetCalls)(const Context&, const CallSite*, size_t) noexcept;
};
bool InstallFastFaceTextureCreation(const Context&, const FaceTexturePatchOps* = nullptr) noexcept;
}
