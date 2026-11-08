// Keyboard controls for Shadow the Hedgehog: keys mapped onto controller port 1 and merged with a real controller, so both work at once.
// Only read while the game window has focus. Every binding can be changed in settings.toml, [keyboard_bindings]:
//   jump = ["Space"]          one or more key names per action (the names are listed in CONTROLS.txt)
//   action = []               an empty list leaves the action without a key
// settings.toml is saved at start, so [keyboard_bindings] is there to edit. Key names: see CONTROLS.txt.

#include "common/keyboard.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if __has_include(<toml++/toml.hpp>) && defined(_WIN32)
#define KEYBOARD_SETTINGS 1
#include <toml++/toml.hpp>
#include "app/app.h"
#else
#define KEYBOARD_SETTINGS 0
#endif

namespace keyboard {

bool g_enabled = true;

namespace {

// ------------------------------------------------------------------------------------------------ key names
struct KeyName { const char* name; int vk; };
const KeyName kKeys[] = {
    {"Space", 0x20}, {"Enter", 0x0D}, {"Escape", 0x1B}, {"Tab", 0x09}, {"Backspace", 0x08},
    {"LShift", 0xA0}, {"RShift", 0xA1}, {"LCtrl", 0xA2}, {"RCtrl", 0xA3}, {"LAlt", 0xA4}, {"RAlt", 0xA5},
    {"Up", 0x26}, {"Down", 0x28}, {"Left", 0x25}, {"Right", 0x27},
    {"Insert", 0x2D}, {"Delete", 0x2E}, {"Home", 0x24}, {"End", 0x23}, {"PageUp", 0x21}, {"PageDown", 0x22},
    {"Numpad0", 0x60}, {"Numpad1", 0x61}, {"Numpad2", 0x62}, {"Numpad3", 0x63}, {"Numpad4", 0x64},
    {"Numpad5", 0x65}, {"Numpad6", 0x66}, {"Numpad7", 0x67}, {"Numpad8", 0x68}, {"Numpad9", 0x69},
    {"NumpadPlus", 0x6B}, {"NumpadMinus", 0x6D}, {"NumpadStar", 0x6A}, {"NumpadSlash", 0x6F}, {"NumpadDot", 0x6E},
    {"Semicolon", 0xBA}, {"Equals", 0xBB}, {"Comma", 0xBC}, {"Minus", 0xBD}, {"Period", 0xBE}, {"Slash", 0xBF},
    {"Backquote", 0xC0}, {"LBracket", 0xDB}, {"Backslash", 0xDC}, {"RBracket", 0xDD}, {"Quote", 0xDE},
    {"F2", 0x71}, {"F4", 0x73}, {"F5", 0x74}, {"F6", 0x75}, {"F7", 0x76}, {"F8", 0x77}, {"F9", 0x78},
    {"F10", 0x79}, {"F11", 0x7A}, {"F12", 0x7B},
    {"MouseLeft", 0x01}, {"MouseRight", 0x02}, {"MouseMiddle", 0x04}, {"Mouse4", 0x05}, {"Mouse5", 0x06},
};

std::string NameOf(int vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, char(vk));
    for (const KeyName& k : kKeys)
        if (k.vk == vk) return k.name;
    return {};
}

int VkOf(const std::string& name) {
    if (name.size() == 1) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') c = char(c - 32);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    }
    for (const KeyName& k : kKeys) {
        const char* a = k.name;
        const char* b = name.c_str();
        while (*a && *b && (*a | 32) == (*b | 32)) { ++a; ++b; }
        if (!*a && !*b) return k.vk;
    }
    return 0;
}

// ------------------------------------------------------------------------------------------------ actions
enum Kind { kButton, kAnalog, kAxis, kWalk };
// pad bit order of input::Pad::buttons
enum : uint16_t {
    kDpadUp = 1 << 0, kDpadDown = 1 << 1, kDpadLeft = 1 << 2, kDpadRight = 1 << 3,
    kStart = 1 << 4, kBack = 1 << 5, kLeftThumb = 1 << 6, kRightThumb = 1 << 7,
};
// input::Pad::analog slots
enum { kA, kB, kX, kY, kBlack, kWhite, kLT, kRT };
// axis targets: 0 lx, 1 ly, 2 rx, 3 ry; sign -1 or +1 (up is positive)

struct Action {
    const char* key;     // settings.toml name
    const char* label;   // CONTROLS.txt
    Kind kind;
    int target;          // button bit, analog slot or axis
    int sign;            // axis direction
    std::vector<int> keys;
};

std::vector<Action> g_actions = {
    {"move_up", "Move up (left stick)", kAxis, 1, +1, {'W'}},
    {"move_down", "Move down", kAxis, 1, -1, {'S'}},
    {"move_left", "Move left", kAxis, 0, -1, {'A'}},
    {"move_right", "Move right", kAxis, 0, +1, {'D'}},
    {"walk", "Walk (half tilt, hold)", kWalk, 0, 0, {0xA4}},
    {"camera_up", "Camera up (right stick)", kAxis, 3, +1, {'I'}},
    {"camera_down", "Camera down", kAxis, 3, -1, {'K'}},
    {"camera_left", "Camera left", kAxis, 2, -1, {'J'}},
    {"camera_right", "Camera right", kAxis, 2, +1, {'L'}},
    {"a", "A  (jump, confirm)", kAnalog, kA, 0, {0x20}},
    {"b", "B  (action, back)", kAnalog, kB, 0, {0xA0, 0x08}},
    {"x", "X", kAnalog, kX, 0, {'F'}},
    {"y", "Y", kAnalog, kY, 0, {'R'}},
    {"black", "Black", kAnalog, kBlack, 0, {'C'}},
    {"white", "White", kAnalog, kWhite, 0, {'V'}},
    {"left_trigger", "Left trigger", kAnalog, kLT, 0, {'Q'}},
    {"right_trigger", "Right trigger", kAnalog, kRT, 0, {'E'}},
    {"start", "Start (pause, Press START)", kButton, kStart, 0, {0x0D, 0x1B}},
    {"back", "Back", kButton, kBack, 0, {0x09}},
    {"left_stick_click", "Left stick click", kButton, kLeftThumb, 0, {'Z'}},
    {"right_stick_click", "Right stick click", kButton, kRightThumb, 0, {'T'}},
    {"dpad_up", "D-pad up", kButton, kDpadUp, 0, {0x26}},
    {"dpad_down", "D-pad down", kButton, kDpadDown, 0, {0x28}},
    {"dpad_left", "D-pad left", kButton, kDpadLeft, 0, {0x25}},
    {"dpad_right", "D-pad right", kButton, kDpadRight, 0, {0x27}},
};

// the defaults, for RESET ALL; the lock keeps the menu's rebinding off the keys the game is reading
const std::vector<std::vector<int>> kDefaults = [] {
    std::vector<std::vector<int>> d;
    for (const Action& a : g_actions) d.push_back(a.keys);
    return d;
}();
std::mutex g_lock;

#ifdef _WIN32
HWND g_window = nullptr;

bool Focused() {
    if (!g_window) return false;
    HWND fg = GetForegroundWindow();
    return fg && (fg == g_window || GetAncestor(fg, GA_ROOT) == GetAncestor(g_window, GA_ROOT));
}
bool Down(int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000) != 0; }
#else
bool Focused() { return false; }  // headless harness: input comes from pad scripts
bool Down(int) { return false; }
#endif

