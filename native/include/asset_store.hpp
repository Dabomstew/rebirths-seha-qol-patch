#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "platform_util.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace rebirths::assets {
using Hash = rebirths::platform::Digest;
using Handle = rebirths::platform::Handle;
struct File {
    std::string path;
    uint64_t offset=0,storageSize=0;
    uint32_t size=0,id=0;
    Hash hash{};
    std::array<unsigned char,288> metadata{};
    bool dlc=false;
};
struct Store {
    std::filesystem::path root;
    std::unordered_map<std::string,std::unordered_map<uint32_t,File>> groups;
    std::vector<Handle> sourceLocks;
    uint32_t backend=0,rejectedGroups=0;
    size_t count=0;
    std::vector<std::string> diagnostics;
};
std::string CanonicalPath(std::string name, bool manager=false);
// Check the selected root and every relative component below it. Ancestors
// above that root may be linked Steam-library/Wine paths and are not managed.
std::filesystem::path CheckedPath(const std::filesystem::path& root,const std::string& relative={});
Handle OpenRead(const std::filesystem::path& path);
Hash HashRange(HANDLE file,uint64_t start,uint64_t length);
uint64_t FileSize(HANDLE file);
Store Load(const std::filesystem::path& root,const std::filesystem::path& gameRoot,uint32_t game);
Handle OpenAsset(const Store& store,const File& file,bool verify);
}
