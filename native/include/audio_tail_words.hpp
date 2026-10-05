#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace rebirths::audio_tail {
struct Word { uint32_t offset, expected, replacement; };
inline bool Validate(const void* data, size_t size, const Word* words, size_t count) noexcept {
    if (!data || !words || !count) return false;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < count; ++i) {
        if (size < 4 || words[i].offset > size - 4 || words[i].offset % 4) return false;
        uint32_t actual;
        std::memcpy(&actual, bytes + words[i].offset, 4);
        if (actual != words[i].expected) return false;
    }
    return true;
}
inline void Write(void* data, const Word* words, size_t count, bool restore = false) noexcept {
    auto* bytes = static_cast<unsigned char*>(data);
    for (size_t i = 0; i < count; ++i) {
        const auto value = restore ? words[i].expected : words[i].replacement;
        std::memcpy(bytes + words[i].offset, &value, 4);
    }
}
}
