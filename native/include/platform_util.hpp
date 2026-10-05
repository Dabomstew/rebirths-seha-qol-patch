#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <climits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#pragma comment(lib, "bcrypt.lib")

namespace rebirths::platform {

using Digest = std::array<unsigned char, 32>;

// Owns CloseHandle resources only; BCrypt handles have different release APIs.
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE handle) noexcept : value(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(other.Release()) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { Reset(); value = other.Release(); }
        return *this;
    }
    ~Handle() { Reset(); }
    explicit operator bool() const noexcept { return value && value != INVALID_HANDLE_VALUE; }
    void Reset() noexcept {
        if (*this) CloseHandle(value);
        value = INVALID_HANDLE_VALUE;
    }
    HANDLE Release() noexcept { return std::exchange(value, INVALID_HANDLE_VALUE); }
};

// A narrow platform seam for failure fixtures; production uses the Win32 API.
struct ShaApi {
    decltype(&BCryptOpenAlgorithmProvider) open = BCryptOpenAlgorithmProvider;
    decltype(&BCryptCreateHash) create = BCryptCreateHash;
    decltype(&BCryptHashData) update = BCryptHashData;
    decltype(&BCryptFinishHash) finish = BCryptFinishHash;
    decltype(&BCryptDestroyHash) destroy = BCryptDestroyHash;
    decltype(&BCryptCloseAlgorithmProvider) close = BCryptCloseAlgorithmProvider;
};

class Sha {
    ShaApi api_;
    struct Algorithm {
        const ShaApi& api;
        BCRYPT_ALG_HANDLE value = nullptr;
        ~Algorithm() { if (value) api.close(value, 0); }
    } algorithm_;
    struct State {
        const ShaApi& api;
        BCRYPT_HASH_HANDLE value = nullptr;
        ~State() { if (value) api.destroy(value); }
    } state_;
    static void Check(bool ok, const char* message) {
        if (!ok) throw std::runtime_error(message);
    }
public:
    // Fully constructed member owners unwind even if this constructor throws.
    explicit Sha(ShaApi api = {}) : api_(api), algorithm_{api_}, state_{api_} {
        Check(api_.open(&algorithm_.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
              "SHA provider failed");
        Check(api_.create(algorithm_.value, &state_.value, nullptr, 0, nullptr, 0, 0) >= 0,
              "SHA state failed");
    }
    Sha(const Sha&) = delete;
    Sha& operator=(const Sha&) = delete;
    void Add(const void* data, size_t size) {
        Check(size <= ULONG_MAX, "SHA update exceeds bound");
        Check(api_.update(state_.value, reinterpret_cast<PUCHAR>(const_cast<void*>(data)),
                          static_cast<ULONG>(size), 0) >= 0, "SHA update failed");
    }
    Digest Finish() {
        Digest digest{};
        Check(api_.finish(state_.value, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0,
              "SHA finish failed");
        return digest;
    }
};

// Hex output is always lowercase. Ownership digest text must be canonical.
template<size_t N>
std::string Hex(const std::array<unsigned char, N>& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(N * 2);
    for (auto byte : bytes) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}

inline Digest Unhex(std::string_view text) {
    if (text.size() != 64) throw std::runtime_error("Invalid SHA-256 text");
    auto digit = [](char c) -> unsigned char {
        if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
        throw std::runtime_error("Invalid SHA-256 digit");
    };
    Digest digest{};
    for (size_t i = 0; i < digest.size(); ++i)
        digest[i] = static_cast<unsigned char>((digit(text[2 * i]) << 4) | digit(text[2 * i + 1]));
    return digest;
}

inline std::wstring Wide(std::string_view text) {
    if (text.empty()) return {};
    if (text.size() > INT_MAX) throw std::runtime_error("UTF-8 input exceeds bound");
    const auto size = static_cast<int>(text.size());
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, nullptr, 0);
    if (!count) throw std::runtime_error("Invalid UTF-8");
    std::wstring result(count, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, result.data(), count) != count)
        throw std::runtime_error("UTF-8 conversion failed");
    return result;
}

inline std::string Utf8(std::wstring_view text) {
    if (text.empty()) return {};
    if (text.size() > INT_MAX) throw std::runtime_error("Unicode input exceeds bound");
    const auto size = static_cast<int>(text.size());
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("Invalid Unicode");
    std::string result(count, '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, result.data(), count, nullptr, nullptr) != count)
        throw std::runtime_error("Unicode conversion failed");
    return result;
}

} // namespace rebirths::platform
