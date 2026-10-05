#include "prepare_install_internal.hpp"
#include "target_catalog.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <regex>
#pragma comment(lib, "advapi32.lib")

namespace rebirths::prepare {
using namespace install;
Game OpenGame(const fs::path& selected) {
    auto root = fs::absolute(selected).lexically_normal();
    Safe(root);
    Need(fs::is_directory(root), "Select an installed game folder");
    const rebirths::GameSpec* found = nullptr;
    fs::path executable;
    for (auto id : AllGameIds) {
        auto& spec = rebirths::GameSpecFor(id);
        auto path = root / spec.executableName;
        if (fs::exists(path)) {
            Need(!found, "More than one supported game executable in this folder");
            found = &spec;
            executable = path;
        }
    }
    Need(found != nullptr, "No supported game EXE found. Choose the folder containing the game EXE.");
    SafeBelow(root, executable);
    auto hash = rebirths::HashFile(executable.wstring());
    Need(rebirths::IdentifyGame(hash) == found, "This game EXE does not match a supported version. Leave it in place and report this message.");
    Game game{};
    game.id = static_cast<uint32_t>(found->id);
    game.directory = root;
    game.executable = executable;
    game.proxyDirectory = ProxyDir(root);
    game.executableHash = Hex(hash);
    game.originalExecutable = hash == found->executableSha256;
    game.ntcoreExecutable = hash == found->executableSha256LargeAddressAwareWithChecksum;
    return game;
}
std::vector<fs::path> DetectGames() {
    std::vector<fs::path> libraries;
    libraries.push_back(fs::path(rebirths::ModulePath(GetModuleHandleW(nullptr))).parent_path());
    wchar_t steam[32768]{};
    DWORD bytes = sizeof(steam);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ,
                     nullptr, steam, &bytes) == ERROR_SUCCESS) {
        fs::path root(steam);
        libraries.push_back(root);
        std::ifstream vdf(root / L"steamapps/libraryfolders.vdf");
        std::string line;
        std::regex pattern("\\\"path\\\"\\s+\\\"([^\\\"]+)\\\"");
        std::smatch match;
        while (std::getline(vdf, line))
            if (std::regex_search(line, match, pattern)) {
                auto text = match[1].str();
                for (size_t at = 0; (at = text.find("\\\\", at)) != std::string::npos;)
                    text.replace(at, 2, "\\");
                libraries.push_back(fs::u8path(text));
            }
    }
    std::vector<fs::path> games;
    for (const auto& library : libraries) {
        std::vector<fs::path> candidates = {library};
        for (const auto& target : catalog::Targets)
            candidates.push_back(library / L"steamapps/common" / target.steamFolder);
        for (const auto& candidate : candidates)
            try {
                auto game = OpenGame(candidate);
                if (std::none_of(games.begin(), games.end(),
                                 [&](const auto& p) { return Same(p, game.directory); }))
                    games.push_back(game.directory);
            } catch (...) {
            }
    }
    return games;
}
void ApplySettings(const Game& game, const Settings& settings) {
    Need(!settings.assets.empty() && settings.assets.is_absolute(),
         "Choose a full folder path for prepared assets.");
    install::InstallPrepared(game, settings, settings.assets.lexically_normal());
}
void PrepareAndInstall(const Game& game, const Settings& settings, const Report& report,
                       const Cancel& cancel) {
    install::Preflight(game, settings);
    auto assets = fs::absolute(settings.assets).lexically_normal();
    RunAssets(game.directory, assets, game.id, report, cancel, settings.transformProfile);
    VerifyAssets(game.directory, assets, game.id, report, cancel, settings.transformProfile);
    if (cancel && cancel())
        throw std::runtime_error("Cancelled; installed files are unchanged");
    install::InstallPrepared(game, settings, assets);
}
} // namespace rebirths::prepare
