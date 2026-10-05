#include "adv_blackout.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>

namespace mock {
struct State {
    int drawFbo = 7, readFbo = 3;
    unsigned drawBuffer = 0x8ce2, defaultBuffer = 0x404;
    bool scissor = true;
    unsigned char mask[4]{0, 1, 0, 1};
    float color[4]{.2f, .3f, .4f, .5f};
} state, saved;
bool active = true, hasContext = true;
const char* version = "3.3";
const char* extensions = "";
int clears = 0, swaps = 0, queries = 0, binds = 0, pushes = 0, pops = 0;
int skins = 0, expectedClearsAtSkin = 0, expectedClearFbo = 0;
unsigned expectedClearBuffer = 0x405;
uintptr_t skinArgs[4]{};
HDC expectedDevice = reinterpret_cast<HDC>(0x1234);
bool Guard() noexcept { return active; }
uintptr_t CustomInfoHandle() noexcept { return 0x87654321; }
BOOL WINAPI Swap(HDC device) { assert(device == expectedDevice); ++swaps; return 73; }
void __cdecl Skin(uintptr_t widget, uintptr_t arg2, uintptr_t arg3, uintptr_t arg4) {
    assert(clears == expectedClearsAtSkin);
    skinArgs[0] = widget; skinArgs[1] = arg2; skinArgs[2] = arg3; skinArgs[3] = arg4;
    ++skins;
}
HGLRC WINAPI Context() { return hasContext ? reinterpret_cast<HGLRC>(1) : nullptr; }
const unsigned char* APIENTRY String(unsigned name) {
    return reinterpret_cast<const unsigned char*>(name == 0x1f02 ? version : extensions);
}
void APIENTRY Integer(unsigned name, int* value) {
    ++queries;
    if (name == 0x8ca6) *value = state.drawFbo;
    else if (name == 0x8caa) *value = state.readFbo;
    else { assert(name == 0xc01); *value = state.drawFbo ? state.drawBuffer : state.defaultBuffer; }
}
void APIENTRY Push(unsigned long bits) { assert(bits == 0x84000); saved = state; ++pushes; }
void APIENTRY Pop() {
    assert(state.drawFbo == saved.drawFbo && state.readFbo == saved.readFbo);
    // Draw-buffer selection belongs to the bound FBO. Pop cannot undo a
    // leaked mutation of the default FBO while another one is bound.
    if (state.drawFbo) assert(state.defaultBuffer == saved.defaultBuffer);
    state.drawBuffer = saved.drawBuffer;
    if (!state.drawFbo) state.defaultBuffer = saved.defaultBuffer;
    state.scissor = saved.scissor;
    std::memcpy(state.mask, saved.mask, sizeof(state.mask));
    std::memcpy(state.color, saved.color, sizeof(state.color));
    ++pops;
}
void APIENTRY Disable(unsigned name) { assert(name == 0xc11); state.scissor = false; }
void APIENTRY Mask(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    state.mask[0] = r; state.mask[1] = g; state.mask[2] = b; state.mask[3] = a;
}
void APIENTRY Color(float r, float g, float b, float a) {
    state.color[0] = r; state.color[1] = g; state.color[2] = b; state.color[3] = a;
}
void APIENTRY Buffer(unsigned value) { (state.drawFbo ? state.drawBuffer : state.defaultBuffer) = value; }
void APIENTRY Clear(unsigned long bits) {
    assert(bits == 0x4000 && state.drawFbo == expectedClearFbo && !state.scissor);
    assert((state.drawFbo ? state.drawBuffer : state.defaultBuffer) == expectedClearBuffer);
    for (auto value : state.mask) assert(value == 1);
    assert(state.color[0] == 0 && state.color[1] == 0 && state.color[2] == 0 && state.color[3] == 1);
    ++clears;
}
void APIENTRY Bind(unsigned target, unsigned value) {
    assert(target == 0x8ca9 || target == 0x8d40);
    state.drawFbo = value;
    if (target == 0x8d40) state.readFbo = value;
    ++binds;
}
PROC WINAPI WglProc(LPCSTR name) {
    if (!std::strcmp(name, "glBindFramebuffer") || !std::strcmp(name, "glBindFramebufferEXT"))
        return reinterpret_cast<PROC>(Bind);
    return nullptr;
}
HMODULE WINAPI Module(LPCWSTR) { return reinterpret_cast<HMODULE>(1); }
FARPROC WINAPI Proc(HMODULE, LPCSTR name) {
#define MAP(n, f) if (!std::strcmp(name, n)) return reinterpret_cast<FARPROC>(f)
    MAP("wglGetCurrentContext", Context); MAP("glGetIntegerv", Integer); MAP("glGetString", String);
    MAP("glPushAttrib", Push); MAP("glPopAttrib", Pop); MAP("glDisable", Disable);
    MAP("glColorMask", Mask); MAP("glClearColor", Color); MAP("glDrawBuffer", Buffer);
    MAP("glClear", Clear); MAP("wglGetProcAddress", WglProc);
#undef MAP
    return nullptr;
}
void Reset() {
    state = State{}; clears = swaps = queries = binds = pushes = pops = skins = 0;
    expectedClearsAtSkin = 0; expectedClearFbo = 0; expectedClearBuffer = 0x405;
    std::memset(skinArgs, 0, sizeof(skinArgs));
    active = hasContext = true; version = "3.3"; extensions = "";
}
void CheckState(const State& before) {
    assert(state.drawFbo == before.drawFbo && state.readFbo == before.readFbo);
    assert(state.drawBuffer == before.drawBuffer && state.defaultBuffer == before.defaultBuffer && state.scissor == before.scissor);
    assert(!std::memcmp(state.mask, before.mask, sizeof(state.mask)) && !std::memcmp(state.color, before.color, sizeof(state.color)));
}
void PresentAndCheck(bool expectedClear) {
    const State before = state;
    assert(rebirths::AdvBlackoutPresent(expectedDevice) == 73);
    assert(swaps == 1 && clears == int(expectedClear));
    assert(pushes == int(expectedClear) && pops == int(expectedClear));
    CheckState(before);
}
void Store(uintptr_t base, size_t offset, uintptr_t value) {
    std::memcpy(reinterpret_cast<void*>(base + offset), &value, sizeof(value));
}
}

