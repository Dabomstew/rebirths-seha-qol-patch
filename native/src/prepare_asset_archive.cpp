#include "prepare_asset_internal.hpp"
#include <algorithm>
#include <set>
extern "C" int __cdecl RebirthsDecodeHuffman(const unsigned char*, uint32_t, unsigned char*,
                                             uint32_t) noexcept;

namespace rebirths::prepare::assetprep {
std::string Group(const std::string& p) {
    auto s = Lower(p);
    Need(s.size() > 4, "Invalid archive path");
    if (s.substr(s.size() - 4) == ".cpk")
        return s.substr(0, s.size() - 4);
    Need(s.size() > 9 && s.substr(s.size() - 4) == ".pac", "Invalid source suffix");
    for (size_t at = s.size() - 9; at < s.size() - 4; at++)
        Need(s[at] >= '0' && s[at] <= '9', "Invalid PAC suffix");
    return s.substr(0, s.size() - 9);
}
void InspectPac(Source& s, HANDLE h) {
    Need(s.size >= 20, "Truncated PAC");
    auto header = ReadAt(h, 0, 20);
    Need(!std::memcmp(header.data(), "DW_PACK\0", 8), "Wrong PAC magic");
    uint32_t unknown = At<uint32_t>(header, 8), count = At<uint32_t>(header, 12),
             part = At<uint32_t>(header, 16);
    Need(!unknown && count && count <= 65536 && part <= 65535, "Invalid PAC header");
    Need(part == std::stoul(s.path.substr(s.path.size() - 9, 5)), "PAC part mismatch");
    s.part = part;
    s.indexSize = 20 + uint64_t(count) * 288;
    Need(s.indexSize <= s.size, "Truncated PAC table");
    auto table = ReadAt(h, 20, count * 288);
    for (uint32_t i = 0; i < count; i++) {
        Entry e{};
        e.ordinal = i;
        std::memcpy(e.metadata.data(), table.data() + size_t(i) * 288, 288);
        Need(std::memchr(e.metadata.data() + 8, 0, 260) != nullptr, "Unterminated asset name");
        e.packed = At<uint32_t>(table, size_t(i) * 288 + 272);
        e.size = At<uint32_t>(table, size_t(i) * 288 + 276);
        e.compression = At<uint32_t>(table, size_t(i) * 288 + 280);
        auto relative = At<uint32_t>(table, size_t(i) * 288 + 284);
        Need(e.compression <= 1 && (!e.compression ? e.packed == e.size : true),
             "Unsupported PAC compression");
        Need(e.size < 0x80000000 && uint64_t(relative) <= s.size - s.indexSize &&
                 e.packed <= s.size - s.indexSize - relative,
             "PAC entry out of range");
        e.offset = s.indexSize + relative;
        s.entries.push_back(e);
    }
}
std::vector<Source> Collect(const fs::path& game, uint32_t id, bool dlc, const Report& report,
                            const Cancel& cancel) {
    std::vector<std::string> folders =
        dlc ? (static_cast<GameId>(id) == GameId::SegaHardGirls
                   ? std::vector<std::string>{"DLC_EN", "DLC_JP", "DLC_CN"}
                   : std::vector<std::string>{"DLC"})
            : std::vector<std::string>{"data"};
    std::vector<std::string> names;
    std::set<std::string> seen;
    for (const auto& name : folders) {
        auto root = Safe(game, name);
        if (!fs::exists(root))
            continue;
        Need(fs::is_directory(root), "Source root is not a directory");
        for (const auto& item : fs::recursive_directory_iterator(root)) {
            CheckCancel(cancel);
            Safe(game, item.path().lexically_relative(game).generic_string());
            if (!item.is_regular_file())
                continue;
            auto ext = Lower(item.path().extension().string());
            if (ext != ".pac" && ext != ".cpk")
                continue;
            auto relative = item.path().lexically_relative(game).generic_string();
            Safe(game, relative);
            Need(seen.insert(Lower(relative)).second, "Case-colliding source paths");
            names.push_back(relative);
        }
    }
    std::sort(names.begin(), names.end(),
              [](const auto& a, const auto& b) { return Lower(a) < Lower(b); });
    std::vector<Source> out;
    out.reserve(names.size());
    for (const auto& name : names) {
        CheckCancel(cancel);
        auto path = Safe(game, name);
        ReportAt(report, L"Checking source files", path, out.size(), names.size());
        auto h = rebirths::assets::OpenRead(path);
        Source s{};
        s.path = name;
        s.group = Group(name);
        s.kind = Lower(path.extension().string()) == ".pac" ? 1 : 0;
        s.size = rebirths::assets::FileSize(h.value);
        s.ticks = Ticks(h.value);
        if (s.kind)
            InspectPac(s, h.value);
        else {
            Need(s.size <= 64 * 1024 * 1024, "CPK index exceeds bound");
            s.indexSize = s.size;
        }
        s.indexHash = rebirths::assets::HashRange(h.value, 0, s.indexSize);
        s.hash = rebirths::assets::HashRange(h.value, 0, s.size);
        s.lock = std::move(h);
        out.push_back(std::move(s));
    }
    std::set<std::string> cpk;
    for (const auto& s : out)
        if (!s.kind)
            cpk.insert(s.group);
    for (const auto& s : out)
        if (s.kind)
            Need(cpk.count(s.group) > 0, "PAC namespace lacks CPK index");
    return out;
}
void Decode(HANDLE h, const Entry& e, const std::function<void(const unsigned char*, size_t)>& emit,
            const Cancel& cancel) {
    if (!e.compression) {
        for (uint64_t at = 0; at < e.size;) {
            CheckCancel(cancel);
            auto n = static_cast<uint32_t>(std::min<uint64_t>(Chunk, e.size - at));
            auto b = ReadAt(h, e.offset + at, n);
            emit(b.data(), b.size());
            at += n;
        }
        return;
    }
    Need(e.packed >= 16, "Truncated divided-Huffman header");
    auto hdr = ReadAt(h, e.offset, 16);
    auto magic = At<uint32_t>(hdr, 0), count = At<uint32_t>(hdr, 4), block = At<uint32_t>(hdr, 8),
         header = At<uint32_t>(hdr, 12);
    Need(magic == 0x1234 && block && block <= BlockLimit && count <= 1048576,
         "Invalid divided-Huffman header");
    Need(uint64_t(header) == 16 + uint64_t(count) * 12 && header <= e.packed &&
             count == (uint64_t(e.size) + block - 1) / block,
         "Invalid divided-Huffman descriptors");
    auto table = ReadAt(h, e.offset + 16, count * 12);
    uint64_t written = 0;
    for (uint32_t i = 0; i < count; i++) {
        CheckCancel(cancel);
        auto size = At<uint32_t>(table, size_t(i) * 12),
             packed = At<uint32_t>(table, size_t(i) * 12 + 4),
             relative = At<uint32_t>(table, size_t(i) * 12 + 8);
        Need(size == std::min<uint64_t>(block, e.size - written) && packed &&
                 packed <= BlockLimit && uint64_t(header) + relative + packed <= e.packed,
             "Invalid compressed block");
        auto input = ReadAt(h, e.offset + header + relative, packed);
        std::vector<unsigned char> output(size);
        Need(RebirthsDecodeHuffman(input.data(), packed, output.data(), size) == 1,
             "Malformed Huffman block");
        emit(output.data(), output.size());
        written += size;
    }
    Need(written == e.size, "Decoded size mismatch");
}
} // namespace rebirths::prepare::assetprep
