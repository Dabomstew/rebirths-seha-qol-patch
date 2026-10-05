#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace rebirths::prepare {
namespace fs = std::filesystem;
struct Progress {
    std::wstring stage;
    std::wstring current;
    uint64_t completed = 0;
    uint64_t total = 0;
};
struct Result {
    uint64_t files = 0;
    uint64_t bytes = 0;
    uint64_t reused = 0;
    bool fullCoverage = true;
};
using Report = std::function<void(const Progress&)>;
using Cancel = std::function<bool()>;
enum class TransformProfile : uint32_t {
    Raw = 0,
    Rb3Ma123Pilot = 1,
    Rb3LargeMaPilot = 2,
    AdvCgHalf24V1 = 3,
};
// Missing owner.json returns nullopt. Existing metadata must match the selected
// game, directory and supported owner schema; invalid data throws with a diagnostic.
// Source archives are checked separately by RunAssets/VerifyAssets.
std::optional<TransformProfile> ReadAssetProfile(const fs::path& game, const fs::path& output,
                                                 uint32_t gameId);
Result RunAssets(const fs::path& game, const fs::path& output, uint32_t gameId,
                 const Report& report = {}, const Cancel& cancel = {},
                 TransformProfile profile = TransformProfile::Raw);
Result VerifyAssets(const fs::path& game, const fs::path& output, uint32_t gameId,
                    const Report& report = {}, const Cancel& cancel = {},
                    TransformProfile profile = TransformProfile::Raw);
} // namespace rebirths::prepare