bool AnyDown(const Action& a) {
    for (int k : a.keys)
        if (Down(k)) return true;
    return false;
}

}  // namespace

void OnWindow(void* hwnd) {
#ifdef _WIN32
    g_window = static_cast<HWND>(hwnd);
#else
    (void)hwnd;
#endif
}

void ApplyToPad(int port, input::Pad& pad) {
    if (!g_enabled || port != 0) return;
    // a keyboard-only player still needs a controller in port 1 for the game to read; stays plugged in while the
    // window is in the background so the game does not pause with "controller removed"
    pad.connected = true;
    if (!Focused()) return;

    std::lock_guard<std::mutex> l(g_lock);
    uint16_t b = 0;
    uint8_t an[8] = {};
    int axis[4][2] = {};  // [axis][negative, positive] held
    bool walk = false;
    for (const Action& a : g_actions) {
        if (!AnyDown(a)) continue;
        switch (a.kind) {
        case kButton: b |= uint16_t(a.target); break;
        case kAnalog: an[a.target] = 255; break;
        case kAxis: axis[a.target][a.sign > 0] = 1; break;
        case kWalk: walk = true; break;
        }
    }
    auto value = [&](int i, int16_t full) -> int16_t {
        if (axis[i][0] == axis[i][1]) return 0;
        return axis[i][1] ? full : int16_t(-full);
    };
    int16_t move = walk ? 16384 : 32767;
    int16_t lx = value(0, move), ly = value(1, move), rx = value(2, 32767), ry = value(3, 32767);
    // keyboard wins on an axis while one of its keys is held, otherwise the controller's value stays
    if (lx) pad.lx = lx;
    if (ly) pad.ly = ly;
    if (rx) pad.rx = rx;
    if (ry) pad.ry = ry;
    pad.buttons |= b;
    for (int i = 0; i < 8; i++) pad.analog[i] = std::max(pad.analog[i], an[i]);
}

