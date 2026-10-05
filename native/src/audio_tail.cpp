#include "audio_tail.hpp"
#include "audio_tail_profiles.hpp"
#include "platform_util.hpp"
#include "feature_catalog.hpp"
#include <intrin.h>
#include <mutex>
#include <iterator>

namespace rebirths {
namespace {
const audio_tail::Profile* profile = nullptr;
using MapFunction = LPVOID (WINAPI*)(HANDLE, DWORD, DWORD, DWORD, SIZE_T);
MapFunction originalMap = nullptr;
void** importSlot = nullptr;
HMODULE proxyModule = nullptr, gameModule = nullptr;
bool bootstrapUsable = false;
std::once_flag qualifyOnce;
bool enabled = false;
const char* result = "mapping not observed";
size_t corrected = 0;

bool ReadableBank(void* view) noexcept {
    auto* current = static_cast<unsigned char*>(view);
    size_t remaining = profile->bankSize;
    while (remaining) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(current, &info, sizeof(info)) || info.State != MEM_COMMIT ||
            info.Type != MEM_MAPPED || info.AllocationBase != view ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const size_t offset = current - static_cast<unsigned char*>(info.BaseAddress);
        if (offset >= info.RegionSize) return false;
        const size_t available = static_cast<size_t>(info.RegionSize) - offset;
        const size_t amount = remaining < available ? remaining : available;
        current += amount; remaining -= amount;
    }
    return true;
}

// No C++ owners in SEH leaves. A failed write restores every guarded word.
bool WriteBank(void* view) noexcept {
    __try {
        if (!audio_tail::Validate(view, profile->bankSize, profile->words, profile->wordCount))
            return false;
        audio_tail::Write(view, profile->words, profile->wordCount);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        __try { audio_tail::Write(view, profile->words, profile->wordCount, true); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }
}

void Qualify() noexcept {
    try {
        const GameSpec* spec = IdentifyGame(HashFile(ModulePath(gameModule)));
        if (!spec || spec->id != profile->game) { result = "executable identity rejected"; return; }
        const auto path = ModulePath(proxyModule);
        const auto directory = path.substr(0, path.find_last_of(L"\\/"));
        Context context{gameModule, *spec, directory, directory + L"\\rebirths-patches.ini"};
        enabled = ReadRuntimeFeature(spec->id, FeatureId::TrimSilentAudioTails,
            [&](const wchar_t* key, int fallback) { return Option(context, L"Patches", key, fallback); });
        if (!enabled) { result = "disabled"; return; }
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(proxyModule), &pinned)) {
            enabled = false; result = "proxy pin failed";
        }
    } catch (...) { enabled = false; result = "qualification failed"; }
}

LPVOID WINAPI MapBank(HANDLE mapping, DWORD access, DWORD high, DWORD low, SIZE_T length) noexcept {
    if (!originalMap) return nullptr;
    const auto caller = reinterpret_cast<unsigned char*>(_ReturnAddress());
    if (!bootstrapUsable || caller != reinterpret_cast<unsigned char*>(gameModule) + profile->mapCall + 6 ||
        access != FILE_MAP_READ || high || low || length)
        return originalMap(mapping, access, high, low, length);
    try { std::call_once(qualifyOnce, Qualify); }
    catch (...) { result = "qualification synchronization failed"; return originalMap(mapping, access, high, low, length); }
    if (!enabled) return originalMap(mapping, access, high, low, length);
    void* view = originalMap(mapping, FILE_MAP_COPY, high, low, length);
    if (!view) { result = "copy mapping failed; original retained"; return originalMap(mapping, access, high, low, length); }
    try {
        if (!ReadableBank(view)) { result = "mapping bounds rejected"; return view; }
        platform::Sha hash; hash.Add(view, profile->bankSize);
        if (hash.Finish() != platform::Unhex(profile->bankHash)) {
            result = "bank identity rejected"; return view;
        }
        if (!WriteBank(view)) {
            // Never pass a possibly partial mutation to XACT; remap pristine bytes.
            UnmapViewOfFile(view); result = "word guard/write rejected; original retained";
            return originalMap(mapping, access, high, low, length);
        }
        corrected = profile->correctedWaves; result = "private packet correction applied";
    } catch (...) { result = "bank qualification failed; unchanged"; }
    return view;
}
}

void BootstrapAudioTail(HMODULE proxy) noexcept {
    // Only fixed-size kernel calls and guarded memory access under loader lock.
    // No hashing, allocation, configuration reads, loading, logging or freezing.
    wchar_t filename[MAX_PATH]{};
    HMODULE game = GetModuleHandleW(nullptr);
    const DWORD length = GetModuleFileNameW(game, filename, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    const wchar_t* leaf = filename;
    for (const wchar_t* p = filename; *p; ++p) if (*p == L'\\' || *p == L'/') leaf = p + 1;
    for (const auto& candidate : audio_tail::Profiles)
        if (!lstrcmpiW(leaf, candidate.executable)) { profile = &candidate; break; }
    if (!profile) return;
    __try {
        auto* bytes = reinterpret_cast<unsigned char*>(game);
        const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096) return;
        const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            nt->OptionalHeader.SizeOfImage <= profile->mapIat + sizeof(void*) ||
            nt->OptionalHeader.SizeOfImage < profile->mapCall + 6) return;
        if (bytes[profile->mapCall] != 0xff || bytes[profile->mapCall + 1] != 0x15 ||
            *reinterpret_cast<uint32_t*>(bytes + profile->mapCall + 2) != reinterpret_cast<uint32_t>(bytes + profile->mapIat)) return;
        auto** slot = reinterpret_cast<void**>(bytes + profile->mapIat);
        const auto expected = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "MapViewOfFile");
        if (!expected || *slot != reinterpret_cast<void*>(expected)) return;
        originalMap = reinterpret_cast<MapFunction>(expected);
        gameModule = game; proxyModule = proxy;
        DWORD previous = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous)) return;
        void* observed = InterlockedCompareExchangePointer(slot, reinterpret_cast<void*>(&MapBank), reinterpret_cast<void*>(expected));
        DWORD ignored = 0;
        const bool restored = VirtualProtect(slot, sizeof(void*), previous, &ignored) != FALSE;
        if (observed == reinterpret_cast<void*>(expected)) { importSlot = slot; bootstrapUsable = restored; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void DetachAudioTail() noexcept {
    if (!importSlot) return;
    DWORD previous = 0;
    if (VirtualProtect(importSlot, sizeof(void*), PAGE_READWRITE, &previous)) {
        InterlockedCompareExchangePointer(importSlot, reinterpret_cast<void*>(originalMap), reinterpret_cast<void*>(&MapBank));
        DWORD ignored = 0; VirtualProtect(importSlot, sizeof(void*), previous, &ignored);
    }
}

void LogAudioTail(const Context& context) noexcept {
    if (profile && context.spec.id == profile->game)
        Log("TrimSilentAudioTails bootstrap=%d result=%s waves=%zu", bootstrapUsable, result, corrected);
}
}
