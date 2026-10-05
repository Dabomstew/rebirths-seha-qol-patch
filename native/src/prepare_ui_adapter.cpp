#include "prepare_ui_adapter.hpp"
#include "prepare_feature_settings.hpp"
#include "rebirths_patch.hpp"
#include "platform_util.hpp"
#include "target_catalog.hpp"
#include <algorithm>
namespace rebirths::prepare {
namespace {
struct Snapshot { Game game; Settings settings; };
fs::path OtherProfilePath(fs::path path, bool half) {
    const std::wstring suffix = L"-cg24v1"; auto leaf = path.filename().wstring();
    if (half) {
        if (leaf == L"assets") leaf = L"cg24-v1";
        else if (leaf.size() < suffix.size() || leaf.substr(leaf.size() - suffix.size()) != suffix) leaf += suffix;
    } else if (leaf == L"cg24-v1") leaf = L"assets";
    else if (leaf.size() >= suffix.size() && leaf.substr(leaf.size() - suffix.size()) == suffix) leaf.resize(leaf.size() - suffix.size());
    else leaf += L"-raw";
    path.replace_filename(leaf); return path;
}
const preparer::Feature* Find(const preparer::View& view, const std::string& id) {
    for (const auto& f : view.features) if (f.id == id) return &f;
    return nullptr;
}
}
preparer::Product UiAdapter::Describe() const {
    preparer::Product p{L"Rebirths Preparer", L"Rebirths / Sega Hard Girls QoL Patch",
        L"Choose your game folder and settings. Prepare assets for faster loading, or install the patch and play."};
    p.multipleGames = true; return p;
}
std::vector<fs::path> UiAdapter::Detect() { return DetectGames(); }
preparer::View UiAdapter::Open(const fs::path& path) {
    auto game = OpenGame(path); auto settings = ReadSettings(game);
    preparer::View v; v.game = game.directory; v.executable = game.executable;
    for (const auto& target : catalog::Targets) if (uint32_t(target.spec.id) == game.id) v.gameName = target.steamFolder;
    v.note = L"Settings for " + v.gameName + L". Your saved choices are loaded; advanced INI settings are kept.";
    v.paths = {{"assets", L"Prepared assets...", settings.assets}};
    v.snapshot = std::make_shared<Snapshot>(Snapshot{game, settings});
    for (const auto& binding : FeatureBindings) {
        const auto& f = FeatureSpecFor(binding.id);
        if (!Supports(f, static_cast<GameId>(game.id))) continue;
        unsigned group = (binding.id == FeatureId::UncompressedAssets || binding.id == FeatureId::FastTextureConversion ||
            binding.id == FeatureId::DungeonMovementFix) ? 0u : 1u;
        v.features.push_back({platform::Utf8(f.key), f.uiLabel, group, settings.*binding.member});
    }
    v.features.push_back({"downscale", L"Downscale large story images (24 MiB+)", 0,
        settings.transformProfile == TransformProfile::AdvCgHalf24V1});
    return v;
}
std::vector<preparer::Capability> UiAdapter::Actions(const preparer::View& view) const {
    using A = preparer::Action;
    std::vector<preparer::Capability> result{{A::Install}, {A::Prepare}, {A::Play}, {A::Rollback}, {A::Uninstall}};
    const auto data = std::static_pointer_cast<const Snapshot>(view.snapshot);
    result.push_back({A::Patch4GB, data && data->game.originalExecutable});
    result.push_back({A::RestoreExe, data && data->game.ntcoreExecutable});
    return result;
}
void UiAdapter::Changed(preparer::View& view, const std::string& id) {
    if (id == "downscale" && !view.paths.empty()) view.paths[0].value = OtherProfilePath(view.paths[0].value, Find(view, id)->value);
}
preparer::Outcome UiAdapter::Execute(preparer::Action action, const preparer::View& view,
        const preparer::Report& report, const preparer::Cancel& cancel) {
    const auto data = std::static_pointer_cast<const Snapshot>(view.snapshot);
    if (!data) throw std::runtime_error("Select a supported game");
    auto settings = data->settings;
    for (const auto& p : view.paths) if (p.id == "assets") {
        if (p.value.empty() || !p.value.is_absolute()) throw std::runtime_error("Choose a full folder path for prepared assets, such as D:\\Games\\PreparedAssets");
        settings.assets = p.value;
    }
    for (const auto& binding : FeatureBindings) {
        const auto* f = Find(view, platform::Utf8(FeatureSpecFor(binding.id).key));
        if (f) settings.*binding.member = f->value;
    }
    if (const auto* f = Find(view, "downscale")) {
        if (f->value) settings.transformProfile = TransformProfile::AdvCgHalf24V1;
        else if (settings.assets != data->settings.assets) settings.transformProfile = TransformProfile::Raw;
    }
    using A = preparer::Action;
    try {
        std::wstring summary;
        switch (action) {
        case A::Install: ApplySettings(data->game, settings); summary = L"Patch installed and settings saved."; break;
        case A::Prepare:
            PrepareAndInstall(data->game, settings, [&](const Progress& p) {
                if (report) report({p.stage, p.current, L"files", p.completed, p.total});
            }, cancel); summary = L"Preparation complete."; break;
        case A::Rollback: Rollback(data->game); summary = L"Last update rolled back."; break;
        case A::Uninstall: Uninstall(data->game); summary = L"Patch uninstalled. Your prepared assets and settings have been kept."; break;
        case A::Patch4GB: Apply4GB(data->game); summary = L"4GB patch applied."; break;
        case A::RestoreExe: RestoreOriginalExe(data->game); summary = L"Original executable restored."; break;
        default: throw std::runtime_error("Unsupported action");
        }
        return {preparer::State::Completed, L"Action complete", summary};
    } catch (const std::exception& e) {
        if (action == A::Prepare && std::string(e.what()).rfind("Cancelled", 0) == 0) throw preparer::Cancelled(e.what());
        throw;
    }
}
}
