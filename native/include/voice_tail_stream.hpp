#pragma once
#include "audio_tail_words.hpp"
#include <algorithm>

namespace rebirths::audio_tail {
// Overlays arbitrary (including split-word) metadata reads. Check the entire
// overlapping pristine region before writing; payload bytes remain untouched.
inline bool OverlayVoiceMetadata(void* buffer, size_t bytes, uint64_t offset,
        const unsigned char* original, const unsigned char* corrected, size_t prefix) noexcept {
    if (offset >= prefix || !bytes) return true;
    if (!buffer || !original || !corrected) return false;
    const size_t start = static_cast<size_t>(offset);
    const size_t count = (std::min)(bytes, prefix - start);
    if (std::memcmp(buffer, original + start, count)) return false;
    std::memcpy(buffer, corrected + start, count);
    return true;
}
}
