#include "prepare_install_internal.hpp"
#include "prepare_feature_settings.hpp"

namespace rebirths::prepare {
using namespace install;
Settings ReadSettings(const Game& game) {
    GameSpecFor(static_cast<GameId>(game.id));
    Settings s;
    auto config = SafeBelow(game.directory, game.proxyDirectory / L"rebirths-patches.ini");
    bool existing = fs::exists(config);
    s.configHash = existing ? Hash(config) : "";
    auto assetText =
        Ini(config, L"UncompressedAssets", L"Directory",
            existing ? L"rebirths-speedrun-patch\\assets" : L"rebirths-speedrun-patch\\cg24-v1");
    s.assets = fs::path(assetText);
    if (s.assets.is_relative())
        s.assets = game.directory / s.assets;
    s.transformProfile =
        ReadAssetProfile(game.directory, s.assets, game.id)
            .value_or(existing ? TransformProfile::Raw : TransformProfile::AdvCgHalf24V1);
    for (const auto& binding : FeatureBindings) {
        const auto& feature = FeatureSpecFor(binding.id);
        s.*binding.member = GetPrivateProfileIntW(L"Patches", feature.key,
            existing ? feature.existingDefault : feature.freshDefault, config.c_str()) != 0;
    }
    Need((fs::exists(config) ? Hash(config) : "") == *s.configHash,
         "Settings changed while reading; reload settings");
    return s;
}

namespace install {
void WriteSettings(const Game& game, const Settings& settings, const fs::path& assets,
                   const fs::path& staged, bool hadConfig) {
    const auto gameId = GameSpecFor(static_cast<GameId>(game.id)).id;
    auto directoryText = RelativeOrAbsolute(game.directory, assets);
    EnsureUnicode(staged, directoryText);
    auto bit = [](bool b) { return b ? L"1" : L"0"; };
    for (const auto& binding : FeatureBindings) {
        const auto& feature = FeatureSpecFor(binding.id);
        if (Supports(feature, gameId))
            Set(staged, L"Patches", feature.key, bit(settings.*binding.member));
    }
    Set(staged, L"UncompressedAssets", L"Directory", directoryText);
    if (!hadConfig)
        Set(staged, L"UncompressedAssets", L"Verify", L"0");
}
} // namespace install
} // namespace rebirths::prepare
