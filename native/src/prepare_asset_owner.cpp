#include "prepare_asset_internal.hpp"
#include <algorithm>
#include <cwctype>
#include <stdexcept>

namespace rebirths::prepare::assetprep {
bool Within(const fs::path& child, const fs::path& parent) {
    auto c = fs::weakly_canonical(fs::absolute(child)).wstring(),
         p = fs::weakly_canonical(fs::absolute(parent)).wstring();
    std::transform(c.begin(), c.end(), c.begin(), towlower);
    std::transform(p.begin(), p.end(), p.begin(), towlower);
    return c == p || (c.size() > p.size() && c.compare(0, p.size(), p) == 0 &&
                      (p.back() == L'\\' || c[p.size()] == L'\\'));
}
J Rb2Exclusions() {
    return J::Array{J::Object{
        {"source", "data/GAME00001.pac"},
        {"sha256", "a2a8efac6f0eea5be4a5b03816ad543be06fb5ef11ebc049fa54ac85a13dba52"},
        {"ordinal", 553},
        {"reason",
         "Native block 14 needs 77 bytes beyond its declared allocation; full and partial native reads cannot share a deterministic prepared result. See docs/rebirth2/uncompressed-loading/log.md."}}};
}
J Exclusions(uint32_t id, const fs::path& game) {
    if (static_cast<GameId>(id) != GameId::Rebirth2 ||
        !fs::exists(Safe(game, "data/GAME00001.pac")))
        return J::Array{};
    const auto source = Safe(game, "data/GAME00001.pac");
    auto h = rebirths::assets::OpenRead(source);
    auto hash = Hex(rebirths::assets::HashRange(h.value, 0, rebirths::assets::FileSize(h.value)));
    // This later PAC has the same index but repairs the malformed help texture.
    // Every entry decodes within its declared bounds, so it needs no exclusion.
    if (hash == "a5fd419e0d24810e6a96c5e01410ad26351d300b3ee390bec6ac5c1c739b8acf")
        return J::Array{};
    auto exclusions = Rb2Exclusions();
    if (hash != exclusions.A().front().Get("sha256").S())
        throw std::runtime_error("Unsupported Re;Birth2 GAME00001.pac version; report its SHA-256");
    return exclusions;
}
J Owner(uint32_t id, const fs::path& game, const J& exclusions, TransformProfile profile) {
    J::Object o{{"format", 1},
                {"game", id},
                {"backend", 1},
                {"game_directory", Utf8(fs::absolute(game).lexically_normal().wstring())}};
    if (!exclusions.A().empty())
        o["exclusions"] = exclusions;
    if (profile != TransformProfile::Raw)
        o["transform"] = Recipe(profile);
    return o;
}
TransformProfile ReadOwner(const fs::path& owner, uint32_t id, const fs::path& game,
                           const J* sourceExclusions) {
    try {
        const auto document = ReadJson(owner);
        auto profile = TransformProfile::Raw;
        const auto& fields = document.O();
        const auto transform = fields.find("transform");
        if (transform != fields.end()) {
            const auto& recipe = transform->second.S();
            bool supported = false;
            for (auto candidate :
                 {TransformProfile::Rb3Ma123Pilot, TransformProfile::Rb3LargeMaPilot,
                  TransformProfile::AdvCgHalf24V1}) {
                if (recipe == Recipe(candidate)) {
                    profile = candidate;
                    supported = true;
                    break;
                }
            }
            Need(supported, "Unsupported asset transform recipe");
        }
        Need((profile != TransformProfile::Rb3Ma123Pilot &&
              profile != TransformProfile::Rb3LargeMaPilot) ||
                 static_cast<GameId>(id) == GameId::Rebirth3,
             "Pilot MA transform requires Re;Birth3");
        // Settings validate the supported metadata contract without hashing an
        // entire source PAC. Preparation additionally requires source-derived
        // exclusions, so the existing asset ownership check remains exact.
        auto exclusions = sourceExclusions ? *sourceExclusions : J(J::Array{});
        if (!sourceExclusions && fields.count("exclusions")) {
            Need(static_cast<GameId>(id) == GameId::Rebirth2, "Asset exclusions require Re;Birth2");
            exclusions = Rb2Exclusions();
        }
        Need(document == Owner(id, game, exclusions, profile), "Output ownership mismatch");
        return profile;
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("Invalid prepared asset ownership metadata: ") +
                                 error.what());
    }
}
void CheckOutput(const fs::path& game, const fs::path& output) {
    Need(!Within(game, output), "Output cannot contain the game installation");
    for (const auto& name : {"data", "DLC", "DLC_EN", "DLC_JP", "DLC_CN"})
        Need(!Within(output, game / name), "Output overlaps original source tree");
    Safe(output);
}
} // namespace rebirths::prepare::assetprep

namespace rebirths::prepare {
using namespace assetprep;
std::optional<TransformProfile> ReadAssetProfile(const fs::path& selected, const fs::path& output,
                                                 uint32_t id) {
    Need(ValidGameId(static_cast<GameId>(id)), "Unknown game identity");
    const auto game = Safe(selected);
    const auto owner = Safe(output, "owner.json");
    if (!fs::exists(owner))
        return std::nullopt;
    return ReadOwner(owner, id, game);
}
} // namespace rebirths::prepare
