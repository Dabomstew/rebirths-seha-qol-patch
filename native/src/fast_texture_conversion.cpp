#include "fast_texture_conversion.hpp"
#include "texture_conversion.hpp"
#include "texture_conversion_profiles.hpp"
#include <cstring>
#include <atomic>

namespace rebirths {
namespace {
using Decode = void(__cdecl*)(void*, int, int, const void*, uint32_t);
using Untwiddle = void(__cdecl*)(void*, const void*, int, int, uint32_t);
using Bits = uint32_t(__cdecl*)(uint32_t);
using Morton = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t);
Decode originalDecode = nullptr;
Untwiddle originalUntwiddle = nullptr;
Untwiddle originalPack = nullptr;
Bits bits = nullptr;
Morton morton = nullptr;
std::atomic<bool> enabled{false};
struct PendingPack {
    const void* source = nullptr;
    const void* tileBuffer = nullptr;
    int width = 0, height = 0;
    bool valid = false;
};
// Each validated game initializer calls pack and untwiddle consecutively on this thread.
// Keep no game resource after the next untwiddle call; standalone calls use tiles.
thread_local PendingPack pending;

bool TileDimensions(int width, int height) noexcept {
    return width >= 4 && height >= 4 && width <= 16384 && height <= 16384 && !(width & 3) &&
           !(height & 3);
}
void __cdecl FastDecode(void* output, int width, int height, const void* source,
                        uint32_t mode) noexcept {
    if (!enabled.load(std::memory_order_acquire) || (mode != 1 && mode != 2 && mode != 4) ||
        !TileDimensions(width, height)) {
        originalDecode(output, width, height, source, mode);
        return;
    }
    auto* pixels = static_cast<uint32_t*>(output);
    auto* blocks = static_cast<const unsigned char*>(source);
    if (mode == 1)
        textures::DecodeBc1(pixels, width, height, blocks);
    else if (mode == 2)
        textures::DecodeBcAlpha<2>(pixels, width, height, blocks);
    else
        textures::DecodeBcAlpha<4>(pixels, width, height, blocks);
}
void __cdecl FastPack(void* output, const void* source, int width, int height,
                      uint32_t bpp) noexcept {
    pending.valid = false;
    if (enabled.load(std::memory_order_acquire) && bpp == 32 && TileDimensions(width, height) &&
        !(width & (width - 1)) && !(height & (height - 1))) {
        pending = {source, output, width, height, true};
        return;
    }
    originalPack(output, source, width, height, bpp);
}
void __cdecl FastUntwiddle(void* output, const void* source, int width, int height,
                           uint32_t bpp) noexcept {
    const PendingPack pack = pending;
    pending.valid = false;
    if (!enabled.load(std::memory_order_acquire) || bpp != 32 || !TileDimensions(width, height)) {
        originalUntwiddle(output, source, width, height, bpp);
        return;
    }
    const uint32_t ex = 1u << (bits(uint32_t(width) * 2 - 1) & 31),
                   ey = 1u << (bits(uint32_t(height) * 2 - 1) & 31);
    if (pack.valid && source == pack.tileBuffer && width == pack.width && height == pack.height) {
        textures::Compose32(static_cast<uint32_t*>(output),
                            static_cast<const uint32_t*>(pack.source), width, height,
                            morton(ex - 1, 0, ex, ey), morton(0, ey - 1, ex, ey));
        return;
    }
    textures::Untwiddle32(static_cast<uint32_t*>(output), static_cast<const uint32_t*>(source),
                          width, height, morton(ex - 1, 0, ex, ey), morton(0, ey - 1, ex, ey));
}
} // namespace

bool InstallFastTextureConversion(const Context& context) noexcept {
    const auto* profile = textures::ProfileFor(context.spec.id);
    if (!profile)
        return false;
    const uintptr_t base = reinterpret_cast<uintptr_t>(context.game);
    for (size_t i = 0; i < profile->helpers.size(); ++i) {
        auto expected = profile->helpers[i].bytes;
        // Decoder's security cookie is an absolute address relocated by Windows.
        if (i == 0) {
            const uint32_t cookie = static_cast<uint32_t>(base + profile->cookieRva);
            std::memcpy(expected.data() + 7, &cookie, 4);
        }
        if (std::memcmp(reinterpret_cast<void*>(base + profile->helpers[i].rva), expected.data(),
                        expected.size())) {
            Log("FastTextureConversion unsupported helpers game=%s", GameIdName(context.spec.id));
            return false;
        }
    }
    originalDecode = reinterpret_cast<Decode>(base + profile->helpers[0].rva);
    enabled.store(false, std::memory_order_release);
    originalPack = reinterpret_cast<Untwiddle>(base + profile->helpers[1].rva);
    originalUntwiddle = reinterpret_cast<Untwiddle>(base + profile->helpers[2].rva);
    bits = reinterpret_cast<Bits>(base + profile->helpers[3].rva);
    morton = reinterpret_cast<Morton>(base + profile->helpers[4].rva);
    void* replacements[] = {reinterpret_cast<void*>(&FastDecode),
                            reinterpret_cast<void*>(&FastPack),
                            reinterpret_cast<void*>(&FastUntwiddle)};
    CallSite sites[3]{};
    for (size_t i = 0; i < 3; ++i)
        sites[i] = {profile->calls[i].rva, profile->calls[i].bytes, replacements[i]};
    if (!RetargetCalls(context, sites, 3)) {
        Log("FastTextureConversion transaction failed game=%s error=%#lx; forwarding",
            GameIdName(context.spec.id), GetLastError());
        return false;
    }
    enabled.store(true, std::memory_order_release);
    Log("FastTextureConversion active game=%s decoder=DXT1/3/5 transform=compose+tile; native format/dimension fallbacks retained",
        GameIdName(context.spec.id));
    return true;
}
} // namespace rebirths