void Load(const void* table) {
#if KEYBOARD_SETTINGS
    const toml::table& t = *static_cast<const toml::table*>(table);
    const toml::table* kb = t["keyboard_bindings"].as_table();
    if (!kb) return;
    for (Action& a : g_actions) {
        const toml::node* n = kb->get(a.key);
        if (!n) continue;
        std::vector<int> keys;
        auto add = [&](const toml::node& e) {
            if (auto s = e.value<std::string>()) {
                if (int vk = VkOf(*s)) keys.push_back(vk);
                else std::fprintf(stderr, "[keyboard] %s: unknown key \"%s\" (see CONTROLS.txt for the names)\n", a.key, s->c_str());
            }
        };
        if (const toml::array* arr = n->as_array()) arr->for_each([&](const toml::node& e) { add(e); });
        else add(*n);
        a.keys = keys;
    }
#else
    (void)table;
#endif
}

void Save(void* table) {
#if KEYBOARD_SETTINGS
    toml::table& t = *static_cast<toml::table*>(table);
    std::lock_guard<std::mutex> l(g_lock);
    toml::table kb;
    for (const Action& a : g_actions) {
        toml::array arr;
        for (int k : a.keys)
            if (std::string n = NameOf(k); !n.empty()) arr.push_back(n);
        kb.insert(a.key, arr);
    }
    t.insert_or_assign("keyboard_bindings", kb);
#else
    (void)table;
#endif
}

std::vector<std::pair<std::string, std::string>> Bindings() {
    std::lock_guard<std::mutex> l(g_lock);
    std::vector<std::pair<std::string, std::string>> out;
    for (const Action& a : g_actions) {
        std::string keys;
        for (int k : a.keys) {
            std::string n = NameOf(k);
            if (n.empty()) continue;
            if (!keys.empty()) keys += ", ";
            keys += n;
        }
        out.push_back({a.label, keys.empty() ? "(none)" : keys});
    }
    return out;
}

bool Bind(size_t action, int vk) {
    if (NameOf(vk).empty()) return false;
    std::lock_guard<std::mutex> l(g_lock);
    if (action >= g_actions.size()) return false;
    for (Action& a : g_actions) a.keys.erase(std::remove(a.keys.begin(), a.keys.end(), vk), a.keys.end());
    g_actions[action].keys = {vk};
    return true;
}

void ResetAll() {
    std::lock_guard<std::mutex> l(g_lock);
    for (size_t i = 0; i < g_actions.size(); i++) g_actions[i].keys = kDefaults[i];
}

void WriteControlsFile() {
#if KEYBOARD_SETTINGS
    app::Save();  // settings.toml gets every setting, the key bindings included, so they can be edited
#endif
}

}  // namespace keyboard
