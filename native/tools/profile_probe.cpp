#include "texture_conversion_profiles.hpp"
#include "face_texture_profiles.hpp"
#include "tutorial_profiles.hpp"
#include "enemy_turn_profiles.hpp"
#include <cstdio>

namespace {
template <size_t N> void Bytes(const std::array<unsigned char, N>& bytes) {
    std::putchar('[');
    for (size_t i = 0; i < N; ++i)
        std::printf("%s%u", i ? "," : "", static_cast<unsigned>(bytes[i]));
    std::putchar(']');
}
template <class Rows> void RowsJson(const Rows& rows) {
    std::putchar('[');
    bool comma = false;
    for (const auto& row : rows) {
        std::printf("%s{\"rva\":%u,\"bytes\":", comma ? "," : "", row.rva);
        Bytes(row.bytes);
        std::putchar('}');
        comma = true;
    }
    std::putchar(']');
}
} // namespace
int main() {
    using rebirths::GameId;
    const GameId games[]{GameId::Rebirth1, GameId::Rebirth2, GameId::Rebirth3,
                         GameId::SegaHardGirls};
    const char* names[]{"rebirth1", "rebirth2", "rebirth3", "sega-hard-girls"};
    std::printf("{\"schema\":1,\"games\":{");
    for (size_t i = 0; i < 4; ++i) {
        std::printf("%s\"%s\":{\"texture\":{", i ? "," : "", names[i]);
        const auto& texture = *rebirths::textures::ProfileFor(games[i]);
        std::printf("\"game\":%u,\"cookie\":%u,\"helpers\":", static_cast<unsigned>(texture.game),
                    texture.cookieRva);
        RowsJson(texture.helpers);
        std::printf(",\"calls\":");
        RowsJson(texture.calls);
        std::printf("},\"face\":");
        const auto* face = rebirths::face_textures::ProfileFor(games[i]);
        if (face) {
            std::printf("{\"surface\":%u,\"finish\":%u,\"iat\":%u,\"calls\":", face->surfaceRva,
                        face->finishRva, face->finishIatRva);
            RowsJson(face->calls);
            std::putchar('}');
        } else
            std::printf("null");
        const auto& tutorial = *rebirths::tutorials::For(games[i]);
        const auto& discovery = *rebirths::tutorials::DiscoveryFor(games[i]);
        const auto& block = *rebirths::tutorials::BlockFor(games[i]);
        std::printf(",\"tutorial\":{\"core\":[%u,%u,%u,%u,%u],\"call\":", tutorial.commandRva,
                    tutorial.callRva, tutorial.predicateRva, tutorial.managerRva,
                    tutorial.createRva);
        Bytes(tutorial.call);
        std::printf(",\"discovery\":[%u,%u,%u,%u,%u,%u],\"seen\":%u,", discovery.registerRva,
                    discovery.lookupRva, discovery.saveRva, discovery.countOffset, discovery.advRva,
                    discovery.flagRva, rebirths::tutorials::SeenFor(games[i])->queryRva);
        std::printf("\"block\":[%u,%u,%u,%u,%u,%u,%u,%u],\"intro\":%s,\"idle\":%s}",
                    block.prepareCallRva, block.prepareRva, block.musicRva, block.stopRva,
                    block.cacheSaveRva, block.cacheTailRva, block.stateOffset, block.fadeRva,
                    block.hasIntro ? "true" : "false", block.idleAllowed ? "true" : "false");
        const auto& enemy = *rebirths::enemy_turn::For(games[i]);
        std::printf(",\"enemy_turn\":{\"call\":%u,\"start\":%u,\"planner\":%u,\"busy\":%u,\"bytes\":",
            enemy.callRva,enemy.startRva,enemy.plannerOffset,enemy.busyOffset);
        Bytes(enemy.original);
        std::printf(",\"setup\":");Bytes(enemy.setup);
        std::printf(",\"creator\":%u,\"callback\":%u,\"trampoline\":%u,\"query\":%u,\"battle\":%u,\"owned_calls\":[%u,%u,%u,%u]",
            enemy.creatorRva,enemy.callbackRva,enemy.trampolineRva,enemy.busyRva,enemy.battleAccessorRva,
            enemy.ownedCalls[0],enemy.ownedCalls[1],enemy.ownedCalls[2],enemy.ownedCalls[3]);
        std::printf("}}");
    }
    std::puts("}}");
    return 0;
}
