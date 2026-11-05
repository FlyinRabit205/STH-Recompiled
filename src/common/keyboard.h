#pragma once
#include <string>
#include <utility>
#include <vector>

#include "core/input.h"

namespace keyboard {
extern bool g_enabled;          // settings.toml: keyboard
void OnWindow(void* hwnd);      // the game window, for the focus check
void ApplyToPad(int port, input::Pad& pad);  // keyboard keys merged into controller port 1
void Load(const void* table);   // settings.toml [keyboard_bindings]
void Save(void* table);
void WriteControlsFile();       // saves settings.toml at start, so the bindings are there to edit
// each action's label and its keys as text ("Space, J"), for the options menu
std::vector<std::pair<std::string, std::string>> Bindings();
}
