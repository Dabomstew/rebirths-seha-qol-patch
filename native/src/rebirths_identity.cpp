#include "rebirths_patch.hpp"
#include "platform_util.hpp"
#include "target_catalog.hpp"
#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace rebirths {

const GameSpec* FindGameSpec(GameId id) noexcept {
    for (const auto& target : catalog::Targets)
        if (target.spec.id == id) return &target.spec;
    return nullptr;
}

const GameSpec& GameSpecFor(GameId id) {
    if (const auto* spec = FindGameSpec(id)) return *spec;
    throw std::invalid_argument("Unsupported game ID");
}

const GameSpec* IdentifyGame(const Digest& digest) noexcept {
    for (const auto& target : catalog::Targets) {
        const auto& spec = target.spec;
        if (spec.executableSha256 == digest || spec.executableSha256LargeAddressAware == digest ||
            spec.executableSha256LargeAddressAwareWithChecksum == digest) return &spec;
    }
    return nullptr;
}

const char* GameIdName(GameId id) noexcept {
    for (const auto& target : catalog::Targets)
        if (target.spec.id == id) return target.name;
    return "unknown";
}

std::wstring ModulePath(HMODULE module) {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) throw std::runtime_error("module path unavailable");
    return std::wstring(buffer.data(), length);
}

Digest HashFile(const std::wstring& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot open executable for identity check");
    const auto length = input.tellg();
    if (length < 0 || length > 128 * 1024 * 1024) throw std::runtime_error("unexpected executable size");
    std::vector<unsigned char> bytes(static_cast<size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("cannot read executable for identity check");

    rebirths::platform::Sha sha;
    sha.Add(bytes.data(), bytes.size());
    return sha.Finish();
}

}
