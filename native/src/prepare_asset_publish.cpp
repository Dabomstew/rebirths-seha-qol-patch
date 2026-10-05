#include "prepare_asset_internal.hpp"
#include <cstdio>
#include <algorithm>

namespace rebirths::prepare::assetprep {
J SourceJson(const Source& s) {
    return J::Object{{"path", s.path},      {"group", s.group},
                     {"kind", s.kind},      {"size", s.size},
                     {"ticks", s.ticks},    {"index_size", s.indexSize},
                     {"hash", Hex(s.hash)}, {"index_hash", Hex(s.indexHash)}};
}
J RowJson(const Row& r) {
    return J::Object{{"source", r.source},
                     {"id", r.id},
                     {"path", r.path},
                     {"offset", r.offset},
                     {"storage_size", r.storageSize},
                     {"size", r.size},
                     {"hash", Hex(r.hash)},
                     {"metadata", Hex(r.metadata)}};
}
Row ParseRow(const J& j) {
    Row r{};
    r.source = static_cast<uint32_t>(j.Get("source").N());
    r.id = static_cast<uint32_t>(j.Get("id").N());
    r.path = j.Get("path").S();
    Safe(fs::path(L"."), r.path);
    r.offset = j.Get("offset").N();
    r.storageSize = j.Get("storage_size").N();
    r.size = static_cast<uint32_t>(j.Get("size").N());
    r.hash = Unhex(j.Get("hash").S());
    auto m = j.Get("metadata").S();
    Need(m.size() == 576, "Invalid metadata hex");
    for (size_t k = 0; k < 288; k++) {
        std::string two = m.substr(k * 2, 2);
        r.metadata[k] = static_cast<unsigned char>(std::stoul(two, nullptr, 16));
    }
    return r;
}
std::string Identity(const Source& s) {
    auto value = s.path + Hex(s.hash) + "1" + std::to_string(PackLimit);
    return Hex(Digest(value.data(), value.size()));
}
void AppendRows(const fs::path& root, const char* scope, const Source& s, uint32_t sourceIndex,
                uint32_t game, TransformProfile profile, bool exclude, bool allowWrite,
                std::vector<Row>& all, Result& result, const Report& report, const Cancel& cancel) {
    const char* recipe = Recipe(profile);
    auto identity = Identity(s);
    if (profile == TransformProfile::AdvCgHalf24V1)
        identity.resize(32); // Full source hash stays in the journal.
    auto directoryName = std::string(scope) + "/" + identity + DirectorySuffix(profile);
    auto dir = Safe(root, directoryName);
    if (allowWrite)
        fs::create_directories(dir);
    auto journal = Safe(dir, "complete.json");
    if (fs::exists(journal)) {
        auto j = ReadJson(journal);
        Need(j.Get("source") == SourceJson(s) && j.Get("backend").N() == 1 &&
                 (profile == TransformProfile::Raw || j.Get("transform").S() == recipe),
             "Resume source mismatch");
        auto& files = j.Get("files").A();
        size_t expected = 0;
        for (const auto& e : s.entries)
            if (!(exclude && e.ordinal == 553))
                expected++;
        Need(files.size() == expected, "Incomplete archive journal");
        size_t at = 0;
        for (const auto& e : s.entries) {
            if (exclude && e.ordinal == 553)
                continue;
            auto row = ParseRow(files[at++]);
            auto spec = SelectedHalf(s, e, game, profile);
            Need(row.id == (s.part << 16 | e.ordinal) &&
                     row.size == (spec ? spec->outputSize : e.size) && row.metadata == e.metadata &&
                     row.path.rfind(directoryName + "/", 0) == 0 && row.storageSize <= PackLimit &&
                     row.offset <= row.storageSize && row.size <= row.storageSize - row.offset,
                 "Resume identity mismatch");
            if (spec)
                Need(Hex(row.hash) == spec->outputHash, "ADV CG prepared output hash changed");
            Need(ValidFile(root, row), "Existing output differs; choose a fresh output directory");
            row.source = sourceIndex;
            all.push_back(row);
            result.reused++;
        }
        return;
    }
    Need(allowWrite, "Preparation journal missing");
    std::vector<const Entry*> entries;
    entries.reserve(s.entries.size());
    for (const auto& e : s.entries)
        if (!(exclude && e.ordinal == 553))
            entries.push_back(&e);
    std::vector<Row> rows;
    size_t cursor = 0;
    uint32_t partNumber = 0;
    while (cursor < entries.size()) {
        CheckCancel(cancel);
        size_t end = cursor;
        uint64_t length = 20;
        while (end < entries.size() && end - cursor < 65536) {
            auto spec = SelectedHalf(s, *entries[end], game, profile);
            auto size = spec ? spec->outputSize : entries[end]->size;
            if (length + 288 + size > PackLimit)
                break;
            length += 288 + size;
            end++;
        }
        Need(end > cursor, "Single asset exceeds generated PAC bound");
        char name[32]{};
        std::snprintf(name, sizeof(name), "pack%05u.pac", partNumber++);
        const std::string relative = directoryName + "/" + name;
        auto final = Safe(root, relative);
        auto temporary = final;
        temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId());
        Need(!fs::exists(temporary), "Interrupted temporary pack exists");
        Handle output(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr));
        Need(output.value != INVALID_HANDLE_VALUE, "Cannot create raw PAC");
        std::vector<Row> batch;
        batch.reserve(end - cursor);
        try {
            unsigned char header[20] = {'D', 'W', '_', 'P', 'A', 'C', 'K', 0};
            Store32(header + 8, 0);
            Store32(header + 12, static_cast<uint32_t>(end - cursor));
            Store32(header + 16, partNumber - 1);
            Write(output.value, header, 20);
            uint32_t relativeOffset = 0;
            for (size_t i = cursor; i < end; i++) {
                const Entry& e = *entries[i];
                auto meta = e.metadata;
                auto spec = SelectedHalf(s, e, game, profile);
                auto size = spec ? spec->outputSize : e.size;
                Store32(meta.data() + 4, static_cast<uint32_t>(i - cursor));
                Store32(meta.data() + 272, size);
                Store32(meta.data() + 276, size);
                Store32(meta.data() + 280, 0);
                Store32(meta.data() + 284, relativeOffset);
                Write(output.value, meta.data(), 288);
                relativeOffset += size;
            }
            uint64_t offset = 20 + uint64_t(end - cursor) * 288;
            for (size_t i = cursor; i < end; i++) {
                CheckCancel(cancel);
                const Entry& e = *entries[i];
                ReportAt(report, L"Preparing asset archive", fs::path(s.path), i, s.entries.size());
                Sha hash;
                uint64_t size = 0;
                const auto spec = SelectedHalf(s, e, game, profile);
                if (spec) {
                    std::vector<unsigned char> decoded;
                    decoded.reserve(e.size);
                    Decode(
                        s.lock.value, e,
                        [&](const unsigned char* p, size_t n) {
                            decoded.insert(decoded.end(), p, p + n);
                        },
                        cancel);
                    auto transformed = HalfMa(decoded, *spec);
                    Write(output.value, transformed.data(), transformed.size());
                    hash.Add(transformed.data(), transformed.size());
                    size = transformed.size();
                } else
                    Decode(
                        s.lock.value, e,
                        [&](const unsigned char* p, size_t n) {
                            Write(output.value, p, n);
                            hash.Add(p, n);
                            size += n;
                        },
                        cancel);
                Need(size == (spec ? spec->outputSize : e.size), "Prepared byte count mismatch");
                Row r{};
                r.source = sourceIndex;
                r.id = (s.part << 16) | e.ordinal;
                r.path = relative;
                r.offset = offset;
                r.size = static_cast<uint32_t>(size);
                r.hash = hash.Finish();
                r.metadata = e.metadata;
                batch.push_back(r);
                offset += size;
            }
            Need(offset <= PackLimit, "Generated PAC exceeds bound");
            for (auto& r : batch)
                r.storageSize = offset;
            Need(FlushFileBuffers(output.value) != 0, "Cannot flush raw PAC");
            output.Reset();
            if (fs::exists(final)) {
                auto a = rebirths::assets::OpenRead(final),
                     b = rebirths::assets::OpenRead(temporary);
                Need(rebirths::assets::FileSize(a.value) == offset &&
                         rebirths::assets::HashRange(a.value, 0, offset) ==
                             rebirths::assets::HashRange(b.value, 0, offset),
                     "Conflicting output preserved");
                b.Reset();
                DeleteFileW(temporary.c_str());
            } else
                Need(MoveFileExW(temporary.c_str(), final.c_str(), MOVEFILE_WRITE_THROUGH) != 0,
                     "Cannot publish raw PAC");
            for (const auto& r : batch)
                Need(ValidFile(root, r), "Published raw PAC failed reread");
            rows.insert(rows.end(), batch.begin(), batch.end());
        } catch (...) {
            output.Reset();
            DeleteFileW(temporary.c_str());
            throw;
        }
        cursor = end;
    }
    J::Array files;
    for (const auto& r : rows)
        files.push_back(RowJson(r));
    J::Object finished{{"source", SourceJson(s)}, {"backend", 1}, {"files", files}};
    if (profile != TransformProfile::Raw)
        finished["transform"] = recipe;
    Atomic(journal, JsonText(finished) + "\n");
    all.insert(all.end(), rows.begin(), rows.end());
}
} // namespace rebirths::prepare::assetprep
