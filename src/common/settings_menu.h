#pragma once
#include <atomic>
#include <cstdint>

// In-game settings menu, F1 (src/common/settings_menu.cpp)
namespace shadow {
extern bool g_show_fps;                // settings.toml: show_fps
extern std::atomic<bool> g_menu_open;  // the game gets no input while it is open
void MenuWindow(void* hwnd);
bool MenuMessage(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);
void MenuUi(double fps);
}  // namespace shadow
