#include "../src/fast_texture_conversion.cpp"
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace rebirths {
void Log(const char*, ...) noexcept {}
static bool installSucceeds = false;
bool RetargetCalls(const Context& context, const CallSite* sites, size_t count) noexcept {
    const auto base = reinterpret_cast<uintptr_t>(context.game);
    // Leave the pack CALL redirected in an incomplete multi-page rollback.
    for (size_t i = 0; i < count; ++i) if (installSucceeds || i == 1) {
        const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sites[i].replacement) - base - sites[i].rva - 5);
        std::memcpy(reinterpret_cast<void*>(base + sites[i].rva + 1), &relative, 4);
    }
    SetLastError(installSucceeds ? 0 : 0x2100000b); return installSucceeds;
}
}
static unsigned decodes = 0, packs = 0, untwiddles = 0;
static void __cdecl NativeDecode(void*, int, int, const void*, uint32_t) { ++decodes; }
static void __cdecl Pack(void*, const void*, int, int, uint32_t) { ++packs; }
static void __cdecl NativeUntwiddle(void*, const void*, int, int, uint32_t) { ++untwiddles; }
static void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        using namespace rebirths;
        for (const auto game : {GameId::Rebirth1, GameId::Rebirth2, GameId::Rebirth3, GameId::SegaHardGirls}) {
            const auto* profile = textures::ProfileFor(game);
            std::vector<unsigned char> image(0x400000);
            for (size_t i = 0; i < profile->helpers.size(); ++i) {
                auto bytes = profile->helpers[i].bytes;
                if (!i) { const uint32_t cookie = reinterpret_cast<uint32_t>(image.data()) + profile->cookieRva; std::memcpy(bytes.data()+7,&cookie,4); }
                std::memcpy(image.data()+profile->helpers[i].rva,bytes.data(),bytes.size());
            }
            for (const auto& call : profile->calls) std::memcpy(image.data()+call.rva,call.bytes.data(),5);
            GameSpec spec{}; spec.id = game;
            Context context(reinterpret_cast<HMODULE>(image.data()),spec,L"",L"");
            Require(!InstallFastTextureConversion(context), "partial install must fail");
            originalDecode=NativeDecode;originalPack=Pack;originalUntwiddle=NativeUntwiddle;
            uint32_t pixels[16]{}, tiles[16]{};
            FastDecode(pixels,4,4,tiles,1);FastPack(tiles,pixels,4,4,32);FastUntwiddle(pixels,tiles,4,4,32);
            Require(!enabled.load() && !pending.valid, "partial pack cannot leave deferred output");
            for (const auto& call : profile->calls) std::memcpy(image.data()+call.rva,call.bytes.data(),5);
            installSucceeds=true;
            Require(InstallFastTextureConversion(context) && enabled.load(), "successful installation activates");
            installSucceeds=false;enabled.store(false);
        }
        Require(decodes==4 && packs==4 && untwiddles==4, "every partial wrapper forwards the native operation");
        std::puts("Texture hooks: all four profiles retain native decode/pack/untwiddle after incomplete installation");return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
}
