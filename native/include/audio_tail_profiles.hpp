#pragma once
#include "rebirths_patch.hpp"
#include "rebirth2_audio_tail_table.hpp"
#include "rebirth3_audio_tail_table.hpp"
#include <iterator>

namespace rebirths::audio_tail {
struct Profile {
    GameId game;
    const wchar_t* executable;
    uint32_t mapCall, mapIat;
    size_t bankSize;
    const char* bankHash;
    const Word* words;
    size_t wordCount, correctedWaves;
};
// Each mapping call, import slot and bank identity is independently verified.
inline constexpr Profile Profiles[] = {
    {GameId::Rebirth2, L"NeptuniaReBirth2.exe", 0x2ab4cf, 0x33b0a4, 122159040,
     "23cf2eae562ac576cc0a67d7b307ecbd833cc16dd95daa35686aa6eb2e5852a2",
     rebirth2::PacketWords, std::size(rebirth2::PacketWords), rebirth2::CorrectedWaves},
    {GameId::Rebirth3, L"NeptuniaReBirth3.exe", 0x2ed12f, 0x3830a4, 61778272,
     "d675c09ee467843431b069e6886e299cf970a64185247a2774a05df835ce519b",
     rebirth3::PacketWords, std::size(rebirth3::PacketWords), rebirth3::CorrectedWaves},
};
}
