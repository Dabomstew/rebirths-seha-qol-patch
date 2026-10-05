#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

// Bounded JSON value/parser for owner and journal schemas. Schema callers
// validate exact allowed fields after parsing; serialization stays canonical.
namespace rebirths::prepare::assetprep {
void Need(bool good, const char* why);
struct J {
    using Array = std::vector<J>;
    using Object = std::map<std::string, J>;
    std::variant<std::nullptr_t, bool, uint64_t, std::string, Array, Object> value;
    J() : value(nullptr) {}
    J(bool x) : value(x) {}
    template <class T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    J(T x) : value(uint64_t(x)) {}
    J(std::string x) : value(std::move(x)) {}
    J(const char* x) : value(std::string(x)) {}
    J(Array x) : value(std::move(x)) {}
    J(Object x) : value(std::move(x)) {}
    bool operator==(const J& b) const {
        return value == b.value;
    }
    const Object& O() const {
        Need(std::holds_alternative<Object>(value), "JSON object expected");
        return std::get<Object>(value);
    }
    const Array& A() const {
        Need(std::holds_alternative<Array>(value), "JSON array expected");
        return std::get<Array>(value);
    }
    const std::string& S() const {
        Need(std::holds_alternative<std::string>(value), "JSON string expected");
        return std::get<std::string>(value);
    }
    uint64_t N() const {
        Need(std::holds_alternative<uint64_t>(value), "JSON integer expected");
        return std::get<uint64_t>(value);
    }
    const J& Get(const char* key) const {
        auto& o = O();
        auto at = o.find(key);
        Need(at != o.end(), "JSON key missing");
        return at->second;
    }
};

J ParseJson(const std::string& text);
std::string JsonText(const J& value);
} // namespace rebirths::prepare::assetprep
