#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    HMODULE proxy = LoadLibraryA(argv[1]);
    if (!proxy) return 3;
    const bool valid = GetProcAddress(proxy, "X3DAudioInitialize") != nullptr &&
                       GetProcAddress(proxy, "X3DAudioCalculate") != nullptr;
    FreeLibrary(proxy);
    if (!valid) { std::fputs("missing required X3DAudio proxy exports\n", stderr); return 4; }
    return 0;
}
