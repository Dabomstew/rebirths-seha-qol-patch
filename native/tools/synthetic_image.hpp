#pragma once
#include "rebirths_patch.hpp"
#include <cstring>
#include <stdexcept>

namespace fixture {
class Image {
  public:
    explicit Image(size_t size)
        : data_(static_cast<unsigned char*>(
              VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))) {
        if (!data_)
            throw std::runtime_error("synthetic image allocation failed");
    }
    ~Image() {
        VirtualFree(data_, 0, MEM_RELEASE);
    }
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    unsigned char* data() const noexcept {
        return data_;
    }

  private:
    unsigned char* data_;
};
// Independent x86 fixture encoder. Does not consume production CALL helpers.
inline std::array<unsigned char, 5> CallBytes(uintptr_t source, uintptr_t target) {
    const uint32_t displacement = static_cast<uint32_t>(target - source - 5);
    std::array<unsigned char, 5> instruction{0xe8};
    std::memcpy(instruction.data() + 1, &displacement, sizeof(displacement));
    return instruction;
}
inline bool Protect(void* address, size_t size, DWORD protection) {
    DWORD previous = 0;
    return VirtualProtect(address, size, protection, &previous) != FALSE;
}
} // namespace fixture
