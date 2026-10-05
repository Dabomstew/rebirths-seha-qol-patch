#include "prepare_asset_internal.hpp"
#include <algorithm>
#include <stdexcept>

namespace rebirths::prepare::assetprep {
const char* Recipe(TransformProfile p) {
    switch (p) {
    case TransformProfile::Raw:
        return "";
    case TransformProfile::Rb3Ma123Pilot:
        return "rb3-ma123-half-box-v1";
    case TransformProfile::Rb3LargeMaPilot:
        return "rb3-ma106-107-123-half-box-v2";
    case TransformProfile::AdvCgHalf24V1:
        return "adv-cg-half24-box-v1";
    }
    throw std::runtime_error("Unknown transform profile");
}
const char* DirectorySuffix(TransformProfile p) {
    switch (p) {
    case TransformProfile::Raw:
        return "";
    case TransformProfile::Rb3Ma123Pilot:
        return "-ma123-half-v1";
    case TransformProfile::Rb3LargeMaPilot:
        return "-ma106-107-123-half-v2";
    case TransformProfile::AdvCgHalf24V1:
        return ""; // Owner recipe and separate root distinguish this profile.
    }
    throw std::runtime_error("Unknown transform profile");
}
const advcg::Spec* SelectedHalf(const Source& s, const Entry& e, uint32_t game,
                                TransformProfile profile) {
    if (profile == TransformProfile::Raw)
        return nullptr;
    for (const auto& spec : advcg::kSpecs) {
        if (spec.game != game || spec.id != (s.part << 16 | e.ordinal) ||
            Lower(s.path) != spec.archive)
            continue;
        if (profile == TransformProfile::Rb3Ma123Pilot &&
            !(static_cast<GameId>(game) == GameId::Rebirth3 && spec.id == (1u << 16 | 264u)))
            continue;
        if (profile == TransformProfile::Rb3LargeMaPilot &&
            !(static_cast<GameId>(game) == GameId::Rebirth3 &&
              (spec.id == (1u << 16 | 233u) || spec.id == (1u << 16 | 236u) ||
               spec.id == (1u << 16 | 264u))))
            continue;
        Need(std::strcmp(reinterpret_cast<const char*>(e.metadata.data() + 8), spec.name) == 0 &&
                 e.size == spec.sourceSize,
             "ADV CG source entry identity changed");
        Need(uint64_t(spec.width) * spec.height * 4 >= advcg::kMinPixelBytes,
             "ADV CG threshold mismatch");
        return &spec;
    }
    return nullptr;
}
std::vector<unsigned char> HalfMa(const std::vector<unsigned char>& source,
                                  const advcg::Spec& spec) {
    Need(source.size() == spec.sourceSize && !std::memcmp(source.data(), "TID\x90", 4),
         "ADV CG TID format changed");
    Need(Hex(Digest(source.data(), source.size())) == spec.sourceHash,
         "ADV CG decoded source hash changed");
    const uint32_t width = spec.width, height = spec.height, ow = (width + 1) / 2,
                   oh = (height + 1) / 2;
    Need(At<uint32_t>(source, 4) == source.size() && At<uint32_t>(source, 0x44) == width &&
             At<uint32_t>(source, 0x48) == height && At<uint32_t>(source, 0x4c) == 32 &&
             At<uint32_t>(source, 0x58) == width * height * 4 &&
             At<uint32_t>(source, 0x5c) == 128 && spec.outputSize == 128 + ow * oh * 4,
         "ADV CG TID dimensions changed");
    std::vector<unsigned char> out(spec.outputSize);
    std::memcpy(out.data(), source.data(), 128);
    Store32(out.data() + 4, spec.outputSize);
    Store32(out.data() + 0x44, ow);
    Store32(out.data() + 0x48, oh);
    Store32(out.data() + 0x58, ow * oh * 4);
    for (uint32_t y = 0; y < oh; y++)
        for (uint32_t x = 0; x < ow; x++) {
            const uint32_t sy = y * 2, sx = x * 2, rows = sy + 1 < height ? 2 : 1,
                           cols = sx + 1 < width ? 2 : 1;
            const uint32_t count = rows * cols;
            for (uint32_t c = 0; c < 4; c++) {
                uint32_t sum = 0;
                for (uint32_t dy = 0; dy < rows; dy++)
                    for (uint32_t dx = 0; dx < cols; dx++)
                        sum += source[128 + (uint64_t(sy + dy) * width + sx + dx) * 4 + c];
                out[128 + (uint64_t(y) * ow + x) * 4 + c] =
                    static_cast<unsigned char>((sum + count / 2) / count);
            }
        }
    Need(Hex(Digest(out.data(), out.size())) == spec.outputHash, "ADV CG transform output changed");
    return out;
}
} // namespace rebirths::prepare::assetprep
