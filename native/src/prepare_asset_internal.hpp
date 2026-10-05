#pragma once
#include "prepare_assets_native.hpp"
#include "prepare_json.hpp"
#include "asset_store.hpp"
#include "adv_cg_specs.hpp"
#include "rebirths_patch.hpp"
#include <cstring>

// Private asset pipeline: immutable source contracts, bounded binary I/O,
// transforms, ownership metadata and resumable archive publication.
namespace rebirths::prepare::assetprep {
using Hash = rebirths::assets::Hash;
using Handle = rebirths::assets::Handle;
using platform::Wide;
using platform::Utf8;
using platform::Hex;
using platform::Unhex;
using platform::Sha;
constexpr uint64_t PackLimit = 0x60000000;
constexpr uint32_t BlockLimit = 16 * 1024 * 1024;
constexpr uint32_t Chunk = 1024 * 1024;
void CheckCancel(const Cancel& cancel);
Hash Digest(const void* data, size_t size);
void Put(std::vector<unsigned char>& bytes, const void* data, size_t size);
template <class T> void Number(std::vector<unsigned char>& b, T n) {
    static_assert(std::is_integral_v<T>);
    Put(b, &n, sizeof(n));
}
template <class T> T At(const std::vector<unsigned char>& b, size_t p) {
    Need(p <= b.size() && sizeof(T) <= b.size() - p, "Truncated binary data");
    T n{};
    std::memcpy(&n, b.data() + p, sizeof(n));
    return n;
}

void Store32(unsigned char* bytes, uint32_t value);
void ReportAt(const Report& report, const wchar_t* stage, const fs::path& current,
              uint64_t completed, uint64_t total);
std::string ReadText(const fs::path& path, uint64_t limit = 128 * 1024 * 1024);
J ReadJson(const fs::path& path);
std::vector<unsigned char> ReadAt(HANDLE handle, uint64_t offset, uint32_t length);
void Write(HANDLE handle, const void* data, size_t length);
void Atomic(const fs::path& target, const void* data, size_t length);
void Atomic(const fs::path& target, const std::string& text);
void Atomic(const fs::path& target, const std::vector<unsigned char>& bytes);
fs::path Safe(const fs::path& root, const std::string& name = {});
uint64_t Ticks(HANDLE handle);
std::string Lower(std::string value);
struct Entry {
    uint32_t ordinal, packed, size, compression;
    uint64_t offset;
    std::array<unsigned char, 288> metadata;
};
struct Source {
    std::string path, group;
    uint32_t kind;
    uint64_t size, ticks, indexSize;
    Hash hash, indexHash;
    std::vector<Entry> entries;
    uint32_t part = 0;
    Handle lock;
};
struct Row {
    uint32_t source, id;
    std::string path;
    uint64_t offset, storageSize;
    uint32_t size;
    Hash hash;
    std::array<unsigned char, 288> metadata;
};

J SourceJson(const Source& source);
std::vector<Source> Collect(const fs::path& game, uint32_t id, bool dlc, const Report& report,
                            const Cancel& cancel);
void Decode(HANDLE handle, const Entry& entry,
            const std::function<void(const unsigned char*, size_t)>& emit, const Cancel& cancel);
bool ValidFile(const fs::path& root, const Row& row);
std::vector<unsigned char> Manifest(uint32_t id, const std::vector<Source>& sources,
                                    const std::vector<Row>& rows);
const char* Recipe(TransformProfile profile);
const char* DirectorySuffix(TransformProfile profile);
const advcg::Spec* SelectedHalf(const Source& source, const Entry& entry, uint32_t game,
                                TransformProfile profile);
std::vector<unsigned char> HalfMa(const std::vector<unsigned char>& source,
                                  const advcg::Spec& spec);
void AppendRows(const fs::path& root, const char* scope, const Source& source, uint32_t sourceIndex,
                uint32_t game, TransformProfile profile, bool exclude, bool allowWrite,
                std::vector<Row>& all, Result& result, const Report& report, const Cancel& cancel);
J Exclusions(uint32_t id, const fs::path& game);
J Owner(uint32_t id, const fs::path& game, const J& exclusions, TransformProfile profile);
TransformProfile ReadOwner(const fs::path& owner, uint32_t id, const fs::path& game,
                           const J* sourceExclusions = nullptr);
void CheckOutput(const fs::path& game, const fs::path& output);
} // namespace rebirths::prepare::assetprep
