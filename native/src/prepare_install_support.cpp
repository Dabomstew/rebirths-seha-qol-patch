#include "prepare_install_internal.hpp"
#include "asset_store.hpp"
#include <tlhelp32.h>
#include <algorithm>
#include <cstdio>
#include <fstream>

namespace rebirths::prepare::install {
void Need(bool b, const char* m) {
    if (!b)
        throw std::runtime_error(m);
}
std::string Hash(const fs::path& p) {
    return Hex(rebirths::HashFile(p.wstring()));
}
bool Same(const fs::path& a, const fs::path& b) {
    auto x = fs::absolute(a).lexically_normal().wstring(),
         y = fs::absolute(b).lexically_normal().wstring();
    return CompareStringOrdinal(x.c_str(), -1, y.c_str(), -1, TRUE) == CSTR_EQUAL;
}
fs::path Safe(const fs::path& path) {
    return rebirths::assets::CheckedPath(path);
}
fs::path SafeBelow(const fs::path& root, const fs::path& path) {
    const auto boundary = fs::absolute(root).lexically_normal(),
               result = fs::absolute(path).lexically_normal();
    auto relative = result.lexically_relative(boundary);
    Need(!relative.empty() && *relative.begin() != L"..",
         "Preparation path is outside selected folder");
    // Installer paths may contain Unicode; manifest-relative paths remain ASCII.
    for (auto p = result;; p = p.parent_path()) {
        Safe(p);
        if (p == boundary || p.lexically_relative(boundary) == L".")
            break;
    }
    return result;
}
fs::path State(const Game& g) {
    return SafeBelow(g.directory, g.directory / L"rebirths-prepare-state.ini");
}
fs::path Backups(const Game& g) {
    return SafeBelow(g.directory, g.directory / L"rebirths-prepare-backups");
}
std::wstring Ini(const fs::path& file, const wchar_t* section, const wchar_t* key,
                 const wchar_t* fallback) {
    wchar_t text[32768]{};
    auto n = GetPrivateProfileStringW(section, key, fallback, text, 32768, file.c_str());
    Need(n < 32767, "INI value exceeds bound");
    return text;
}
void Set(const fs::path& file, const wchar_t* section, const wchar_t* key,
         const std::wstring& value) {
    Need(WritePrivateProfileStringW(section, key, value.c_str(), file.c_str()) != 0,
         "Cannot update INI");
}
void CopyKeys(const fs::path& from, const fs::path& to, const wchar_t* section,
              std::initializer_list<const wchar_t*> keys) {
    if (!fs::exists(from))
        return;
    for (auto key : keys) {
        auto value = Ini(from, section, key);
        if (!value.empty())
            Set(to, section, key, value);
    }
}
std::wstring Stamp() {
    SYSTEMTIME t{};
    GetSystemTime(&t);
    wchar_t s[80]{};
    swprintf_s(s, L"%04u%02u%02uT%02u%02u%02u%03u-%lu", t.wYear, t.wMonth, t.wDay, t.wHour,
               t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentProcessId());
    return s;
}
void Copy(const fs::path& from, const fs::path& to) {
    Need(CopyFileW(from.c_str(), to.c_str(), TRUE) != 0, "Cannot copy preparation file");
    Need(Hash(from) == Hash(to), "Copied file hash mismatch");
}
void WriteNew(const fs::path& file, const void* data, size_t size) {
    Need(size <= MAXDWORD, "Preparation file too large");
    Handle h(CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                         nullptr));
    Need(bool(h), "Cannot create preparation file");
    DWORD written = 0;
    bool ok = WriteFile(h.value, data, DWORD(size), &written, nullptr) && written == size &&
              FlushFileBuffers(h.value);
    Need(ok, "Cannot write preparation file");
}
void Publish(const fs::path& from, const fs::path& to) {
    Need(MoveFileExW(from.c_str(), to.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
         "Cannot publish preparation file");
}
void RemoveVerified(const fs::path& path, const std::string& hash) {
    if (fs::exists(path)) {
        Need(Hash(path) == hash, "File changed before removal");
        Need(DeleteFileW(path.c_str()) != 0, "Cannot remove verified file");
    }
}
std::string Ascii(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (wchar_t c : w) {
        Need(c < 128, "ASCII value expected");
        out += char(c);
    }
    return out;
}
std::wstring W(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}
void CheckStopped(const Game& game) {
    Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    Need(bool(snap), "Cannot inspect running games");
    PROCESSENTRY32W item{sizeof(item)};
    bool running = false, unknown = false;
    if (Process32FirstW(snap.value, &item))
        do {
            if (_wcsicmp(item.szExeFile, game.executable.filename().c_str()))
                continue;
            Handle process(
                OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, item.th32ProcessID));
            if (!process) {
                unknown = true;
                continue;
            }
            wchar_t path[32768]{};
            DWORD size = 32768;
            if (!QueryFullProcessImageNameW(process.value, 0, path, &size))
                unknown = true;
            else {
                std::error_code error;
                bool sameFile = fs::equivalent(path, game.executable, error);
                if (error)
                    unknown = true;
                else if (sameFile)
                    running = true;
            }
        } while (Process32NextW(snap.value, &item));
    Need(!unknown, "Cannot verify a running game process path");
    Need(!running, "Close the selected game before preparing or changing it");
}
void CheckIdentity(const Game& game) {
    GameSpecFor(static_cast<GameId>(game.id));
    SafeBelow(game.directory, game.executable);
    SafeBelow(game.directory, game.proxyDirectory);
    Need(Hash(game.executable) == game.executableHash,
         "Game executable changed during preparation");
    CheckStopped(game);
}
std::wstring RelativeOrAbsolute(const fs::path& game, const fs::path& value) {
    auto absolute = fs::absolute(value).lexically_normal();
    auto relative = absolute.lexically_relative(game);
    if (!relative.empty() && relative.native().rfind(L"..", 0) != 0)
        return relative.wstring();
    return absolute.wstring();
}
void EnsureUnicode(const fs::path& file, const std::wstring& value) {
    if (std::all_of(value.begin(), value.end(), [](wchar_t c) { return c < 128; }))
        return;
    std::ifstream in(file, std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(in)), {});
    in.close();
    if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xff &&
        static_cast<unsigned char>(raw[1]) == 0xfe)
        return;
    int n = MultiByteToWideChar(CP_ACP, 0, raw.data(), int(raw.size()), nullptr, 0);
    Need(n >= 0, "INI encoding failed");
    std::wstring wide(n, L' ');
    if (n)
        MultiByteToWideChar(CP_ACP, 0, raw.data(), int(raw.size()), wide.data(), n);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    const unsigned char bom[] = {0xff, 0xfe};
    out.write(reinterpret_cast<const char*>(bom), 2);
    out.write(reinterpret_cast<const char*>(wide.data()), wide.size() * 2);
    Need(bool(out), "Cannot preserve Unicode configuration");
}
fs::path ProxyDir(const fs::path& root) {
    auto leaf = root.filename().wstring();
    auto split = leaf.find(L';');
    if (split == std::wstring::npos)
        return root;
    auto suffix = leaf.substr(split + 1);
    Need(!suffix.empty() && suffix != L"." && suffix != L".." &&
             suffix.find_first_of(L"\\/:*?\"<>|") == std::wstring::npos,
         "Unsafe semicolon-derived directory");
    auto result = (root / suffix).lexically_normal();
    Need(Same(result.parent_path(), root), "Semicolon directory escaped game folder");
    SafeBelow(root, result);
    return result;
}
fs::path ValidateBackup(const Game& g, const std::wstring& text) {
    Need(!text.empty(), "No backup recorded");
    auto candidate = fs::absolute(fs::path(text)).lexically_normal();
    Need(Same(candidate.parent_path(), Backups(g)), "Backup is outside game folder");
    SafeBelow(g.directory, candidate);
    Need(fs::is_directory(candidate), "Backup directory missing");
    return candidate;
}
void RestoreChecked(const fs::path& source, const fs::path& target) {
    auto staged = target.wstring() + L".recovery-" + Stamp();
    Copy(source, staged);
    Publish(staged, target);
    Need(Hash(source) == Hash(target), "Preparation recovery hash mismatch");
}
} // namespace rebirths::prepare::install
