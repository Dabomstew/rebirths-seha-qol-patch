#include "prepare_asset_internal.hpp"
#include <algorithm>
#include <stdexcept>

namespace rebirths::prepare::assetprep {
void Need(bool good, const char* why) {
    if (!good)
        throw std::runtime_error(why);
}
void CheckCancel(const Cancel& cancel) {
    if (cancel && cancel())
        throw std::runtime_error("Cancelled; completed source archives remain resumable");
}
Hash Digest(const void* p, size_t n) {
    Sha s;
    s.Add(p, n);
    return s.Finish();
}
void Put(std::vector<unsigned char>& b, const void* p, size_t n) {
    const auto* c = static_cast<const unsigned char*>(p);
    b.insert(b.end(), c, c + n);
}
void Store32(unsigned char* p, uint32_t n) {
    std::memcpy(p, &n, 4);
}
void ReportAt(const Report& report, const wchar_t* stage, const fs::path& current,
              uint64_t completed, uint64_t total) {
    if (report)
        report({stage, current.wstring(), completed, total});
}
std::string ReadText(const fs::path& path, uint64_t limit) {
    auto h = rebirths::assets::OpenRead(path);
    auto size = rebirths::assets::FileSize(h.value);
    Need(size <= limit, "Input file too large");
    std::string s(static_cast<size_t>(size), '\0');
    if (size) {
        DWORD got = 0;
        Need(ReadFile(h.value, s.data(), static_cast<DWORD>(size), &got, nullptr) && got == size,
             "Cannot read input");
    }
    return s;
}
J ReadJson(const fs::path& p) {
    return ParseJson(ReadText(p));
}
std::vector<unsigned char> ReadAt(HANDLE h, uint64_t offset, uint32_t n) {
    Need(offset <= INT64_MAX, "File offset out of range");
    LARGE_INTEGER at{};
    at.QuadPart = static_cast<LONGLONG>(offset);
    Need(SetFilePointerEx(h, at, nullptr, FILE_BEGIN) != 0, "Cannot seek source");
    std::vector<unsigned char> b(n);
    DWORD got = 0;
    Need(!n || (ReadFile(h, b.data(), n, &got, nullptr) && got == n), "Truncated source");
    return b;
}
void Write(HANDLE h, const void* data, size_t length) {
    const auto* p = static_cast<const unsigned char*>(data);
    while (length) {
        auto n = static_cast<DWORD>(std::min<size_t>(length, Chunk));
        DWORD done = 0;
        Need(WriteFile(h, p, n, &done, nullptr) && done == n, "Cannot write output");
        p += n;
        length -= n;
    }
}
fs::path Safe(const fs::path& root, const std::string& name) {
    return rebirths::assets::CheckedPath(root, name);
}
uint64_t Ticks(HANDLE h) {
    BY_HANDLE_FILE_INFORMATION i{};
    Need(GetFileInformationByHandle(h, &i) != 0, "Cannot inspect source");
    return (uint64_t(i.ftLastWriteTime.dwHighDateTime) << 32) | i.ftLastWriteTime.dwLowDateTime;
}
std::string Lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c + 32);
    return s;
}
bool ValidFile(const fs::path& root, const Row& r) {
    try {
        auto h = rebirths::assets::OpenRead(Safe(root, r.path));
        return rebirths::assets::FileSize(h.value) == r.storageSize &&
               rebirths::assets::HashRange(h.value, r.offset, r.size) == r.hash;
    } catch (...) {
        return false;
    }
}
std::vector<unsigned char> Manifest(uint32_t id, const std::vector<Source>& sources,
                                    const std::vector<Row>& rows) {
    std::vector<unsigned char> out;
    Put(out, "RBAST001", 8);
    Number<uint32_t>(out, id);
    Number<uint32_t>(out, 1);
    Number<uint32_t>(out, static_cast<uint32_t>(sources.size()));
    Number<uint32_t>(out, static_cast<uint32_t>(rows.size()));
    auto str = [&](const std::string& s) {
        Safe(fs::path(L"."), s);
        Number<uint32_t>(out, static_cast<uint32_t>(s.size()));
        Put(out, s.data(), s.size());
    };
    for (const auto& s : sources) {
        str(s.path);
        str(s.group);
        Number<uint32_t>(out, s.kind);
        Number<uint64_t>(out, s.size);
        Number<uint64_t>(out, s.ticks);
        Number<uint64_t>(out, s.indexSize);
        Put(out, s.hash.data(), 32);
        Put(out, s.indexHash.data(), 32);
    }
    for (const auto& r : rows) {
        Number<uint32_t>(out, r.source);
        Number<uint32_t>(out, r.id);
        str(r.path);
        Number<uint64_t>(out, r.offset);
        Number<uint64_t>(out, r.storageSize);
        Number<uint32_t>(out, r.size);
        Put(out, r.hash.data(), 32);
        Put(out, r.metadata.data(), 288);
    }
    Need(out.size() <= 128 * 1024 * 1024, "Manifest exceeds bound");
    auto hash = Digest(out.data(), out.size());
    Put(out, hash.data(), 32);
    return out;
}
void Atomic(const fs::path& target, const void* data, size_t length) {
    auto temp = target;
    temp += L".tmp-" + std::to_wstring(GetCurrentProcessId());
    Need(!fs::exists(temp), "Interrupted temporary output exists");
    Handle h(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                         nullptr));
    Need(h.value != INVALID_HANDLE_VALUE, "Cannot create temporary output");
    try {
        Write(h.value, data, length);
        Need(FlushFileBuffers(h.value) != 0, "Cannot flush output");
        h.Reset();
        Need(MoveFileExW(temp.c_str(), target.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
             "Cannot publish output");
    } catch (...) {
        h.Reset();
        DeleteFileW(temp.c_str());
        throw;
    }
}
void Atomic(const fs::path& p, const std::string& s) {
    Atomic(p, s.data(), s.size());
}
void Atomic(const fs::path& p, const std::vector<unsigned char>& b) {
    Atomic(p, b.data(), b.size());
}
} // namespace rebirths::prepare::assetprep
