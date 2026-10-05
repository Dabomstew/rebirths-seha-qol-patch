#include "feature_catalog.hpp"
#include "audio_tail.hpp"
#include "voice_tail.hpp"
#include "rebirth1_adv_fast_forward.hpp"
#include "rebirth1_adv_auto_skip.hpp"
#include "rebirth2_adv_fast_forward.hpp"
#include "uncompressed_assets.hpp"
#include "sega_dungeon_movement_fix.hpp"
#include "rebirth3_battle_delay.hpp"
#include "fast_texture_conversion.hpp"
#include "fast_face_texture_creation.hpp"
#include "tutorial_skip.hpp"
#include "rebirth3_adv_auto_skip.hpp"
#include <cstdarg>
#include <cstdio>
#include <share.h>
#include <mutex>
#include <stdexcept>

namespace rebirths {
namespace {
std::wstring logPath;
std::mutex logMutex;
}

void Log(const char* format, ...) noexcept {
    try {
        std::lock_guard<std::mutex> lock(logMutex);
        if (logPath.empty()) return;
        char message[512];
        va_list args; va_start(args, format);
        vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
        va_end(args);
        // Permit read-only observers; the secure fopen default can lose a log
        // line merely because a reader overlaps this append. Keep other writers
        // excluded, with our own threads serialized by logMutex.
        FILE* output = _wfsopen(logPath.c_str(), L"ab", _SH_DENYWR);
        if (!output) return;
        std::fprintf(output, "%llu %s\n", GetTickCount64(), message);
        std::fclose(output);
    } catch (...) {}
}

int Option(const Context& context, const wchar_t* section, const wchar_t* key, int fallback) {
    return GetPrivateProfileIntW(section, key, fallback, context.ini.c_str());
}

void Initialize(HMODULE proxy) noexcept {
    try {
        const std::wstring path = ModulePath(proxy);
        const std::wstring directory = path.substr(0, path.find_last_of(L"\\/"));
        logPath = directory + L"\\rebirths-patches.log";
        Log("Rebirths / Sega Hard Girls QoL Patch %s", PATCH_RELEASE_VERSION);
        const HMODULE game = GetModuleHandleW(nullptr);
        const Digest digest = HashFile(ModulePath(game));
        const GameSpec* spec = IdentifyGame(digest);
        if (!spec) {
            char hexadecimal[65]{};
            for (size_t index = 0; index < digest.size(); ++index)
                std::snprintf(hexadecimal + index * 2, 3, "%02x", digest[index]);
            Log("unsupported executable SHA-256=%s; patches disabled", hexadecimal);
            return;
        }
        Context context{game, *spec, directory, directory + L"\\rebirths-patches.ini"};
        Log("recognized target game=%s", GameIdName(spec->id));
        LogAudioTail(context);
        LogVoiceTail(context);
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                reinterpret_cast<LPCWSTR>(proxy), &pinned))
            throw std::runtime_error("cannot pin proxy");
        if (!ValidGameId(spec->id)) {
            Log("recognized target has no validated ADV installers; patches disabled");
            return;
        }
        const auto enabled = [&](FeatureId feature) {
            return ReadRuntimeFeature(spec->id, feature, [&](const wchar_t* key, int fallback) {
                return Option(context, L"Patches", key, fallback);
            });
        };
        if (enabled(FeatureId::UncompressedAssets)) InstallUncompressedAssets(context);
        else Log("UncompressedAssets disabled");
        if (enabled(FeatureId::FastTextureConversion))
            InstallFastTextureConversion(context);
        else Log("FastTextureConversion disabled or unsupported");
        if (enabled(FeatureId::AdvFastForward)) InstallAdvFastForward(context);
        else Log("AdvFastForward disabled");
        if (enabled(FeatureId::SkipTutorials)) InstallSkipTutorials(context);
        else Log("SkipTutorials disabled");
        if (enabled(FeatureId::AdvAutoSkip)) InstallAdvAutoSkip(context);
        else Log("AdvAutoSkip disabled");
        if (enabled(FeatureId::NepstationSkip)) InstallRebirth3NepstationSkip(context);
        else Log("NepstationSkip disabled or unsupported");
        if (enabled(FeatureId::BattleLoadDelaySkip))
            InstallRebirth3BattleDelay(context);
        else Log("BattleLoadDelaySkip disabled or unsupported");
        if (Supports(FeatureId::FastFaceTextureCreation, spec->id)) {
            if (enabled(FeatureId::FastFaceTextureCreation)) InstallFastFaceTextureCreation(context);
            else Log("FastFaceTextureCreation disabled");
        }
        if (Supports(FeatureId::DungeonMovementFix, spec->id)) {
            if (enabled(FeatureId::DungeonMovementFix)) InstallSegaDungeonMovementFix(context);
            else Log("DungeonMovementFix disabled");
        }
        if (enabled(FeatureId::SkipChapterIntros))
            InstallRebirth2SkipChapterIntros(context);
        else Log("SkipChapterIntros disabled or unsupported");
    } catch (const std::exception& error) { Log("initialization failed: %s", error.what()); }
    catch (...) { Log("initialization failed"); }
}

}
