#pragma once
#include "prepare_assets_native.hpp"
#include "feature_catalog.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rebirths::prepare {
struct Game {
    uint32_t id = 0;
    fs::path directory;
    fs::path executable;
    fs::path proxyDirectory;
    std::string executableHash;
    bool originalExecutable = false;
    bool ntcoreExecutable = false;
};
struct Settings {
    fs::path assets;
#include "prepare_feature_fields.inc"
    TransformProfile transformProfile = TransformProfile::Raw;
    // Present when settings came from disk; empty hash means the INI was absent.
    std::optional<std::string> configHash;
};
Game OpenGame(const fs::path& selected);
std::vector<fs::path> DetectGames();
Settings ReadSettings(const Game& game);
void ApplySettings(const Game& game, const Settings& settings);
void PrepareAndInstall(const Game& game, const Settings& settings, const Report& report = {},
                       const Cancel& cancel = {});
void Rollback(const Game& game);
void Uninstall(const Game& game);
void Apply4GB(const Game& game);
void RestoreOriginalExe(const Game& game);
std::string EmbeddedProxyHash();
} // namespace rebirths::prepare
