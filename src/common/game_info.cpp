#include "app/app.h"
#include "common/keyboard.h"
#include "common/render_ext.h"
#include "common/settings_menu.h"

namespace shadow {
extern bool g_skip_cutscenes;  // src/game/hooks.cpp
extern bool g_cutscene_60;     // src/game/hooks.cpp
}

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"Shadow the Hedgehog",               // window and splash
    .short_title = L"Shadow",               // message boxes
    .name = L"ShadowNativePort",                      // window classes, crash dumps, handover event
    .log = "shadow",                        // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "Shadow"}},  // each recompiled xbe and its exe, for title launches
    .options = {{"keyboard", &keyboard::g_enabled, nullptr}, {"skip_cutscenes", &shadow::g_skip_cutscenes, nullptr}, {"cutscene_60fps", &shadow::g_cutscene_60, nullptr}, {"hd_scene", &shadow::g_hd_scene, nullptr}, {"show_fps", &shadow::g_show_fps, nullptr}},                        // game settings kept in the settings file
    .load = keyboard::Load,               // [keyboard_bindings] from the settings file's toml::table
    .save = keyboard::Save,               // and back into it
    .init = [] { keyboard::WriteControlsFile(); shadow::InitRender(); },                      // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = [](void* hwnd) { keyboard::OnWindow(hwnd); shadow::MenuWindow(hwnd); },                    // the game window is up
    .message = shadow::MenuMessage,                   // window messages first; true takes one
    .ui = shadow::MenuUi,                        // ImGui drawing each frame
    .pad = [](int port, input::Pad& pad) {
        shadow::MenuPad(port, pad);  // controller drives the menu (before keyboard keys are merged in)
        keyboard::ApplyToPad(port, pad);
        if (shadow::g_menu_open) {  // the menu has the input
            bool c = pad.connected;
            pad = input::Pad{};
            pad.connected = c;
        }
    },                       // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
};
