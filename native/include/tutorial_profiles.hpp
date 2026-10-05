#pragma once
#include "rebirths_patch.hpp"

namespace rebirths::tutorials {
// Independent executable contracts. Only the automatic VM callsite is patched;
// the shared manager getter and manually opened Help remain native.
struct Profile {
    GameId game;
    uint32_t commandRva, callRva, predicateRva, managerRva, createRva;
    std::array<unsigned char, 5> call;
};
inline constexpr Profile Profiles[] = {
    {GameId::Rebirth1, 0x13cf0, 0x13d05, 0x2e0c0, 0x459208, 0x94cd0, {0xe8,0xb6,0xa3,0x01,0x00}},
    {GameId::Rebirth2, 0x7f610, 0x7f625, 0x99120, 0x4432c8, 0xfb100, {0xe8,0xf6,0x9a,0x01,0x00}},
    {GameId::Rebirth3, 0x82b10, 0x82b25, 0x9c6b0, 0x49a4c8, 0x108b80, {0xe8,0x86,0x9b,0x01,0x00}},
    {GameId::SegaHardGirls, 0x8bc60, 0x8bc75, 0xa57b0, 0x437b88, 0xfa1a0, {0xe8,0x36,0x9b,0x01,0x00}},
};
inline const Profile* For(GameId game) noexcept {
    for (const auto& profile : Profiles) if (profile.game == game) return &profile;
    return nullptr;
}
struct DiscoveryProfile {
    GameId game;
    uint32_t registerRva, lookupRva, saveRva, countOffset, advRva, flagRva;
};
inline constexpr DiscoveryProfile DiscoveryProfiles[] = {
    {GameId::Rebirth1, 0x50420, 0x54590, 0x459248, 0xb48f4, 0x4591c4, 0},
    {GameId::Rebirth2, 0xbac50, 0xbf080, 0x443310, 0x77eec, 0x443284, 0},
    {GameId::Rebirth3, 0xc0520, 0xc5920, 0x4f6ed8, 0xb4678, 0x49a484, 0},
    {GameId::SegaHardGirls, 0xcba30, 0xcf4e0, 0x437bd0, 0xc6144, 0x437b44, 0xc4a10},
};
inline const DiscoveryProfile* DiscoveryFor(GameId game) noexcept {
    for (const auto& profile : DiscoveryProfiles) if (profile.game == game) return &profile;
    return nullptr;
}
struct SeenProfile {GameId game;uint32_t queryRva;};
inline constexpr SeenProfile SeenProfiles[]={
    {GameId::Rebirth1,0x504f0},{GameId::Rebirth2,0xbad20},
    {GameId::Rebirth3,0xc05f0},{GameId::SegaHardGirls,0xcbb00},
};
inline const SeenProfile* SeenFor(GameId game) noexcept {
    for(const auto& profile:SeenProfiles)if(profile.game==game)return &profile;
    return nullptr;
}
struct BlockProfile {
    GameId game;
    uint32_t prepareCallRva, prepareRva, musicRva, stopRva;
    uint32_t cacheSaveRva, cacheTailRva, stateOffset, fadeRva;
    bool hasIntro, idleAllowed;
};
inline constexpr BlockProfile BlockProfiles[]={
    {GameId::Rebirth1,0x20036d,0x200b30,0x12250,0x126a0,0x200367,0x200891,0x84f4,0xf890,false,true},
    {GameId::Rebirth2,0x24ad3b,0x24b500,0x7db70,0x7dfc0,0x24ad2c,0x24b25d,0x84fc,0x7afc0,false,true},
    {GameId::Rebirth3,0x288deb,0x2895b0,0x81070,0x814c0,0x288ddc,0x28930d,0x850c,0x7e4e0,true,false},
    {GameId::SegaHardGirls,0x253e4b,0x254610,0x8a1c0,0x8a610,0x253e3c,0x25436d,0x850c,0x87610,true,false},
};
inline const BlockProfile* BlockFor(GameId game) noexcept {
    for(const auto& profile:BlockProfiles)if(profile.game==game)return &profile;
    return nullptr;
}
}
