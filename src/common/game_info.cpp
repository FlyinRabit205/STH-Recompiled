#include "app/app.h"
#include "common/keyboard.h"
#include "common/render_ext.h"

namespace shadow {
extern bool g_skip_cutscenes;  // src/game/hooks.cpp
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
    .options = {{"keyboard", &keyboard::g_enabled, nullptr}, {"skip_cutscenes", &shadow::g_skip_cutscenes, nullptr}, {"hd_scene", &shadow::g_hd_scene, nullptr}},                        // game settings kept in the settings file
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = shadow::InitRender,                      // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = keyboard::OnWindow,                    // the game window is up
    .message = nullptr,                   // window messages first; true takes one
    .ui = nullptr,                        // ImGui drawing each frame
    .pad = keyboard::ApplyToPad,                       // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
};
