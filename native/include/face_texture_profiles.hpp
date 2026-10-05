#pragma once
#include "rebirths_patch.hpp"

namespace rebirths::face_textures {
struct SurfaceCall { uint32_t rva; std::array<unsigned char, 5> bytes; };
// Independently validated constants; adapter behavior is shared with SeHa.
struct Profile {
    GameId game;
    uint32_t surfaceRva, finishRva, finishIatRva;
    std::array<SurfaceCall, 2> calls;
};
inline constexpr Profile SegaProfile{GameId::SegaHardGirls, 0x22f490, 0x2c43f9, 0x346408, {{
    {0x2456f4, {0xe8,0x97,0x9d,0xfe,0xff}},
    {0x245714, {0xe8,0x77,0x9d,0xfe,0xff}},
}}};
inline constexpr Profile Rebirth3Profile{GameId::Rebirth3, 0x2860f0, 0x2f8934, 0x38340c, {{
    {0x2795cf, {0xe8,0x1c,0xcb,0x00,0x00}},
    {0x2795ef, {0xe8,0xfc,0xca,0x00,0x00}},
}}};
inline const Profile* ProfileFor(GameId game) noexcept {
    if (game == GameId::SegaHardGirls) return &SegaProfile;
    if (game == GameId::Rebirth3) return &Rebirth3Profile;
    // RB1/RB2 native blank initializers have no completion wait to omit.
    return nullptr;
}
}
