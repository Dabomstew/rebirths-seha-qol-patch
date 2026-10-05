#include "../src/fast_face_texture_creation.cpp"
#include <cassert>
#include <cstdio>
#include <thread>
#include <stdexcept>

namespace rebirths {
void Log(const char*, ...) noexcept {}
bool RetargetBytes(const Context&, uint32_t, const unsigned char*, const unsigned char*, size_t) noexcept { return false; }
bool RetargetCalls(const Context&, const CallSite*, size_t) noexcept { return false; }
}
namespace {
thread_local HGLRC fakeContext = reinterpret_cast<HGLRC>(1);
unsigned finishes = 0, surfaces = 0, mutations = 0;
bool changeContext = false, throwSurface = false, nested = false;
const rebirths::face_textures::Profile* checkedProfile = nullptr;
void WINAPI OriginalFinish() { ++finishes; }
HGLRC WINAPI Current() { return fakeContext; }
uintptr_t __cdecl OriginalSurface(const char* name, int f, int k, int filter, int depth, int w, int h, int count) {
    ++surfaces;
    assert(name && f == 1 && k == 1 && filter == 2 && depth == 0 && h == 1024 && count == 1);
    if (throwSurface) throw std::runtime_error("native failure");
    if (nested) {
        nested = false;
        rebirths::face_textures::Surface("unrelated", f,k,filter,depth,w,h,count);
        assert(rebirths::face_textures::faceContext == fakeContext);
    }
    if (changeContext) fakeContext = reinterpret_cast<HGLRC>(2);
    rebirths::face_textures::Finish(); rebirths::face_textures::Finish();
    return 0x12345678;
}
bool Bytes(const rebirths::Context&, uint32_t rva, const unsigned char* before, const unsigned char* after, size_t size) noexcept {
    assert(checkedProfile && rva == checkedProfile->finishRva && size == 6 && before[0] == 0xff && after[0] == 0xe8 && after[5] == 0x90);
    ++mutations; return true;
}
bool Calls(const rebirths::Context&, const rebirths::CallSite* sites, size_t count) noexcept {
    assert(checkedProfile && count == 2);
    for (size_t i = 0; i < count; ++i) {
        assert(sites[i].rva == checkedProfile->calls[i].rva && sites[i].expected == checkedProfile->calls[i].bytes);
        assert(sites[i].replacement == reinterpret_cast<void*>(rebirths::face_textures::Surface));
    }
    ++mutations; return false; // failed transaction leaves the scope unarmed
}
}
int main() {
    using namespace rebirths;
    using namespace rebirths::face_textures;
    originalSurface = OriginalSurface; originalFinish = OriginalFinish; currentContext = Current;
    enabled.store(true);
    Finish(); assert(finishes == 1);
    assert(Surface("FaceAnimeSurface",1,1,2,0,1024,1024,1) == 0x12345678);
    assert(finishes == 1 && surfaces == 1 && faceContext == nullptr);
    Surface("FaceAnimeSurfaceBlow",1,1,2,0,1024,1024,1); assert(finishes == 1);
    Surface("unrelated",1,1,2,0,1024,1024,1); assert(finishes == 3);
    Surface("FaceAnimeSurface",1,1,2,0,512,1024,1); assert(finishes == 5);
    fakeContext = nullptr; Surface("FaceAnimeSurface",1,1,2,0,1024,1024,1); assert(finishes == 7);
    fakeContext = reinterpret_cast<HGLRC>(1); changeContext = true;
    Surface("FaceAnimeSurface",1,1,2,0,1024,1024,1); assert(finishes == 9 && faceContext == nullptr);
    changeContext = false; fakeContext = reinterpret_cast<HGLRC>(1); nested = true;
    Surface("FaceAnimeSurface",1,1,2,0,1024,1024,1); assert(finishes == 11 && faceContext == nullptr);
    throwSurface = true;
    try { Surface("FaceAnimeSurface",1,1,2,0,1024,1024,1); assert(false); } catch (const std::runtime_error&) {}
    assert(faceContext == nullptr); throwSurface = false;
    faceContext = fakeContext;
    std::thread other([] { assert(faceContext == nullptr); Finish(); }); other.join(); assert(finishes == 12);
    faceContext = nullptr;

    // Relocated operands, exact call preflight and failure forwarding. The real
    // transaction's image ownership/peer freezing/rollback are tested separately.
    constexpr size_t imageSize = 0x390000;
    for (const auto* profile : {&SegaProfile, &Rebirth3Profile}) {
        checkedProfile = profile;
        const auto FinishRva = profile->finishRva, FinishIatRva = profile->finishIatRva;
        auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr,imageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)); assert(image);
        HMODULE gl = LoadLibraryW(L"opengl32.dll"); assert(gl);
        const auto realFinish = reinterpret_cast<FinishFn>(GetProcAddress(gl,"glFinish"));
        std::array<unsigned char,6> original{0xff,0x15};
        const uint32_t iat = reinterpret_cast<uint32_t>(image + FinishIatRva); std::memcpy(original.data()+2,&iat,4);
        std::memcpy(image+FinishRva,original.data(),6);
        for (const auto& site : profile->calls) std::memcpy(image+site.rva,site.bytes.data(),5);
        *reinterpret_cast<FinishFn*>(image+FinishIatRva) = realFinish;
        DWORD old; assert(VirtualProtect(image,imageSize,PAGE_EXECUTE_READ,&old));
        assert(VirtualProtect(image+FinishIatRva,4,PAGE_READONLY,&old));
        Context context{reinterpret_cast<HMODULE>(image),GameSpecFor(profile->game),L"",L""};
        FaceTexturePatchOps ops{Bytes,Calls};
        mutations = 0;
        assert(!InstallFastFaceTextureCreation(context,&ops) && mutations == 2 && faceContext == nullptr);
        assert(originalSurface == reinterpret_cast<SurfaceFn>(image + profile->surfaceRva));
        originalFinish = OriginalFinish; currentContext = Current;
        const auto previousFinishes = finishes; Finish(); assert(finishes == previousFinishes + 1);
        originalSurface = OriginalSurface;
        // A surviving redirected Surface must also forward after a failed
        // two-CALL activation, even for the exact eligible face parameters.
        Surface("FaceAnimeSurface",1,1,2,0,1024,1024,1);
        assert(!enabled.load() && finishes == previousFinishes + 3);
        mutations = 0; context.spec = GameSpecFor(GameId::Rebirth2);
        assert(!InstallFastFaceTextureCreation(context,&ops) && mutations == 0);
        context.spec = GameSpecFor(GameId::Rebirth1);
        assert(!InstallFastFaceTextureCreation(context,&ops) && mutations == 0);
        context.spec = GameSpecFor(profile->game);
        assert(VirtualProtect(image+profile->calls[1].rva,5,PAGE_EXECUTE_READWRITE,&old));
        image[profile->calls[1].rva] ^= 1;
        assert(VirtualProtect(image+profile->calls[1].rva,5,PAGE_EXECUTE_READ,&old));
        assert(!InstallFastFaceTextureCreation(context,&ops) && mutations == 0);
        assert(VirtualFree(image,0,MEM_RELEASE)); FreeLibrary(gl);
    }
    std::puts("RB3/Sega shared face texture guards OK: forwarding, arguments, context changes, nesting, TLS, unwind, both profiles, preflight and inactive fallback");
}
