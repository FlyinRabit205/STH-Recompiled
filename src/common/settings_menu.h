#pragma once
#include <atomic>
#include <cstdint>

#include "core/input.h"

// In-game options menu, F1 or Back+Start (src/common/settings_menu.cpp)
namespace shadow {
extern bool g_show_fps;                // settings.toml: show_fps
extern bool g_widescreen;              // settings.toml: widescreen (src/game/hooks.cpp)
extern std::atomic<bool> g_menu_open;  // the game gets no input while it is open
void MenuWindow(void* hwnd);
bool MenuMessage(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);
void MenuPad(int port, const input::Pad& pad);  // the real controller, before the game sees it
void MenuUi(double fps);
}  // namespace shadow
