#include "rebirths_patch.hpp"
#include "prepare_feature_settings.hpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <stdexcept>

using namespace rebirths;
static_assert(static_cast<uint32_t>(GameId::Rebirth1) == 0);
static_assert(static_cast<uint32_t>(GameId::Rebirth2) == 1);
static_assert(static_cast<uint32_t>(GameId::Rebirth3) == 2);
static_assert(static_cast<uint32_t>(GameId::SegaHardGirls) == 3);

int main() {
    // Independent policy oracle: never generate it from catalog.json.
    struct Expected { FeatureId id; const wchar_t* key; unsigned mask; int runtime, fresh, existing; bool managed; };
    const Expected expected[] = {
        {FeatureId::TrimSilentAudioTails, L"TrimSilentAudioTails", 7, 1, 1, 1, false},
        {FeatureId::UncompressedAssets, L"UncompressedAssets", 15, 0, 1, 0, true},
        {FeatureId::FastTextureConversion, L"FastTextureConversion", 15, 1, 1, 1, true},
        {FeatureId::AdvFastForward, L"AdvFastForward", 15, 0, 0, 0, true},
        {FeatureId::AdvAutoSkip, L"AdvAutoSkip", 15, 0, 0, 0, true},
        {FeatureId::NepstationSkip, L"NepstationSkip", 4, 0, 0, 0, true},
        {FeatureId::SkipTutorials, L"SkipTutorials", 15, 1, 1, 1, true},
        {FeatureId::BattleLoadDelaySkip, L"BattleLoadDelaySkip", 4, 0, 0, 0, true},
        {FeatureId::SkipChapterIntros, L"SkipChapterIntros", 2, 0, 0, 0, true},
        {FeatureId::DungeonMovementFix, L"DungeonMovementFix", 8, 1, 1, 1, true},
        {FeatureId::FastFaceTextureCreation, L"FastFaceTextureCreation", 12, 0, 0, 0, false},
    };
    assert(Features.size() == std::size(expected));
    prepare::Settings settings;
    size_t checks = 0;
    for (const auto& e : expected) {
        const auto& feature = FeatureSpecFor(e.id);
        assert(std::wcscmp(feature.key, e.key) == 0);
        assert(feature.gameMask == e.mask && feature.runtimeDefault == e.runtime);
        assert(feature.freshDefault == e.fresh && feature.existingDefault == e.existing);
        assert((feature.preparerPolicy == PreparerPolicy::Managed) == e.managed);
        if (e.managed) assert(prepare::SettingValue(settings, e.id) == (e.fresh != 0));
        else {
            bool rejected = false;
            try { prepare::SettingValue(settings, e.id); }
            catch (const std::invalid_argument&) { rejected = true; }
            assert(rejected && !feature.uiLabel);
        }
        for (uint32_t id : {0u, 1u, 2u, 3u, 4u, 32u, 0xffffffffu}) {
            const auto game = static_cast<GameId>(id);
            const bool supported = id < 4 && (e.mask & (1u << id)) != 0;
            assert(Supports(feature, game) == supported);
            for (int value : {-1, 0, 1}) {
                int reads = 0;
                const bool enabled = ReadRuntimeFeature(game, e.id, [&](const wchar_t* key, int fallback) {
                    ++reads;
                    assert(std::wcscmp(key, e.key) == 0 && fallback == e.runtime);
                    return value < 0 ? fallback : value;
                });
                assert(enabled == (supported && (value < 0 ? e.runtime : value) != 0));
                assert(reads == (supported ? 1 : 0));
                ++checks;
            }
        }
    }
    for (uint32_t id : {4u, 32u, 0xffffffffu}) {
        const auto game = static_cast<GameId>(id);
        assert(!FindGameSpec(game));
        bool rejected = false;
        try { GameSpecFor(game); } catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }
    bool rejected = false;
    try { FeatureSpecFor(static_cast<FeatureId>(99)); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    std::printf("%zu independent runtime feature cases plus settings/invalid-ID contracts passed\n", checks);
}
