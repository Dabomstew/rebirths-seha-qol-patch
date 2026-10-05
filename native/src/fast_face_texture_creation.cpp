#include "fast_face_texture_creation.hpp"
#include "face_texture_profiles.hpp"
#include <cstring>
#include <atomic>

namespace rebirths {
namespace face_textures {
using SurfaceFn = uintptr_t (__cdecl*)(const char*, int, int, int, int, int, int, int);
using FinishFn = void (WINAPI*)();
using CurrentContextFn = HGLRC (WINAPI*)();
SurfaceFn originalSurface = nullptr;
FinishFn originalFinish = nullptr;
CurrentContextFn currentContext = nullptr;
std::atomic<bool> enabled{false};
thread_local HGLRC faceContext = nullptr;
struct Scope {
    HGLRC saved = faceContext;
    ~Scope() { faceContext = saved; }
};

// This adapter replaces only the blank-RGBA initialization completion call.
// TexImage2D still copies the native zero buffer. Subsequent commands in the
// same context observe that upload in order; no cross-context wait is elided.
void WINAPI Finish() {
    if (!faceContext || currentContext() != faceContext) originalFinish();
}

uintptr_t __cdecl Surface(const char* name, int format, int kind, int filter,
                         int depth, int width, int height, int count) {
    const Scope scope;
    const bool eligible = enabled.load(std::memory_order_acquire) && name && (!std::strcmp(name, "FaceAnimeSurface") ||
        !std::strcmp(name, "FaceAnimeSurfaceBlow")) && format == 1 && kind == 1 &&
        filter == 2 && depth == 0 && width == 1024 && height == 1024 && count == 1;
    faceContext = eligible ? currentContext() : nullptr;
    // Native ownership, texture allocation, zero initialization and framebuffer
    // setup all remain in the original cdecl function with its eight arguments.
    return originalSurface(name, format, kind, filter, depth, width, height, count);
}

bool Readable(const Context& context, uint32_t rva, size_t count, DWORD protection) {
    const uintptr_t at = reinterpret_cast<uintptr_t>(context.game) + rva;
    MEMORY_BASIC_INFORMATION memory{};
    return VirtualQuery(reinterpret_cast<void*>(at), &memory, sizeof(memory)) == sizeof(memory) &&
        memory.State == MEM_COMMIT && memory.AllocationBase == context.game &&
        memory.Protect == protection && memory.RegionSize >= count &&
        at - reinterpret_cast<uintptr_t>(memory.BaseAddress) <= memory.RegionSize - count;
}
}

bool InstallFastFaceTextureCreation(const Context& context, const FaceTexturePatchOps* injected) noexcept {
    using namespace face_textures;
    const auto* profile = ProfileFor(context.spec.id);
    if (!profile) { Log("FastFaceTextureCreation unsupported target"); return false; }
    const uint32_t FinishRva = profile->finishRva, FinishIatRva = profile->finishIatRva;
    CallSite SurfaceSites[2]{};
    for (size_t i = 0; i < std::size(SurfaceSites); ++i)
        SurfaceSites[i] = {profile->calls[i].rva, profile->calls[i].bytes, reinterpret_cast<void*>(Surface)};
    const FaceTexturePatchOps production{RetargetBytes, RetargetCalls};
    const auto& ops = injected ? *injected : production;
    const auto base = reinterpret_cast<uintptr_t>(context.game);
    const HMODULE gl = GetModuleHandleW(L"opengl32.dll");
    const auto finish = reinterpret_cast<FinishFn>(gl ? GetProcAddress(gl, "glFinish") : nullptr);
    const auto current = reinterpret_cast<CurrentContextFn>(gl ? GetProcAddress(gl, "wglGetCurrentContext") : nullptr);
    std::array<unsigned char, 6> before{0xff, 0x15};
    const uint32_t iat = static_cast<uint32_t>(base + FinishIatRva);
    std::memcpy(before.data() + 2, &iat, 4); // relocated IAT operand, not a code target
    if (!finish || !current || !ops.retargetBytes || !ops.retargetCalls ||
        !Readable(context, FinishIatRva, sizeof(void*), PAGE_READONLY) ||
        *reinterpret_cast<FinishFn*>(base + FinishIatRva) != finish ||
        !Readable(context, FinishRva, before.size(), PAGE_EXECUTE_READ) ||
        std::memcmp(reinterpret_cast<void*>(base + FinishRva), before.data(), before.size())) {
        Log("FastFaceTextureCreation preflight failed; no bytes changed"); return false;
    }
    for (const auto& site : SurfaceSites) {
        if (!Readable(context, site.rva, site.expected.size(), PAGE_EXECUTE_READ) ||
            std::memcmp(reinterpret_cast<void*>(base + site.rva), site.expected.data(), site.expected.size())) {
            Log("FastFaceTextureCreation surface preflight failed; no bytes changed"); return false;
        }
    }
    originalSurface = reinterpret_cast<SurfaceFn>(base + profile->surfaceRva);
    enabled.store(false, std::memory_order_release);
    originalFinish = finish;
    currentContext = current;
    std::array<unsigned char, 6> after{0xe8, 0, 0, 0, 0, 0x90};
    const uint32_t displacement = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Finish) - base - FinishRva - 5);
    std::memcpy(after.data() + 1, &displacement, 4);
    // First install a behavior-preserving adapter (scope is empty). Only the
    // atomic two-CALL transaction can activate elision. If it fails, the pinned
    // adapter safely keeps forwarding every completion call to the original GL.
    // Both production transactions enforce MEM_IMAGE/RX ownership and freeze peers.
    if (!ops.retargetBytes(context, FinishRva, before.data(), after.data(), after.size())) {
        Log("FastFaceTextureCreation completion transaction failed"); return false;
    }
    if (!ops.retargetCalls(context, SurfaceSites, std::size(SurfaceSites))) {
        Log("FastFaceTextureCreation surface transaction failed; completion adapter forwards original"); return false;
    }
    enabled.store(true, std::memory_order_release);
    const char* tag = context.spec.id == GameId::SegaHardGirls ? "sega" : GameIdName(context.spec.id);
    Log("FastFaceTextureCreation %s installed; same-context face waits omitted", tag);
    return true;
}
}
