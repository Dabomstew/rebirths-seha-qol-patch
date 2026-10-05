#include "prepare_install_internal.hpp"
#include <imagehlp.h>
#include <cstring>
#include <fstream>
#include <exception>
#pragma comment(lib, "imagehlp.lib")

namespace rebirths::prepare {
using namespace install;
void Apply4GB(const Game& game) {
    CheckIdentity(game);
    auto& spec = rebirths::GameSpecFor(static_cast<rebirths::GameId>(game.id));
    Need(game.originalExecutable, "The optional 4GB action requires the unmodified supported EXE");
    auto state = State(game);
    Need(Ini(state, L"Executable", L"Backup").empty(), "An executable backup is already recorded");
    auto backup = Backups(game) / (L"4gb-" + Stamp());
    SafeBelow(game.directory, backup);
    Need(!fs::exists(backup), "4GB backup already exists");
    fs::create_directories(backup);
    auto original = backup / game.executable.filename();
    Copy(game.executable, original);
    std::ifstream input(game.executable, std::ios::binary | std::ios::ate);
    Need(bool(input), "Cannot read installed EXE");
    auto size = input.tellg();
    Need(size > 0 && size < 128 * 1024 * 1024, "Unexpected EXE size");
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    input.seekg(0);
    Need(bool(input.read(reinterpret_cast<char*>(bytes.data()), size)), "Cannot read full EXE");
    input.close();
    Need(bytes.size() > 0x100 && bytes[0] == 'M' && bytes[1] == 'Z', "Invalid DOS header");
    uint32_t pe{};
    std::memcpy(&pe, bytes.data() + 0x3c, 4);
    Need(pe <= bytes.size() && bytes.size() - pe > 0x80 &&
             !std::memcmp(bytes.data() + pe, "PE\0\0", 4),
         "Invalid PE header");
    auto characteristics = pe + 4 + 18, optional = pe + 24;
    Need(optional + 68 <= bytes.size(), "Truncated PE optional header");
    uint16_t machine{}, magic{}, flags{};
    std::memcpy(&machine, bytes.data() + pe + 4, 2);
    std::memcpy(&magic, bytes.data() + optional, 2);
    std::memcpy(&flags, bytes.data() + characteristics, 2);
    Need(machine == 0x14c && magic == 0x10b && !(flags & 0x20),
         "Unsupported or already-patched PE");
    flags |= 0x20;
    std::memcpy(bytes.data() + characteristics, &flags, 2);
    uint32_t zero = 0;
    std::memcpy(bytes.data() + optional + 64, &zero, 4);
    DWORD headerSum = 0, checkSum = 0;
    Need(CheckSumMappedFile(bytes.data(), DWORD(bytes.size()), &headerSum, &checkSum) != nullptr,
         "Cannot calculate PE checksum");
    std::memcpy(bytes.data() + optional + 64, &checkSum, 4);
    auto stage = game.directory / (L".4gb-" + Stamp() + L".stage");
    auto stateStage = game.directory / (L".4gb-state-" + Stamp() + L".stage");
    bool hadState = fs::exists(state), published = false;
    try {
        WriteNew(stage, bytes.data(), bytes.size());
        Need(Hash(stage) == Hex(spec.executableSha256LargeAddressAwareWithChecksum),
             "4GB patch result was not recognized");
        if (hadState)
            Copy(state, stateStage);
        else {
            const char blank[] = "; Managed preparation paths and backups\r\n";
            WriteNew(stateStage, blank, sizeof(blank) - 1);
        }
        EnsureUnicode(stateStage, original.wstring());
        Set(stateStage, L"Executable", L"Backup", original.wstring());
        Set(stateStage, L"Executable", L"OriginalSHA256", W(game.executableHash));
        Set(stateStage, L"Executable", L"PatchedSHA256",
            W(Hex(spec.executableSha256LargeAddressAwareWithChecksum)));
        CheckIdentity(game);
        Publish(stage, game.executable);
        published = true;
        Need(Hash(game.executable) == Hex(spec.executableSha256LargeAddressAwareWithChecksum),
             "Published 4GB EXE hash mismatch");
        Publish(stateStage, state);
    } catch (...) {
        auto originalError = std::current_exception();
        DeleteFileW(stage.c_str());
        DeleteFileW(stateStage.c_str());
        if (published) {
            try {
                RestoreChecked(original, game.executable);
            } catch (const std::exception& e) {
                throw std::runtime_error(
                    std::string("4GB update failed and EXE recovery failed: ") + e.what());
            }
        }
        std::rethrow_exception(originalError);
    }
}
void RestoreOriginalExe(const Game& game) {
    CheckStopped(game);
    auto state = State(game);
    auto backupText = Ini(state, L"Executable", L"Backup");
    Need(!backupText.empty(), "No preparer-owned 4GB backup is recorded");
    auto path = fs::absolute(fs::path(backupText)).lexically_normal();
    Need(Same(path.parent_path().parent_path(), Backups(game)) &&
             Same(path.filename(), game.executable.filename()),
         "EXE backup path is outside the selected game");
    SafeBelow(game.directory, path);
    auto original = Ascii(Ini(state, L"Executable", L"OriginalSHA256")),
         patched = Ascii(Ini(state, L"Executable", L"PatchedSHA256"));
    auto& spec = rebirths::GameSpecFor(static_cast<rebirths::GameId>(game.id));
    Need(original == Hex(spec.executableSha256) &&
             patched == Hex(spec.executableSha256LargeAddressAwareWithChecksum),
         "Executable backup identity mismatch");
    Need(fs::exists(path) && Hash(path) == original && Hash(game.executable) == patched,
         "Executable or backup changed since 4GB patch");
    auto stage = game.directory / (L".restore-exe-" + Stamp() + L".stage"),
         stateStage = game.directory / (L".restore-state-" + Stamp() + L".stage"),
         patchedStage = game.directory / (L".patched-exe-" + Stamp() + L".stage");
    Copy(path, stage);
    Copy(state, stateStage);
    Copy(game.executable, patchedStage);
    Set(stateStage, L"Executable", L"Backup", L"");
    Set(stateStage, L"Executable", L"OriginalSHA256", L"");
    Set(stateStage, L"Executable", L"PatchedSHA256", L"");
    bool published = false;
    try {
        Publish(stage, game.executable);
        published = true;
        Need(Hash(game.executable) == original, "Restored executable hash mismatch");
        Publish(stateStage, state);
        DeleteFileW(patchedStage.c_str());
    } catch (...) {
        auto originalError = std::current_exception();
        DeleteFileW(stage.c_str());
        DeleteFileW(stateStage.c_str());
        if (published) {
            try {
                Publish(patchedStage, game.executable);
                Need(Hash(game.executable) == patched, "Could not recover patched EXE");
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("EXE restore failed and recovery failed: ") +
                                         e.what());
            }
        } else
            DeleteFileW(patchedStage.c_str());
        std::rethrow_exception(originalError);
    }
}
} // namespace rebirths::prepare
