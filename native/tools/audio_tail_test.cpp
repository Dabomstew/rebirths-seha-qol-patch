#include "audio_tail_profiles.hpp"
#include "platform_util.hpp"
#include <cassert>
#include <cstdio>
#include <iterator>
#include <set>
#include <vector>

void CheckProfile(const rebirths::audio_tail::Profile& profile) {
    using namespace rebirths::audio_tail;
    const size_t size = profile.bankSize;
    const auto* PacketWords = profile.words;
    const size_t count = profile.wordCount;
    std::vector<unsigned char> bytes(size);
    std::set<uint32_t> offsets;
    for (size_t i = 0; i < count; ++i) {
        const auto& w = PacketWords[i];
        assert(w.offset < size - 4 && w.offset % 4 == 0 && w.replacement < w.expected);
        assert(offsets.insert(w.offset).second);
    }
    Write(bytes.data(), PacketWords, count, true);
    assert(Validate(bytes.data(), bytes.size(), PacketWords, count));
    auto bad = PacketWords[count - 1];
    bad.expected ^= 1;
    assert(!Validate(bytes.data(), bytes.size(), &bad, 1));
    bad.offset = static_cast<uint32_t>(size - 2);
    assert(!Validate(bytes.data(), bytes.size(), &bad, 1));
    assert(!Validate(bytes.data(), 3, PacketWords, 1));
    Write(bytes.data(), PacketWords, count);
    assert(!Validate(bytes.data(), bytes.size(), PacketWords, count));
    for (size_t i = 0; i < count; ++i) {
        const auto& w = PacketWords[i];
        uint32_t actual; std::memcpy(&actual, bytes.data() + w.offset, 4);
        assert(actual == w.replacement);
    }
    Write(bytes.data(), PacketWords, count, true);
    assert(Validate(bytes.data(), bytes.size(), PacketWords, count));

}

int main() {
    using namespace rebirths::audio_tail;
    static_assert(std::size(Profiles) == 2);
    static_assert(rebirth2::CorrectedWaves == 544 && std::size(rebirth2::PacketWords) == 1632);
    static_assert(rebirth3::CorrectedWaves == 67 && std::size(rebirth3::PacketWords) == 201);
    assert(Profiles[0].game == rebirths::GameId::Rebirth2 && Profiles[0].mapCall == 0x2ab4cf && Profiles[0].mapIat == 0x33b0a4);
    assert(Profiles[1].game == rebirths::GameId::Rebirth3 && Profiles[1].mapCall == 0x2ed12f && Profiles[1].mapIat == 0x3830a4);
    for (const auto& profile : Profiles) CheckProfile(profile);

    // Exercise the game's PAGE_READONLY section / FILE_MAP_COPY contract.
    wchar_t directory[MAX_PATH]{}, path[MAX_PATH]{};
    assert(GetTempPathW(MAX_PATH, directory));
    assert(GetTempFileNameW(directory, L"rba", 0, path));
    {
        rebirths::platform::Handle file(CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                                   OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY, nullptr));
        assert(file);
        uint32_t original[4] = {100, 0, 0, 80}; DWORD done = 0;
        assert(WriteFile(file.value, original, sizeof(original), &done, nullptr) && done == sizeof(original));
        rebirths::platform::Handle mapping(CreateFileMappingW(file.value, nullptr, PAGE_READONLY, 0, 0, nullptr));
        assert(mapping);
        void* view = MapViewOfFile(mapping.value, FILE_MAP_COPY, 0, 0, 0);
        assert(view);
        const Word words[] = {{0, 100, 50}, {12, 80, 40}};
        assert(Validate(view, sizeof(original), words, 2));
        Write(view, words, 2);
        assert(static_cast<uint32_t*>(view)[0] == 50 && static_cast<uint32_t*>(view)[3] == 40);
        assert(UnmapViewOfFile(view));
        assert(SetFilePointer(file.value, 0, nullptr, FILE_BEGIN) == 0);
        uint32_t retained[4]{};
        assert(ReadFile(file.value, retained, sizeof(retained), &done, nullptr));
        assert(done == sizeof(retained) && std::memcmp(original, retained, sizeof(original)) == 0);
    }
    assert(DeleteFileW(path));
    std::puts("RB2/RB3 bounds, expected-word, repeat, restore and private-mapping contracts passed");
}
