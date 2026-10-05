#pragma once
#ifndef REBIRTHS_TEST_CONTRACTS
#error "Transaction fixture contracts are available only in test builds"
#endif
#include "thread_freeze.hpp"

namespace rebirths::testing {
struct TransactionApi {
    ThreadApi threads{};
    decltype(&::VirtualProtect) protect = &::VirtualProtect;
    decltype(&::FlushInstructionCache) flush = &::FlushInstructionCache;
};
// Single-threaded fixture override. Nested scopes restore their predecessor;
// no override or injection code is compiled into shipping builds.
class TransactionScope {
  public:
    explicit TransactionScope(const TransactionApi& api) noexcept;
    ~TransactionScope() noexcept;
    TransactionScope(const TransactionScope&) = delete;
    TransactionScope& operator=(const TransactionScope&) = delete;

  private:
    const TransactionApi* previous_;
};
} // namespace rebirths::testing
