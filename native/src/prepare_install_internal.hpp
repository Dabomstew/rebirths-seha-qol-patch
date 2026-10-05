#pragma once
#include "prepare_game.hpp"
#include "rebirths_patch.hpp"
#include "platform_util.hpp"
#include <initializer_list>
#include <stdexcept>

// Private installation contracts. Keep game-root containment and owned-backup
// policy here, separate from asset-output containment.
namespace rebirths::prepare::install {
using platform::Hex;
using platform::Wide;
using platform::Utf8;
using platform::Handle;
void Need(bool good, const char* why);
std::string Hash(const fs::path& path);
bool Same(const fs::path& a, const fs::path& b);
fs::path Safe(const fs::path& path);
fs::path SafeBelow(const fs::path& root, const fs::path& path);
fs::path State(const Game& game);
fs::path Backups(const Game& game);
std::wstring Ini(const fs::path& file, const wchar_t* section, const wchar_t* key,
                 const wchar_t* fallback = L"");
void Set(const fs::path& file, const wchar_t* section, const wchar_t* key,
         const std::wstring& value);
void CopyKeys(const fs::path& from, const fs::path& to, const wchar_t* section,
              std::initializer_list<const wchar_t*> keys);
std::wstring Stamp();
void Copy(const fs::path& from, const fs::path& to);
void WriteNew(const fs::path& file, const void* data, size_t size);
void Publish(const fs::path& from, const fs::path& to);
void RemoveVerified(const fs::path& path, const std::string& hash);
std::string Ascii(const std::wstring& value);
std::wstring W(const std::string& value);
void CheckStopped(const Game& game);
void CheckIdentity(const Game& game);
std::wstring RelativeOrAbsolute(const fs::path& game, const fs::path& value);
void EnsureUnicode(const fs::path& file, const std::wstring& value);
fs::path ProxyDir(const fs::path& root);
fs::path ValidateBackup(const Game& game, const std::wstring& text);
void RestoreChecked(const fs::path& source, const fs::path& target);
void WriteSettings(const Game& game, const Settings& settings, const fs::path& assets,
                   const fs::path& staged, bool hadConfig);
void Preflight(const Game& game, const Settings& settings);
void InstallPrepared(const Game& game, const Settings& settings, const fs::path& assets);
} // namespace rebirths::prepare::install
