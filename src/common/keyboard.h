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
// rebinding from the options menu (CONTROLS tab)
bool Bind(size_t action, int vk);  // the action's key becomes vk (taken off any other action); false: not a usable key
void ResetAll();                   // every action back to its default keys
}
