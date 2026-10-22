#include "prepare_asset_internal.hpp"
#include "prepare_install_internal.hpp"
#include <algorithm>
#include <cwctype>
#include <map>

namespace rebirths::prepare {
namespace {
using namespace assetprep;
std::wstring Fold(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}
fs::path Name(const char* text) {
    const auto n = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    Need(n > 1 && n <= 4096, "Invalid archive filename encoding");
    std::wstring wide(n, L'\0');
    Need(MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, text, -1, wide.data(), n) != 0,
         "Cannot decode archive filename");
    wide.pop_back(); std::replace(wide.begin(), wide.end(), L'\\', L'/');
    while (wide.rfind(L"./", 0) == 0) wide.erase(0, 2);
    while (!wide.empty() && wide.front() == L'/') wide.erase(0, 1);
    Need(!wide.empty() && wide.back() != L'/', "Invalid archive filename");
    fs::path result;
    size_t start = 0;
    while (start < wide.size()) {
        auto end = wide.find(L'/', start); if (end == std::wstring::npos) end = wide.size();
        auto part = wide.substr(start, end - start);
        Need(!part.empty() && part != L"." && part != L".." && part.back() != L'.' && part.back() != L' ' &&
            std::none_of(part.begin(), part.end(), [](wchar_t c) { return c < 32 || std::wcschr(L"\\:*?\"<>|", c); }),
            "Archive filename escapes its loose namespace");
        auto stem = Fold(part.substr(0, part.find(L'.')));
        Need(stem != L"con" && stem != L"prn" && stem != L"aux" && stem != L"nul" &&
            !(stem.size() == 4 && (stem.substr(0, 3) == L"com" || stem.substr(0, 3) == L"lpt") &&
              stem[3] >= L'1' && stem[3] <= L'9'), "Reserved archive filename");
        result /= part; start = end + 1;
    }
    return result;
}
// The CPK contains a divided-Huffman encoded native name index: DW_PACK,
// 65536 (start,count) buckets, then 0x108-byte (name[260],part/entry) records.
// Use each bucket's first matching name, exactly as native resolution does.
std::map<std::wstring, uint32_t> Index(const Source& source, const Cancel& cancel) {
    auto header = ReadAt(source.lock.value, 0, 16);
    Entry encoded{};
    encoded.packed = At<uint32_t>(header, 4); encoded.size = At<uint32_t>(header, 8);
    encoded.compression = At<uint32_t>(header, 12); encoded.offset = 16;
    Need(encoded.compression <= 1 && source.size >= 16 && encoded.packed == source.size - 16 &&
         encoded.size <= 128 * 1024 * 1024, "Invalid native name index envelope");
    std::vector<unsigned char> bytes; bytes.reserve(encoded.size);
    Decode(source.lock.value, encoded, [&](const unsigned char* p, size_t n) { Put(bytes, p, n); }, cancel);
    constexpr size_t records = 16 + 65536 * 8;
    Need(bytes.size() >= records && !std::memcmp(bytes.data(), "DW_PACK\0", 8) &&
         (bytes.size() - records) % 264 == 0, "Invalid native name index");
    const auto count = (bytes.size() - records) / 264;
    std::map<std::wstring, uint32_t> result;
    for (size_t bucket = 0; bucket < 65536; ++bucket) {
        const auto start = At<uint32_t>(bytes, 16 + bucket * 8), n = At<uint32_t>(bytes, 20 + bucket * 8);
        if (!n) continue;
        Need(start < count && n <= count - start, "Native name index bucket bound");
        for (size_t i = start; i < size_t(start) + n; ++i) {
            const auto offset = records + i * 264;
            Need(std::memchr(bytes.data() + offset, 0, 260) != nullptr, "Unterminated indexed filename");
            result.emplace(Fold(Name(reinterpret_cast<const char*>(bytes.data() + offset)).generic_wstring()),
                           At<uint32_t>(bytes, offset + 260));
        }
    }
    return result;
}
struct Job { const Source* source; const Entry* entry; fs::path relative; };
}
static Result ProcessLooseAssets(const fs::path& selected, const fs::path& output, uint32_t id,
                                 const Report& report, const Cancel& cancel, bool write) {
    using namespace assetprep;
    Need(ValidGameId(static_cast<GameId>(id)), "Unknown extraction game");
    const auto game = Safe(selected), root = fs::absolute(output).lexically_normal();
    CheckOutput(game, root); CheckCancel(cancel);
    auto sources = Collect(game, id, false, report, cancel);
    auto dlc = Collect(game, id, true, report, cancel);
    for (auto& s : dlc) sources.push_back(std::move(s));
    std::map<std::wstring, std::vector<Job>> candidates;
    std::map<std::string, const Source*> indexes;
    Result result;
    const auto exclusions = Rb2Exclusions();
    for (const auto& source : sources) {
        if (!source.kind) { indexes.emplace(source.group, &source); continue; }
        for (const auto& entry : source.entries) {
            // One original RB2 texture has no deterministic bounded native
            // decode. Preserve its existing documented original-PAC fallback.
            bool excluded = false;
            if (id == 1) for (const auto& e : exclusions.A())
                if (source.path == e.Get("source").S() && Hex(source.hash) == e.Get("sha256").S() &&
                    entry.ordinal == e.Get("ordinal").N()) excluded = true;
            if (excluded) {
                ++result.skipped; result.fullCoverage = false;
                ReportAt(report, L"Keeping original archive for an unreadable file", fs::path(source.path), entry.ordinal, source.entries.size());
                continue;
            }
            auto relative = fs::path(source.group) / Name(reinterpret_cast<const char*>(entry.metadata.data() + 8));
            install::SafeBelow(root, root / relative);
            candidates[Fold(relative.generic_wstring())].push_back({&source, &entry, relative});
        }
    }
    Need(!candidates.empty(), "No assets to extract");
    std::map<std::string, std::map<std::wstring, uint32_t>> resolved;
    std::vector<Job> jobs;
    for (const auto& pair : candidates) {
        CheckCancel(cancel);
        auto chosen = pair.second.front();
        if (pair.second.size() > 1) {
            const auto& group = chosen.source->group;
            if (!resolved.count(group)) resolved.emplace(group, Index(*indexes.at(group), cancel));
            const auto name = Fold(Name(reinterpret_cast<const char*>(chosen.entry->metadata.data() + 8)).generic_wstring());
            const auto found = resolved.at(group).find(name);
            Need(found != resolved.at(group).end(), "Duplicate archive name missing from native index");
            bool matched = false;
            for (const auto& candidate : pair.second)
                if ((candidate.source->part << 16 | candidate.entry->ordinal) == found->second) {
                    chosen = candidate; matched = true; break;
                }
            Need(matched, "Duplicate archive name index does not match installed PACs");
        }
        jobs.push_back(chosen);
    }
    uint64_t needed = 0;
    for (const auto& job : jobs) {
        result.bytes += job.entry->size;
        if (!fs::exists(root / job.relative)) needed += job.entry->size;
    }
    if (!write) { result.files = jobs.size(); return result; }
    auto available = root;
    while (!fs::exists(available)) available = available.parent_path();
    Need(fs::space(available).available >= needed, "Not enough free space for loose-file extraction");
    for (const auto& job : jobs) {
        CheckCancel(cancel);
        const auto target = install::SafeBelow(root, root / job.relative);
        ReportAt(report, L"Extracting loose files", target, result.files, jobs.size());
        if (fs::exists(target)) {
            Need(fs::is_regular_file(target), "Loose-file destination is not a file");
            ++result.reused; ++result.files; continue; // Existing mods are trusted, never rehashed/overwritten.
        }
        fs::create_directories(target.parent_path());
        const auto temporary = target.wstring() + L".extract-" + install::Stamp();
        Handle stream(CreateFileW(rebirths::assets::Extended(temporary).c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        Need(bool(stream), "Cannot create temporary loose file");
        try {
            uint64_t written = 0;
            Decode(job.source->lock.value, *job.entry, [&](const unsigned char* p, size_t n) {
                Write(stream.value, p, n); written += n;
            }, cancel);
            Need(written == job.entry->size && FlushFileBuffers(stream.value), "Cannot finish extracted file");
            stream.Reset(); CheckCancel(cancel);
            install::SafeBelow(root, target);
            if (!MoveFileExW(rebirths::assets::Extended(temporary).c_str(), rebirths::assets::Extended(target).c_str(), MOVEFILE_WRITE_THROUGH)) {
                const auto error = GetLastError();
                Need((error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) && fs::is_regular_file(target),
                     "Cannot publish extracted file");
                ++result.reused; // A concurrently added mod wins too.
                Need(DeleteFileW(rebirths::assets::Extended(temporary).c_str()) != 0, "Cannot remove owned extraction temporary");
            }
            ++result.files;
        } catch (...) {
            stream.Reset(); DeleteFileW(rebirths::assets::Extended(temporary).c_str()); throw;
        }
    }
    ReportAt(report, L"Extraction complete", root, result.files, jobs.size());
    return result;
}
Result ExtractLooseAssets(const fs::path& game, const fs::path& output, uint32_t id,
                          const Report& report, const Cancel& cancel) {
    return ProcessLooseAssets(game, output, id, report, cancel, true);
}
Result InspectLooseAssets(const fs::path& game, const fs::path& output, uint32_t id,
                          const Report& report, const Cancel& cancel) {
    return ProcessLooseAssets(game, output, id, report, cancel, false);
}
} // namespace rebirths::prepare
