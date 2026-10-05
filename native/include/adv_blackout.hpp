#pragma once

#include "rebirths_patch.hpp"

namespace rebirths {

// Shared by the normal proxy and the explicitly loaded experiment.  The host
// supplies both the accepted-ADV predicate and the saved IAT target, so an
// incomplete installation can never clear a frame.
using AdvBlackoutGuard = bool (*)() noexcept;
using AdvWindowSkinFn = void (__cdecl*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using AdvBlackoutInfoHandle = uintptr_t (*)() noexcept;
void ConfigureAdvBlackout(uintptr_t base, BOOL (WINAPI* original)(HDC), AdvBlackoutGuard guard,
                          AdvWindowSkinFn originalWindowSkin = nullptr,
                          AdvBlackoutInfoHandle infoHandle = nullptr) noexcept;
void AdvBlackoutBeforeWindow(uintptr_t widget) noexcept;
BOOL WINAPI AdvBlackoutPresent(HDC device) noexcept;
void __cdecl AdvBlackoutWindowSkin(uintptr_t widget, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4) noexcept;
std::array<unsigned char, 6> AdvBlackoutExpectedCall(uintptr_t base, uint32_t iatRva = 0x32f04c) noexcept;
std::array<unsigned char, 6> AdvBlackoutReplacement(uintptr_t source) noexcept;

} // namespace rebirths
