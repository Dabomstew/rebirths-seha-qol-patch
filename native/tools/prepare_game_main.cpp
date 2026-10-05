#include "prepare_ui_adapter.hpp"
#include <windows.h>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show) {
    rebirths::prepare::UiAdapter adapter;
    return preparer::RunGui(adapter, nullptr, show);
}
