#pragma once
#include "rebirths_patch.hpp"
namespace rebirths {
// Loader-lock bootstrap only redirects a validated per-game import slot.
// Identity, configuration, hashing and bank writes run on the subsequent call.
void BootstrapAudioTail(HMODULE proxy) noexcept;
void DetachAudioTail() noexcept;
void LogAudioTail(const Context& context) noexcept;
}
