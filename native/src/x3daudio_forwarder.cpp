#include "rebirths_patch.hpp"
#include "audio_tail.hpp"
#include "voice_tail.hpp"

#include <cstdint>
#include <mutex>
#include <string>

namespace {
HMODULE proxyModule = nullptr;
HMODULE original = nullptr;
std::once_flag loadOnce;
std::once_flag initializeOnce;

FARPROC Resolve(const char* name) {
    std::call_once(loadOnce, [] {
        wchar_t directory[MAX_PATH];
        const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
        if (length && length < MAX_PATH) {
            original = LoadLibraryW((std::wstring(directory) + L"\\X3DAudio1_7.dll").c_str());
        }
    });
    std::call_once(initializeOnce, [] { rebirths::Initialize(proxyModule); });
    return original ? GetProcAddress(original, name) : nullptr;
}
}

extern "C" HRESULT __cdecl ProxyX3DAudioInitialize(
    uint32_t mask, float speed, unsigned char* handle) {
    using Function = HRESULT (__cdecl*)(uint32_t, float, unsigned char*);
    const auto function = reinterpret_cast<Function>(Resolve("X3DAudioInitialize"));
    return function ? function(mask, speed, handle) : E_FAIL;
}

extern "C" void __cdecl ProxyX3DAudioCalculate(
    const unsigned char* handle, const void* listener, const void* emitter,
    uint32_t flags, void* settings) {
    using Function = void (__cdecl*)(const unsigned char*, const void*, const void*, uint32_t, void*);
    const auto function = reinterpret_cast<Function>(Resolve("X3DAudioCalculate"));
    if (function) {
        function(handle, listener, emitter, flags, settings);
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        proxyModule = instance;
        rebirths::BootstrapAudioTail(instance);
        rebirths::BootstrapVoiceTail(instance);
    }
    if (reason == DLL_PROCESS_DETACH) {
        rebirths::DetachVoiceTail();
        rebirths::DetachAudioTail();
    }
    return TRUE;
}
