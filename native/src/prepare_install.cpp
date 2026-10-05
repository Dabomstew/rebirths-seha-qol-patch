#include "prepare_install_internal.hpp"
#include <algorithm>
#include <set>
#include <exception>

namespace rebirths::prepare {
using namespace install;
namespace {
std::vector<unsigned char> Resource(int number) {
    auto module = GetModuleHandleW(nullptr);
    auto r = FindResourceW(module, MAKEINTRESOURCEW(number), RT_RCDATA);
    Need(r != nullptr, "Preparer resource missing");
    auto loaded = LoadResource(module, r);
    auto* p = loaded ? LockResource(loaded) : nullptr;
    auto size = SizeofResource(module, r);
    Need(p && size, "Cannot read preparer resource");
    const auto* data = static_cast<const unsigned char*>(p);
    return {data, data + size};
}
std::string ResourceText(int number) {
    auto b = Resource(number);
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}
std::set<std::string> Known() {
    std::set<std::string> hashes;
    auto text = ResourceText(102);
    size_t p = 0;
    while (p < text.size()) {
        auto e = text.find('\n', p);
        if (e == std::string::npos)
            e = text.size();
        auto line = text.substr(p, e - p);
        p = e + 1;
        if (line.empty() || line[0] == '#')
            continue;
        Need(line.size() > 67 && line[64] == ' ' && line[65] == '|' && line[66] == ' ',
             "Bad known-proxy registry line");
        auto hash = line.substr(0, 64);
        Need(std::all_of(hash.begin(), hash.end(),
                         [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }),
             "Bad known-proxy hash");
        Need(hashes.insert(hash).second, "Duplicate known-proxy hash");
    }
    Need(!hashes.empty(), "Known-proxy registry empty");
    return hashes;
}
std::string EmbeddedHash() {
    auto b = Resource(101);
    Need(b.size() >= 128 && b[0] == 'M' && b[1] == 'Z', "Bundled proxy is not a PE file");
    auto temp = fs::temp_directory_path() / (L"rebirths-proxy-check-" + Stamp() + L".dll");
    try {
        WriteNew(temp, b.data(), b.size());
        auto hash = Hash(temp);
        DeleteFileW(temp.c_str());
        Need(Known().count(hash) > 0, "Bundled proxy hash missing from registry");
        return hash;
    } catch (...) {
        DeleteFileW(temp.c_str());
        throw;
    }
}
void ValidateProxy(const fs::path& file) {
    if (!fs::exists(file))
        return;
    Need(fs::is_regular_file(file), "Proxy path is not a file");
    Need(Known().count(Hash(file)) > 0,
         "A different X3DAudio1_7.dll is installed; leave it untouched");
}
void Failpoint(const char* point) {
#ifdef REBIRTHS_PREPARE_TEST
    char value[80]{};
    auto size = GetEnvironmentVariableA("REBIRTHS_PREPARE_FAILPOINT", value, sizeof(value));
    if (size && size < sizeof(value) && std::string(value) == point)
        throw std::runtime_error("Injected preparation interruption");
#else
    (void)point;
#endif
}
} // namespace
namespace install {
void Preflight(const Game& game, const Settings& settings) {
    CheckIdentity(game);
    auto config = SafeBelow(game.directory, game.proxyDirectory / L"rebirths-patches.ini");
    if (settings.configHash)
        Need((fs::exists(config) ? Hash(config) : "") == *settings.configHash,
             "Settings changed on disk; reload settings before applying");
    ValidateProxy(SafeBelow(game.directory, game.proxyDirectory / L"X3DAudio1_7.dll"));
    if (static_cast<GameId>(game.id) == GameId::Rebirth3)
        Need(!fs::exists(game.directory / L"steam_api_original.dll"),
             "Unsupported Re;Birth3 installation layout detected; contact the patch author");
}
void InstallPrepared(const Game& game, const Settings& settings, const fs::path& assets) {
    Preflight(game, settings);
    auto proxyBytes = Resource(101);
    auto wanted = EmbeddedHash();
    auto proxy = game.proxyDirectory / L"X3DAudio1_7.dll",
         config = game.proxyDirectory / L"rebirths-patches.ini";
    SafeBelow(game.directory, proxy);
    SafeBelow(game.directory, config);
    ValidateProxy(proxy);
    bool hadProxy = fs::exists(proxy), hadConfig = fs::exists(config),
         hadState = fs::exists(State(game));
    std::string oldProxy = hadProxy ? Hash(proxy) : "", oldConfig = hadConfig ? Hash(config) : "";
    if (settings.configHash)
        Need(oldConfig == *settings.configHash,
             "Settings changed on disk; reload settings before applying");
    auto backup = Backups(game) / Stamp();
    SafeBelow(game.directory, backup);
    Need(!fs::exists(backup), "Preparation backup already exists");
    fs::create_directories(backup);
    if (hadProxy)
        Copy(proxy, backup / L"X3DAudio1_7.dll");
    if (hadConfig)
        Copy(config, backup / L"rebirths-patches.ini");
    if (hadState)
        Copy(State(game), backup / L"rebirths-prepare-state.ini");
    fs::create_directories(game.proxyDirectory);
    auto dllStage = game.proxyDirectory / (L".X3DAudio1_7." + Stamp() + L".stage");
    auto iniStage = game.proxyDirectory / (L".rebirths-patches." + Stamp() + L".stage");
    auto stateStage = game.directory / (L".rebirths-prepare-state." + Stamp() + L".stage");
    bool dllPublished = false, configPublished = false, statePublished = false;
    std::string configHash;
    try {
        WriteNew(dllStage, proxyBytes.data(), proxyBytes.size());
        Need(Hash(dllStage) == wanted, "Staged proxy differs from bundled build");
        if (hadConfig)
            Copy(config, iniStage);
        else {
            auto templateText = ResourceText(103);
            WriteNew(iniStage, templateText.data(), templateText.size());
        }
        WriteSettings(game, settings, assets, iniStage, hadConfig);
        configHash = Hash(iniStage);
        {
            const char blank[] = "; Managed preparation paths and backups\r\n";
            WriteNew(stateStage, blank, sizeof(blank) - 1);
        }
        EnsureUnicode(stateStage, backup.wstring());
        CopyKeys(State(game), stateStage, L"Executable",
                 {L"Backup", L"OriginalSHA256", L"PatchedSHA256"});
        Set(stateStage, L"Install", L"ProxyDirectory", game.proxyDirectory.wstring());
        Set(stateStage, L"Install", L"AssetsDirectory", assets.wstring());
        Set(stateStage, L"Install", L"LastBackup", backup.wstring());
        Set(stateStage, L"Install", L"Active", L"1");
        auto manifest = backup / L"snapshot.ini";
        const char blank[] = "; Verified preparation transaction\r\n";
        WriteNew(manifest, blank, sizeof(blank) - 1);
        EnsureUnicode(manifest, game.directory.wstring());
        Set(manifest, L"Snapshot", L"GameDirectory", game.directory.wstring());
        Set(manifest, L"Snapshot", L"ProxyPath", proxy.wstring());
        Set(manifest, L"Snapshot", L"ConfigPath", config.wstring());
        Set(manifest, L"Snapshot", L"HadProxy", hadProxy ? L"1" : L"0");
        Set(manifest, L"Snapshot", L"HadConfig", hadConfig ? L"1" : L"0");
        Set(manifest, L"Snapshot", L"HadState", hadState ? L"1" : L"0");
        Set(manifest, L"Snapshot", L"PriorProxyHash", W(oldProxy));
        Set(manifest, L"Snapshot", L"InstalledProxyHash", W(wanted));
        Set(manifest, L"Snapshot", L"PriorConfigHash", W(oldConfig));
        Set(manifest, L"Snapshot", L"InstalledConfigHash", W(configHash));
        CheckIdentity(game);
        Need((fs::exists(config) ? Hash(config) : "") == oldConfig,
             "Settings changed during installation; reload settings");
        Publish(dllStage, proxy);
        dllPublished = true;
        Need(Hash(proxy) == wanted, "Published proxy hash mismatch");
        Failpoint("after-proxy");
        Need((fs::exists(config) ? Hash(config) : "") == oldConfig,
             "Settings changed during installation; reload settings");
        Publish(iniStage, config);
        configPublished = true;
        Need(Hash(config) == configHash, "Published settings hash mismatch");
        Failpoint("after-config");
        Publish(stateStage, State(game));
        statePublished = true;
    } catch (...) {
        auto original = std::current_exception();
        std::string recoveryError;
        try {
            if (configPublished) {
                if (hadConfig)
                    RestoreChecked(backup / L"rebirths-patches.ini", config);
                else
                    RemoveVerified(config, configHash);
            }
        } catch (const std::exception& e) {
            recoveryError += std::string(" settings: ") + e.what();
        }
        try {
            if (dllPublished) {
                if (hadProxy)
                    RestoreChecked(backup / L"X3DAudio1_7.dll", proxy);
                else
                    RemoveVerified(proxy, wanted);
            }
        } catch (const std::exception& e) {
            recoveryError += std::string(" proxy: ") + e.what();
        }
        try {
            if (statePublished) {
                if (hadState)
                    RestoreChecked(backup / L"rebirths-prepare-state.ini", State(game));
                else
                    Need(DeleteFileW(State(game).c_str()) != 0, "Cannot remove new state");
            }
        } catch (const std::exception& e) {
            recoveryError += std::string(" state: ") + e.what();
        }
        DeleteFileW(dllStage.c_str());
        DeleteFileW(iniStage.c_str());
        DeleteFileW(stateStage.c_str());
        if (!recoveryError.empty())
            throw std::runtime_error("Preparation failed and recovery requires attention:" +
                                     recoveryError);
        std::rethrow_exception(original);
    }
}
} // namespace install
std::string EmbeddedProxyHash() {
    return EmbeddedHash();
}
void Rollback(const Game& game) {
    CheckIdentity(game);
    auto state = State(game), backup = ValidateBackup(game, Ini(state, L"Install", L"LastBackup"));
    auto manifest = backup / L"snapshot.ini", proxy = game.proxyDirectory / L"X3DAudio1_7.dll",
         config = game.proxyDirectory / L"rebirths-patches.ini";
    Need(Same(Ini(manifest, L"Snapshot", L"GameDirectory"), game.directory) &&
             Same(Ini(manifest, L"Snapshot", L"ProxyPath"), proxy) &&
             Same(Ini(manifest, L"Snapshot", L"ConfigPath"), config),
         "Backup targets a different installation");
    bool hadProxy = Ini(manifest, L"Snapshot", L"HadProxy") == L"1",
         hadConfig = Ini(manifest, L"Snapshot", L"HadConfig") == L"1",
         hadState = Ini(manifest, L"Snapshot", L"HadState") == L"1";
    auto after = Ascii(Ini(manifest, L"Snapshot", L"InstalledProxyHash")),
         prior = Ascii(Ini(manifest, L"Snapshot", L"PriorProxyHash"));
    Need(fs::exists(proxy) && Known().count(after) > 0 && Hash(proxy) == after,
         "Installed proxy changed since this backup");
    if (hadProxy) {
        auto old = backup / L"X3DAudio1_7.dll";
        Need(Known().count(prior) > 0 && fs::exists(old) && Hash(old) == prior,
             "Prior proxy backup is not recognized");
    }
    auto priorConfig = Ascii(Ini(manifest, L"Snapshot", L"PriorConfigHash"));
    if (hadConfig) {
        auto old = backup / L"rebirths-patches.ini";
        Need(fs::exists(old) && Hash(old) == priorConfig, "Prior settings backup changed");
    }
    if (hadState)
        Need(fs::exists(backup / L"rebirths-prepare-state.ini"), "Prior state backup missing");
    auto afterConfig = Ascii(Ini(manifest, L"Snapshot", L"InstalledConfigHash"));
    bool restoreConfig = fs::exists(config) && Hash(config) == afterConfig;
    auto proxyTemp = game.proxyDirectory / (L".rollback-proxy-" + Stamp());
    auto configTemp = game.proxyDirectory / (L".rollback-config-" + Stamp());
    if (hadProxy)
        Copy(backup / L"X3DAudio1_7.dll", proxyTemp);
    if (restoreConfig && hadConfig)
        Copy(backup / L"rebirths-patches.ini", configTemp);
    CheckIdentity(game);
    if (hadProxy)
        Publish(proxyTemp, proxy);
    else
        RemoveVerified(proxy, after);
    if (restoreConfig && hadConfig)
        Publish(configTemp, config);
    // An edited configuration is retained. Fresh-install settings are retained
    // as well, as in Uninstall; they are not proof of proxy ownership.
    if (hadState) {
        auto previous = backup / L"rebirths-prepare-state.ini";
        auto staged = game.directory / (L".rollback-state-" + Stamp());
        const char blank[] = "; Managed preparation paths and backups\r\n";
        WriteNew(staged, blank, sizeof(blank) - 1);
        EnsureUnicode(staged, game.directory.wstring());
        CopyKeys(previous, staged, L"Install",
                 {L"ProxyDirectory", L"AssetsDirectory", L"LastBackup", L"Active"});
        CopyKeys(previous, staged, L"Executable", {L"Backup", L"OriginalSHA256", L"PatchedSHA256"});
        Publish(staged, state);
    } else {
        Set(state, L"Install", L"Active", L"0");
        Set(state, L"Install", L"LastBackup", L"");
    }
}
void Uninstall(const Game& game) {
    CheckIdentity(game);
    auto proxy = game.proxyDirectory / L"X3DAudio1_7.dll";
    Need(fs::exists(proxy), "No X3DAudio proxy is installed");
    ValidateProxy(proxy);
    auto hash = Hash(proxy);
    RemoveVerified(proxy, hash);
    auto state = State(game);
    if (fs::exists(state))
        Set(state, L"Install", L"Active", L"0");
}
} // namespace rebirths::prepare
