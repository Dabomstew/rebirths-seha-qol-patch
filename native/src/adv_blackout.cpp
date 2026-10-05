#include "adv_blackout.hpp"

#include <cstring>

namespace rebirths {
namespace {
AdvBlackoutGuard guard = nullptr;
uintptr_t configuredBase = 0;
AdvWindowSkinFn originalWindowSkin = nullptr;
AdvBlackoutInfoHandle configuredInfoHandle = nullptr;

using GetCurrentContextFn = HGLRC (WINAPI*)();
using GetIntegerFn = void (APIENTRY*)(unsigned, int*);
using GetStringFn = const unsigned char* (APIENTRY*)(unsigned);
using PushAttribFn = void (APIENTRY*)(unsigned long);
using PopAttribFn = void (APIENTRY*)();
using EnableFn = void (APIENTRY*)(unsigned);
using ColorMaskFn = void (APIENTRY*)(unsigned char, unsigned char, unsigned char, unsigned char);
using ClearColorFn = void (APIENTRY*)(float, float, float, float);
using DrawBufferFn = void (APIENTRY*)(unsigned);
using ClearFn = void (APIENTRY*)(unsigned long);
using BindFramebufferFn = void (APIENTRY*)(unsigned, unsigned);
BOOL (WINAPI* originalSwapBuffers)(HDC) = nullptr;

constexpr unsigned GL_COLOR_BUFFER_BIT = 0x00004000;
constexpr unsigned GL_SCISSOR_BIT = 0x00080000;
constexpr unsigned GL_SCISSOR_TEST = 0x00000c11;
constexpr unsigned GL_DRAW_BUFFER = 0x00000c01;
constexpr unsigned GL_BACK = 0x00000405;
constexpr unsigned GL_FRAMEBUFFER = 0x00008d40;
constexpr unsigned GL_FRAMEBUFFER_BINDING = 0x00008ca6;
constexpr unsigned GL_DRAW_FRAMEBUFFER = 0x00008ca9;
constexpr unsigned GL_DRAW_FRAMEBUFFER_BINDING = 0x00008ca6;
constexpr unsigned GL_READ_FRAMEBUFFER_BINDING = 0x00008caa;
constexpr unsigned GL_VERSION = 0x00001f02;
constexpr unsigned GL_EXTENSIONS = 0x00001f03;
constexpr uintptr_t AdvWndManagerPointerRva = 0x459208;

uintptr_t AdvWndInfoHandle() noexcept {
    if (configuredInfoHandle) return configuredInfoHandle();
    if (!configuredBase) return false;
    const uintptr_t manager = *reinterpret_cast<const uintptr_t*>(configuredBase + AdvWndManagerPointerRva);
    if (!manager || *reinterpret_cast<const uint32_t*>(manager) == 0 ||
        (*reinterpret_cast<const uint32_t*>(manager + 0x27c) & 1) == 0)
        return 0;
    return *reinterpret_cast<const uintptr_t*>(manager + 0x70);
}
bool AdvWndInfoShowing() noexcept {
    return AdvWndInfoHandle() != 0;
}
bool WindowSkinClearActive(uintptr_t widget) noexcept {
    return guard && guard() && widget && widget == AdvWndInfoHandle();
}
bool HasExtension(const char* extensions, const char* name) noexcept;
void ClearCurrentDrawTarget() noexcept {
    HMODULE gl = GetModuleHandleW(L"opengl32.dll");
    if (!gl) return;
    const auto context = reinterpret_cast<GetCurrentContextFn>(GetProcAddress(gl, "wglGetCurrentContext"));
    const auto getInteger = reinterpret_cast<GetIntegerFn>(GetProcAddress(gl, "glGetIntegerv"));
    const auto getString = reinterpret_cast<GetStringFn>(GetProcAddress(gl, "glGetString"));
    const auto pushAttrib = reinterpret_cast<PushAttribFn>(GetProcAddress(gl, "glPushAttrib"));
    const auto popAttrib = reinterpret_cast<PopAttribFn>(GetProcAddress(gl, "glPopAttrib"));
    const auto disable = reinterpret_cast<EnableFn>(GetProcAddress(gl, "glDisable"));
    const auto colorMask = reinterpret_cast<ColorMaskFn>(GetProcAddress(gl, "glColorMask"));
    const auto clearColor = reinterpret_cast<ClearColorFn>(GetProcAddress(gl, "glClearColor"));
    const auto drawBuffer = reinterpret_cast<DrawBufferFn>(GetProcAddress(gl, "glDrawBuffer"));
    const auto clear = reinterpret_cast<ClearFn>(GetProcAddress(gl, "glClear"));
    if (!context || !context() || !getInteger || !getString || !pushAttrib || !popAttrib || !disable || !colorMask || !clearColor || !drawBuffer || !clear)
        return;
    const char* version = reinterpret_cast<const char*>(getString(GL_VERSION));
    const char* extensions = reinterpret_cast<const char*>(getString(GL_EXTENSIONS));
    const bool coreFbo = (version && version[0] >= '3') || HasExtension(extensions, "GL_ARB_framebuffer_object");
    const bool extFbo = !coreFbo && HasExtension(extensions, "GL_EXT_framebuffer_object");
    int drawFramebuffer = 0;
    if (coreFbo) getInteger(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    else if (extFbo) getInteger(GL_FRAMEBUFFER_BINDING, &drawFramebuffer);
    int drawAttachment = GL_BACK;
    if (drawFramebuffer) getInteger(GL_DRAW_BUFFER, &drawAttachment);
    pushAttrib(GL_COLOR_BUFFER_BIT | GL_SCISSOR_BIT);
    // Do not bind another framebuffer: the window skin has already selected
    // the live target. Default framebuffer clears use GL_BACK; FBO clears
    // retain that target's active draw attachment.
    drawBuffer(static_cast<unsigned>(drawAttachment));
    disable(GL_SCISSOR_TEST);
    colorMask(1, 1, 1, 1);
    clearColor(0, 0, 0, 1);
    clear(GL_COLOR_BUFFER_BIT);
    popAttrib();
}

bool ValidWglProc(void* value) noexcept {
    const auto bits = reinterpret_cast<uintptr_t>(value);
    return value && bits != 1 && bits != 2 && bits != 3 && bits != static_cast<uintptr_t>(-1);
}
bool HasExtension(const char* extensions, const char* name) noexcept {
    if (!extensions || !name || std::strchr(name, ' ')) return false;
    const size_t length = std::strlen(name);
    for (const char* at = extensions; (at = std::strstr(at, name)) != nullptr; at += length)
        if ((at == extensions || at[-1] == ' ') && (at[length] == 0 || at[length] == ' ')) return true;
    return false;
}
void* Resolve(HMODULE module, const char* name) noexcept {
    void* value = reinterpret_cast<void*>(GetProcAddress(module, name));
    if (ValidWglProc(value)) return value;
    const auto wgl = reinterpret_cast<PROC (WINAPI*)(LPCSTR)>(GetProcAddress(module, "wglGetProcAddress"));
    value = wgl ? reinterpret_cast<void*>(wgl(name)) : nullptr;
    return ValidWglProc(value) ? value : nullptr;
}
void ClearDefaultBackBuffer() noexcept {
    // Manual notices own the foreground until their AdvWndInfo handle is
    // cleared. Input may be inhibited while that handle is still live,
    // so the owner/handle lifetime is the suspension boundary.
    if (!guard || !guard() || AdvWndInfoShowing()) return;
    HMODULE gl = GetModuleHandleW(L"opengl32.dll");
    if (!gl) return;
    const auto context = reinterpret_cast<GetCurrentContextFn>(GetProcAddress(gl, "wglGetCurrentContext"));
    const auto getInteger = reinterpret_cast<GetIntegerFn>(GetProcAddress(gl, "glGetIntegerv"));
    const auto getString = reinterpret_cast<GetStringFn>(GetProcAddress(gl, "glGetString"));
    const auto pushAttrib = reinterpret_cast<PushAttribFn>(GetProcAddress(gl, "glPushAttrib"));
    const auto popAttrib = reinterpret_cast<PopAttribFn>(GetProcAddress(gl, "glPopAttrib"));
    const auto disable = reinterpret_cast<EnableFn>(GetProcAddress(gl, "glDisable"));
    const auto colorMask = reinterpret_cast<ColorMaskFn>(GetProcAddress(gl, "glColorMask"));
    const auto clearColor = reinterpret_cast<ClearColorFn>(GetProcAddress(gl, "glClearColor"));
    const auto drawBuffer = reinterpret_cast<DrawBufferFn>(GetProcAddress(gl, "glDrawBuffer"));
    const auto clear = reinterpret_cast<ClearFn>(GetProcAddress(gl, "glClear"));
    if (!context || !context() || !getInteger || !getString || !pushAttrib || !popAttrib || !disable || !colorMask || !clearColor || !drawBuffer || !clear)
        return;
    const char* version = reinterpret_cast<const char*>(getString(GL_VERSION));
    const char* extensions = reinterpret_cast<const char*>(getString(GL_EXTENSIONS));
    const bool coreFbo = (version && version[0] >= '3') || HasExtension(extensions, "GL_ARB_framebuffer_object");
    const auto bindFramebuffer = coreFbo ? reinterpret_cast<BindFramebufferFn>(Resolve(gl, "glBindFramebuffer")) : nullptr;
    const auto bindFramebufferExt = !bindFramebuffer && HasExtension(extensions, "GL_EXT_framebuffer_object")
        ? reinterpret_cast<BindFramebufferFn>(Resolve(gl, "glBindFramebufferEXT")) : nullptr;
    int originalDrawFramebuffer = 0, originalReadFramebuffer = 0, originalExtFramebuffer = 0;
    if (bindFramebuffer) {
        getInteger(GL_DRAW_FRAMEBUFFER_BINDING, &originalDrawFramebuffer);
        getInteger(GL_READ_FRAMEBUFFER_BINDING, &originalReadFramebuffer);
    } else if (bindFramebufferExt) getInteger(GL_FRAMEBUFFER_BINDING, &originalExtFramebuffer);
    pushAttrib(GL_COLOR_BUFFER_BIT | GL_SCISSOR_BIT);
    if (bindFramebuffer) bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    else if (bindFramebufferExt) bindFramebufferExt(GL_FRAMEBUFFER, 0);
    // The default framebuffer has its own draw-buffer selection.  Save and
    // restore it separately: the attrib stack captured the caller's FBO.
    int defaultDrawBuffer = GL_BACK;
    getInteger(GL_DRAW_BUFFER, &defaultDrawBuffer);
    drawBuffer(GL_BACK);
    disable(GL_SCISSOR_TEST);
    colorMask(1, 1, 1, 1);
    clearColor(0, 0, 0, 1);
    clear(GL_COLOR_BUFFER_BIT);
    drawBuffer(static_cast<unsigned>(defaultDrawBuffer));
    if (bindFramebuffer) {
        bindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<unsigned>(originalDrawFramebuffer));
        // The read target was never rebound; this explicit no-op documents
        // the independent core state we preserved for test instrumentation.
        (void)originalReadFramebuffer;
    } else if (bindFramebufferExt) bindFramebufferExt(GL_FRAMEBUFFER, static_cast<unsigned>(originalExtFramebuffer));
    popAttrib();
}
}

void ConfigureAdvBlackout(uintptr_t base, BOOL (WINAPI* original)(HDC), AdvBlackoutGuard active,
                          AdvWindowSkinFn skin, AdvBlackoutInfoHandle infoHandle) noexcept {
    configuredBase = base;
    originalSwapBuffers = original;
    guard = active;
    originalWindowSkin = skin;
    configuredInfoHandle = infoHandle;
}

void AdvBlackoutBeforeWindow(uintptr_t widget) noexcept {
    if (WindowSkinClearActive(widget)) ClearCurrentDrawTarget();
}

BOOL WINAPI AdvBlackoutPresent(HDC device) noexcept {
    ClearDefaultBackBuffer();
    return originalSwapBuffers ? originalSwapBuffers(device) : FALSE;
}

void __cdecl AdvBlackoutWindowSkin(uintptr_t widget, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4) noexcept {
    AdvBlackoutBeforeWindow(widget);
    // Both verified call sites are cdecl with four pushed arguments. The
    // fourth is unused by this baseline callee, but remains part of the ABI.
    if (originalWindowSkin) originalWindowSkin(widget, arg2, arg3, arg4);
}

std::array<unsigned char, 6> AdvBlackoutExpectedCall(uintptr_t base, uint32_t iatRva) noexcept {
    const uint32_t slot = static_cast<uint32_t>(base + iatRva);
    return {0xff, 0x15, static_cast<unsigned char>(slot), static_cast<unsigned char>(slot >> 8),
            static_cast<unsigned char>(slot >> 16), static_cast<unsigned char>(slot >> 24)};
}
std::array<unsigned char, 6> AdvBlackoutReplacement(uintptr_t source) noexcept {
    const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&AdvBlackoutPresent) - source - 5);
    std::array<unsigned char, 6> result{0xe8, 0, 0, 0, 0, 0x90};
    std::memcpy(result.data() + 1, &relative, sizeof(relative));
    return result;
}

} // namespace rebirths
