#pragma once
#include "../preparer-common/preparer.hpp"
namespace rebirths::prepare {
class UiAdapter final : public preparer::Adapter {
public:
    preparer::Product Describe() const override;
    std::vector<preparer::fs::path> Detect() override;
    preparer::View Open(const preparer::fs::path&) override;
    std::vector<preparer::Capability> Actions(const preparer::View&) const override;
    void Changed(preparer::View&, const std::string&) override;
    preparer::Outcome Execute(preparer::Action, const preparer::View&, const preparer::Report&, const preparer::Cancel&) override;
};
}
