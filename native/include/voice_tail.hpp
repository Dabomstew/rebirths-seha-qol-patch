#pragma once
#include "rebirths_patch.hpp"
namespace rebirths {
void BootstrapVoiceTail(HMODULE proxy) noexcept;
void DetachVoiceTail() noexcept;
void LogVoiceTail(const Context& context) noexcept;
#ifdef REBIRTHS_TEST_CONTRACTS
namespace testing {
void ConfigureVoiceRead(HANDLE file, const unsigned char* original, const unsigned char* corrected, size_t prefix, bool active);
BOOL WINAPI ReadVoiceTest(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED) noexcept;
void* VoiceInitializeAdapterAddress() noexcept;
}
#endif
}