// Keep dependency injection in the test translation unit. Production code
// still resolves the actual Win32/OpenGL exports without a test-facing API.
#define GetModuleHandleW mock::Module
#define GetProcAddress mock::Proc
#include "../src/adv_blackout.cpp"
#undef GetModuleHandleW
#undef GetProcAddress

int main() {
    using namespace mock;
    std::vector<unsigned char> image(0x459208 + sizeof(uintptr_t));
    std::vector<unsigned char> manager(0x280 + sizeof(uintptr_t));
    const uintptr_t imageBase = reinterpret_cast<uintptr_t>(image.data());
    const uintptr_t managerBase = reinterpret_cast<uintptr_t>(manager.data());
    rebirths::ConfigureAdvBlackout(imageBase, Swap, Guard, Skin);
    Reset(); PresentAndCheck(true); assert(binds == 2); // Distinct read/draw FBOs.
    Reset(); version = "2.1"; extensions = "GL_ARB_framebuffer_object"; PresentAndCheck(true);
    Reset(); version = "2.1"; extensions = "GL_EXT_framebuffer_object"; state.readFbo = state.drawFbo; PresentAndCheck(true);
    Reset(); state.drawFbo = state.readFbo = 0; PresentAndCheck(true);
    Reset(); version = "1.1"; state.drawFbo = state.readFbo = 0; PresentAndCheck(true); assert(binds == 0 && queries == 1);
    Reset(); version = "2.1"; extensions = "GL_ARB_framebuffer_object_suffix"; state.drawFbo = state.readFbo = 0; PresentAndCheck(true); assert(binds == 0);
    Reset(); active = false; PresentAndCheck(false); assert(queries == 0 && binds == 0);
    Reset(); hasContext = false; PresentAndCheck(false); assert(queries == 0 && binds == 0);
    Store(imageBase, 0x459208, managerBase);
    Store(managerBase, 0, 1);
    Store(managerBase, 0x27c, 1);
    Store(managerBase, 0x70, 0x12345678);
    Reset();
    const State offscreenBefore = state;
    expectedClearFbo = state.drawFbo; expectedClearBuffer = state.drawBuffer; expectedClearsAtSkin = 1;
    rebirths::AdvBlackoutWindowSkin(0x12345678, 2, 3, 4);
    assert(skins == 1 && clears == 1 && binds == 0 && pushes == 1 && pops == 1);
    assert(skinArgs[0] == 0x12345678 && skinArgs[1] == 2 && skinArgs[2] == 3 && skinArgs[3] == 4);
    CheckState(offscreenBefore); // Clear the current offscreen draw target before forwarding Skin.
    Reset(); state.drawFbo = state.readFbo = 0;
    const State defaultBefore = state;
    expectedClearFbo = 0; expectedClearBuffer = 0x405; expectedClearsAtSkin = 1;
    rebirths::AdvBlackoutWindowSkin(0x12345678, 2, 3, 4);
    assert(skins == 1 && clears == 1 && binds == 0 && pushes == 1 && pops == 1);
    CheckState(defaultBefore); // Default target selects GL_BACK without rebinding.
    Reset(); version = "2.1"; extensions = "GL_ARB_framebuffer_object";
    const State arbBefore = state;
    expectedClearFbo = state.drawFbo; expectedClearBuffer = state.drawBuffer; expectedClearsAtSkin = 1;
    rebirths::AdvBlackoutWindowSkin(0x12345678, 2, 3, 4);
    assert(skins == 1 && clears == 1 && binds == 0 && pushes == 1 && pops == 1);
    CheckState(arbBefore); // ARB FBO retains its current draw attachment.
    Reset(); version = "2.1"; extensions = "GL_EXT_framebuffer_object";
    const State extBefore = state;
    expectedClearFbo = state.drawFbo; expectedClearBuffer = state.drawBuffer; expectedClearsAtSkin = 1;
    rebirths::AdvBlackoutWindowSkin(0x12345678, 2, 3, 4);
    assert(skins == 1 && clears == 1 && binds == 0 && pushes == 1 && pops == 1);
    CheckState(extBefore); // EXT FBO retains its current draw attachment.
    Reset(); rebirths::AdvBlackoutWindowSkin(0x12345679, 2, 3, 4);
    assert(skins == 1 && clears == 0); // Mismatched widget forwards without clear.
    Store(managerBase, 0x70, 0);
    Reset(); rebirths::AdvBlackoutWindowSkin(0x12345678, 2, 3, 4);
    assert(skins == 1 && clears == 0); // Closed owner forwards without clear.
    Store(managerBase, 0x70, 0x12345678);
    Reset(); active = false; rebirths::AdvBlackoutWindowSkin(0x12345678, 2, 3, 4);
    assert(skins == 1 && clears == 0); // Inactive fast-forward forwards without clear.
    Reset(); hasContext = false; rebirths::AdvBlackoutWindowSkin(0x12345678, 2, 3, 4);
    assert(skins == 1 && clears == 0); // No GL context still forwards exactly once.
    Reset(); PresentAndCheck(false); assert(queries == 0 && binds == 0); // Open manual notice.
    Store(managerBase, 0x27c, 3);
    Reset(); PresentAndCheck(false); assert(queries == 0 && binds == 0); // Input inhibition must not end ownership.
    Store(managerBase, 0x70, 0);
    Reset(); PresentAndCheck(true); // AdvWndInfo dismissal resumes blackout.
    rebirths::ConfigureAdvBlackout(0, Swap, Guard, nullptr, CustomInfoHandle);
    Reset(); PresentAndCheck(false);
    Reset(); expectedClearFbo = state.drawFbo; expectedClearBuffer = state.drawBuffer;
    const State customBefore = state;
    rebirths::AdvBlackoutBeforeWindow(0x87654320); assert(clears == 0);
    rebirths::AdvBlackoutBeforeWindow(0x87654321); assert(clears == 1 && skins == 0);
    CheckState(customBefore);
    rebirths::ConfigureAdvBlackout(0, Swap, nullptr, nullptr);
    Reset(); PresentAndCheck(false);
    for (uintptr_t value : {uintptr_t(0), uintptr_t(1), uintptr_t(2), uintptr_t(3), ~uintptr_t(0)})
        assert(!rebirths::ValidWglProc(reinterpret_cast<void*>(value)));
    assert(rebirths::ValidWglProc(reinterpret_cast<void*>(0x1000)));
    const auto expected = rebirths::AdvBlackoutExpectedCall(0x500000);
    assert(expected[0] == 0xff && expected[1] == 0x15);
    uint32_t operand = 0; std::memcpy(&operand, expected.data() + 2, 4); assert(operand == 0x82f04c);
    const auto rb3 = rebirths::AdvBlackoutExpectedCall(0x5e0000, 0x38304c);
    std::memcpy(&operand, rb3.data()+2, 4); assert(operand == 0x96304c);
    std::puts("Adv blackout GL state and presentation ABI tests passed");
}
