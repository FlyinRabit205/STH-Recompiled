#pragma once
#include "core/input.h"

// Keyboard controls merged into controller port 1 (src/common/keyboard.cpp has the layout)
namespace keyboard {
extern bool g_enabled;                         // settings.toml: keyboard = true/false
void OnWindow(void* hwnd);                     // GameInfo::window
void ApplyToPad(int port, input::Pad& pad);    // GameInfo::pad
}  // namespace keyboard
