// Keyboard controls for Shadow the Hedgehog: mapped onto controller port 1 and merged with a real controller, so
// both work at once. Only read while the game window has focus.
//
// Default layout (also written to CONTROLS.txt beside the exe):
//   Move (left stick)        W A S D            hold Left Alt to walk (half tilt)
//   Camera (right stick)     I J K L
//   D-pad                    Arrow keys
//   A  jump / confirm        Space
//   B  action / back         Left Shift, Backspace
//   X                        F
//   Y                        R
//   Black                    C
//   White                    V
//   Left trigger             Q
//   Right trigger            E
//   Start  (pause, "press start")   Enter, Escape
//   Back                     Tab
//   Left stick click         Z
//   Right stick click        T

#include "common/keyboard.h"

#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace keyboard {

bool g_enabled = true;

namespace {

#ifdef _WIN32
HWND g_window = nullptr;

bool Focused() {
    if (!g_window) return false;
    HWND fg = GetForegroundWindow();
    return fg && (fg == g_window || GetAncestor(fg, GA_ROOT) == GetAncestor(g_window, GA_ROOT));
}
bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
#else
bool Focused() { return false; }  // headless harness: input comes from pad scripts
bool Down(int) { return false; }
#endif

enum : int {
    kSpace = 0x20, kEnter = 0x0D, kEscape = 0x1B, kTab = 0x09, kBackspace = 0x08,
    kLShift = 0xA0, kLAlt = 0xA4,
    kLeft = 0x25, kUp = 0x26, kRight = 0x27, kDown = 0x28,
};
constexpr int Key(char c) { return int(c); }  // 'A'..'Z' are their own virtual-key codes

// pad bit order of input::Pad::buttons
enum : uint16_t {
    kDpadUp = 1 << 0, kDpadDown = 1 << 1, kDpadLeft = 1 << 2, kDpadRight = 1 << 3,
    kStart = 1 << 4, kBack = 1 << 5, kLeftThumb = 1 << 6, kRightThumb = 1 << 7,
};
// input::Pad::analog slots
enum { kA, kB, kX, kY, kBlack, kWhite, kLT, kRT };

int16_t Axis(bool neg, bool pos, int16_t full) {
    if (neg == pos) return 0;
    return pos ? full : int16_t(-full);
}

// keyboard wins on an axis while one of its keys is held, otherwise the controller's value stays
void Merge(int16_t& axis, int16_t key) {
    if (key) axis = key;
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

    uint16_t b = 0;
    if (Down(kUp)) b |= kDpadUp;
    if (Down(kDown)) b |= kDpadDown;
    if (Down(kLeft)) b |= kDpadLeft;
    if (Down(kRight)) b |= kDpadRight;
    if (Down(kEnter) || Down(kEscape)) b |= kStart;
    if (Down(kTab)) b |= kBack;
    if (Down(Key('Z'))) b |= kLeftThumb;
    if (Down(Key('T'))) b |= kRightThumb;

    uint8_t a[8] = {};
    auto press = [&](int slot, bool on) { if (on) a[slot] = 255; };
    press(kA, Down(kSpace));
    press(kB, Down(kLShift) || Down(kBackspace));
    press(kX, Down(Key('F')));
    press(kY, Down(Key('R')));
    press(kBlack, Down(Key('C')));
    press(kWhite, Down(Key('V')));
    press(kLT, Down(Key('Q')));
    press(kRT, Down(Key('E')));

    int16_t full = Down(kLAlt) ? 16384 : 32767;
    int16_t lx = Axis(Down(Key('A')), Down(Key('D')), full);
    int16_t ly = Axis(Down(Key('S')), Down(Key('W')), full);  // up is positive
    int16_t rx = Axis(Down(Key('J')), Down(Key('L')), 32767);
    int16_t ry = Axis(Down(Key('K')), Down(Key('I')), 32767);

    bool any = b || lx || ly || rx || ry || std::any_of(a, a + 8, [](uint8_t v) { return v != 0; });
    if (!any) return;
    pad.buttons |= b;
    for (int i = 0; i < 8; i++) pad.analog[i] = std::max(pad.analog[i], a[i]);
    Merge(pad.lx, lx);
    Merge(pad.ly, ly);
    Merge(pad.rx, rx);
    Merge(pad.ry, ry);
}

}  // namespace keyboard
