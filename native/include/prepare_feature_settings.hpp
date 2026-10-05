#pragma once
#include "prepare_game.hpp"
#include <array>
#include <stdexcept>

namespace rebirths::prepare {
struct FeatureBinding { FeatureId id; bool Settings::*member; };
inline constexpr FeatureBinding FeatureBindings[] = {
#include "prepare_feature_bindings.inc"
};
inline bool& SettingValue(Settings& settings, FeatureId id) {
    for (const auto& binding : FeatureBindings)
        if (binding.id == id) return settings.*binding.member;
    throw std::invalid_argument("Feature has no preparer control");
}
} // namespace rebirths::prepare
