#include "prepare_asset_internal.hpp"
#include <set>
#include <stdexcept>

namespace rebirths::prepare {
using namespace assetprep;
namespace {
Result Process(const fs::path& selected, const fs::path& selectedOutput, uint32_t id, bool write,
               const Report& report, const Cancel& cancel, TransformProfile profile) {
    Need(ValidGameId(static_cast<GameId>(id)), "Unknown game identity");
    const auto game = Safe(selected), root = fs::absolute(selectedOutput).lexically_normal();
    CheckOutput(game, root);
    CheckCancel(cancel);
    Recipe(profile);
    Need((profile != TransformProfile::Rb3Ma123Pilot &&
          profile != TransformProfile::Rb3LargeMaPilot) ||
             static_cast<GameId>(id) == GameId::Rebirth3,
         "Pilot MA transform requires Re;Birth3");
    auto exceptions = Exclusions(id, game), expected = Owner(id, game, exceptions, profile);
    auto owner = Safe(root, "owner.json");
    if (fs::exists(root)) {
        Need(fs::is_directory(root), "Output is not a directory");
        if (!fs::exists(owner))
            Need(fs::directory_iterator(root) == fs::directory_iterator{},
                 "Unowned nonempty output refused");
    } else {
        Need(write, "Prepared output missing");
        fs::create_directories(root);
    }
    if (fs::exists(owner))
        Need(ReadOwner(owner, id, game, &exceptions) == profile, "Output ownership mismatch");
    else {
        Need(write, "Output owner missing");
        Atomic(owner, JsonText(expected) + "\n");
    }
    Handle owned(CreateFileW(owner.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr));
    Need(owned.value != INVALID_HANDLE_VALUE, "Output is already being prepared");
    Result result;
    result.fullCoverage = exceptions.A().empty();
    for (const auto& scope : {"base", "dlc"}) {
        CheckCancel(cancel);
        auto sources = Collect(game, id, std::string(scope) == "dlc", report, cancel);
        size_t archives = 0;
        for (const auto& s : sources)
            if (s.kind)
                archives++;
        if (!archives) {
            Need(std::string(scope) == "dlc", "No base PAC archives found");
            continue;
        }
        if (profile == TransformProfile::AdvCgHalf24V1)
            for (const auto& s : sources)
                for (const auto& e : s.entries) {
                    const std::string name =
                        Lower(reinterpret_cast<const char*>(e.metadata.data() + 8));
                    if (e.size >= 128u + advcg::kMinPixelBytes &&
                        name.rfind("event\\ma\\", 0) == 0 &&
                        name.find("\\tex_") != std::string::npos && name.size() >= 4 &&
                        name.substr(name.size() - 4) == ".tid" && !SelectedHalf(s, e, id, profile))
                        ReportAt(report, L"Uncataloged large ADV CG (left original)",
                                 fs::path(s.path), e.ordinal, s.entries.size());
                }
        if ((profile == TransformProfile::Rb3Ma123Pilot ||
             profile == TransformProfile::Rb3LargeMaPilot) &&
            std::string(scope) == "base") {
            std::set<uint32_t> found;
            for (const auto& s : sources)
                for (const auto& e : s.entries) {
                    auto spec = SelectedHalf(s, e, id, profile);
                    if (spec)
                        found.insert(e.ordinal);
                }
            Need(found.count(264) == 1 && (profile != TransformProfile::Rb3LargeMaPilot ||
                                           (found.count(233) == 1 && found.count(236) == 1)),
                 "Large MA pilot source not found");
        }
        std::vector<Row> rows;
        for (uint32_t i = 0; i < sources.size(); i++)
            if (sources[i].kind) {
                const auto& s = sources[i];
                bool exclude = static_cast<GameId>(id) == GameId::Rebirth2 &&
                               s.path == "data/GAME00001.pac" && !exceptions.A().empty();
                if (exclude)
                    Need(s.entries.size() > 553, "RB2 exception entry absent");
                AppendRows(root, scope, s, i, id, profile, exclude, write, rows, result, report,
                           cancel);
            }
        auto bytes = Manifest(id, sources, rows);
        auto manifest = Safe(root, std::string(scope) + ".manifest");
        if (write) {
            CheckCancel(cancel);
            Atomic(manifest, bytes);
        } else {
            auto saved = ReadText(manifest);
            Need(saved.size() == bytes.size() &&
                     !std::memcmp(saved.data(), bytes.data(), bytes.size()),
                 "Manifest differs from prepared data");
        }
        result.files += rows.size();
        for (const auto& r : rows)
            result.bytes += r.size;
    }
    return result;
}
} // namespace
Result RunAssets(const fs::path& game, const fs::path& output, uint32_t id, const Report& report,
                 const Cancel& cancel, TransformProfile profile) {
    return Process(game, output, id, true, report, cancel, profile);
}
Result VerifyAssets(const fs::path& game, const fs::path& output, uint32_t id, const Report& report,
                    const Cancel& cancel, TransformProfile profile) {
    return Process(game, output, id, false, report, cancel, profile);
}
} // namespace rebirths::prepare
