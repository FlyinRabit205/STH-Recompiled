// In-game options menu: F1 (keyboard) or Back+Start (controller).
// Drawn with Dear ImGui's draw lists as a game-style screen: dimmed backdrop, OPTIONS title, tabs, rows with
// < value > selectors, a description of the selected option and button hints. Every change is saved to settings.toml
// at once; display changes go through app::ApplyDisplay, the render scale is picked up by the renderer each frame.

#include "common/settings_menu.h"

#include "common/keyboard.h"
#include "common/render_ext.h"

namespace shadow {
extern bool g_skip_cutscenes;  // src/game/hooks.cpp
extern bool g_cutscene_60;     // src/game/hooks.cpp
bool g_show_fps = false;
std::atomic<bool> g_menu_open{false};
}  // namespace shadow

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "app/app.h"

namespace shadow {
namespace {

HWND g_hwnd = nullptr;
ImFont* g_title = nullptr;  // big header
ImFont* g_body = nullptr;   // rows, tabs
ImFont* g_small = nullptr;  // descriptions, hints
float g_font_px = 1080;     // output height the fonts were made for

// ------------------------------------------------------------------------------------------------ input
enum class Nav { Up, Down, Left, Right, PrevTab, NextTab, Accept };
std::mutex g_nav_lock;
std::vector<Nav> g_nav;  // the window thread and pad polling push, the menu drains each frame

void Push(Nav n) {
    std::lock_guard<std::mutex> l(g_nav_lock);
    g_nav.push_back(n);
}

// ------------------------------------------------------------------------------------------------ options
struct Res {
    uint32_t w, h;
};

// the resolutions the game window's monitor offers, smallest first (Right goes up in size)
std::vector<Res> MonitorModes() {
    std::set<std::pair<uint32_t, uint32_t>> seen;
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    const wchar_t* device = nullptr;
    if (g_hwnd && GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) device = mi.szDevice;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(device, i, &dm); i++)
        if (dm.dmPelsWidth >= 640 && dm.dmPelsHeight >= 480) seen.insert({dm.dmPelsWidth, dm.dmPelsHeight});
    std::vector<Res> out;
    for (auto& p : seen) out.push_back({p.first, p.second});
    return out;
}

std::string ResLabel(uint32_t w, uint32_t h) {
    char b[48];
    std::snprintf(b, sizeof b, "%u x %u", w, h);
    return b;
}

void Saved(bool display) {
    app::Save();
    if (display) app::ApplyDisplay();
}

// one option row: label, value text, change by -1/+1, description
struct Row {
    const char* label;
    std::function<std::string()> value;
    std::function<void(int)> change;
    const char* desc;
};

std::vector<Row> DisplayRows() {
    app::Settings& s = app::Get();
    std::vector<Row> r;
    r.push_back({"DISPLAY MODE",
                 [&s] {
                     static const char* k[] = {"FULLSCREEN", "BORDERLESS", "WINDOW"};
                     return std::string(k[uint32_t(s.display_mode) % 3]);
                 },
                 [&s](int d) {
                     s.display_mode = app::DisplayMode((uint32_t(s.display_mode) + 3 + uint32_t(d + 3)) % 3);
                     Saved(true);
                 },
                 "Fullscreen takes over the monitor. Borderless fills the desktop. Window can be moved and resized."});
    r.push_back({"RESOLUTION",
                 [&s] {
                     if (!s.width || !s.height) {
                         app::Mode d = app::Desktop();
                         return "DESKTOP  (" + ResLabel(d.width, d.height) + ")";
                     }
                     return ResLabel(s.width, s.height);
                 },
                 [&s](int d) {
                     std::vector<Res> modes = MonitorModes();
                     int i = -1;  // -1 is Desktop
                     for (size_t k = 0; k < modes.size(); k++)
                         if (modes[k].w == s.width && modes[k].h == s.height) i = int(k);
                     i = std::clamp(i + d, -1, int(modes.size()) - 1);
                     if (i < 0) {
                         s.width = s.height = 0;
                     } else {
                         s.width = modes[size_t(i)].w;
                         s.height = modes[size_t(i)].h;
                     }
                     Saved(true);
                 },
                 "The game renders at this resolution. Borderless always uses the desktop size."});
    r.push_back({"V-SYNC", [&s] { return std::string(s.vsync ? "ON" : "OFF"); },
                 [&s](int) {
                     s.vsync = !s.vsync;
                     Saved(false);
                 },
                 "Waits for the monitor between frames, which removes tearing."});
    r.push_back({"FRAME RATE", [&s] { return std::string(s.fps_cap == 30 ? "30 FPS" : "60 FPS"); },
                 [&s](int) {
                     s.fps_cap = s.fps_cap == 30 ? 60 : 30;
                     Saved(false);
                 },
                 "60 FPS is the game's original speed."});
    return r;
}

std::vector<Row> GraphicsRows() {
    app::Settings& s = app::Get();
    std::vector<Row> r;
    r.push_back({"RENDER RESOLUTION",
                 [] {
                     float sc = app::RenderScale(480);
                     char b[64];
                     std::snprintf(b, sizeof b, "%u%%   (%u x %u)", app::Get().render_percent, unsigned(640 * sc + 0.5f),
                                   unsigned(480 * sc + 0.5f));
                     return std::string(b);
                 },
                 [&s](int d) {
                     s.render_percent = uint32_t(std::clamp(int(s.render_percent) + d * 25, 50, 200));
                     Saved(false);
                 },
                 "100% is sharp at your resolution. Higher supersamples for a smoother picture and costs speed."});
    r.push_back({"TEXTURE FILTERING",
                 [&s] {
                     if (!s.aniso) return std::string("STANDARD");
                     char b[24];
                     std::snprintf(b, sizeof b, "%ux ANISOTROPIC", s.aniso);
                     return std::string(b);
                 },
                 [&s](int d) {
                     static const uint32_t v[] = {0, 2, 4, 8, 16};
                     int i = 0;
                     for (int k = 0; k < 5; k++)
                         if (v[k] == s.aniso) i = k;
                     s.aniso = v[std::clamp(i + d, 0, 4)];
                     Saved(false);
                 },
                 "Keeps ground and wall textures sharp when seen at an angle."});
    r.push_back({"HD 3D WORLD", [] { return std::string(g_hd_scene ? "ON" : "OFF"); },
                 [](int) {
                     g_hd_scene = !g_hd_scene;
                     Saved(false);
                 },
                 "Renders stages at your full resolution instead of the Xbox's smaller 3D image."});
    return r;
}

std::vector<Row> GameRows() {
    std::vector<Row> r;
    r.push_back({"SKIP ANY CUTSCENE", [] { return std::string(g_skip_cutscenes ? "ON" : "OFF"); },
                 [](int) {
                     g_skip_cutscenes = !g_skip_cutscenes;
                     Saved(false);
                 },
                 "START skips every cutscene, even ones you have not seen yet."});
    r.push_back({"CUTSCENES AT 60 FPS", [] { return std::string(g_cutscene_60 ? "ON" : "OFF"); },
                 [](int) {
                     g_cutscene_60 = !g_cutscene_60;
                     Saved(false);
                 },
                 "In-game cutscenes run at 60 FPS like the stages. The Xbox ran them at 30. Applies from the next cutscene."});
    r.push_back({"KEYBOARD CONTROLS", [] { return std::string(keyboard::g_enabled ? "ON" : "OFF"); },
                 [](int) {
                     keyboard::g_enabled = !keyboard::g_enabled;
                     Saved(false);
                 },
                 "Play with the keyboard. Controllers always work."});
    r.push_back({"SHOW FPS", [] { return std::string(g_show_fps ? "ON" : "OFF"); },
                 [](int) {
                     g_show_fps = !g_show_fps;
                     Saved(false);
                 },
                 "Frame counter in the top left corner."});
    return r;
}

// ------------------------------------------------------------------------------------------------ look
const ImU32 kRed = IM_COL32(214, 32, 40, 255);
const ImU32 kWhite = IM_COL32(245, 245, 245, 255);
const ImU32 kGrey = IM_COL32(160, 160, 165, 255);
const ImU32 kDim = IM_COL32(100, 100, 106, 255);

ImVec2 Measure(ImFont* f, float size, const char* t) { return f->CalcTextSizeA(size, FLT_MAX, 0, t); }

void Arrow(ImDrawList* dl, ImVec2 c, float s, bool left, ImU32 col) {
    float d = left ? -1.0f : 1.0f;
    dl->AddTriangleFilled(ImVec2(c.x - d * s * 0.5f, c.y - s * 0.6f), ImVec2(c.x - d * s * 0.5f, c.y + s * 0.6f),
                          ImVec2(c.x + d * s * 0.5f, c.y), col);
}

const char* kTabs[] = {"DISPLAY", "GRAPHICS", "GAME", "CONTROLS"};
constexpr int kTabCount = 4;
int g_tab = 0, g_row = 0;

const char* kControls[][2] = {
    {"MOVE", "W A S D   (hold LEFT ALT to walk)"},
    {"CAMERA", "I J K L"},
    {"JUMP / CONFIRM", "SPACE"},
    {"ACTION / BACK", "LEFT SHIFT  /  BACKSPACE"},
    {"X  /  Y", "F  /  R"},
    {"BLACK  /  WHITE", "C  /  V"},
    {"TRIGGERS", "Q  /  E"},
    {"D-PAD", "ARROW KEYS"},
    {"START / PAUSE", "ENTER  /  ESCAPE"},
    {"BACK", "TAB"},
    {"OPTIONS MENU", "F1   (controller: BACK + START)"},
};

void Draw() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float u = H / 1080.0f;          // layout unit: 1 at 1080p
    float fs = H / g_font_px;       // the fonts were made for g_font_px
    ImFont* dflt = io.Fonts->Fonts[0];
    ImFont* title = g_title ? g_title : dflt;
    ImFont* body = g_body ? g_body : dflt;
    ImFont* small = g_small ? g_small : dflt;
    float t_title = g_title ? g_title->FontSize * fs : 64 * u;
    float t_body = g_body ? g_body->FontSize * fs : 30 * u;
    float t_small = g_small ? g_small->FontSize * fs : 22 * u;

    std::vector<Row> rows;
    if (g_tab == 0) rows = DisplayRows();
    else if (g_tab == 1) rows = GraphicsRows();
    else if (g_tab == 2) rows = GameRows();
    int nrows = int(rows.size());
    g_row = nrows ? std::clamp(g_row, 0, nrows - 1) : 0;

    // keys and pad
    std::vector<Nav> nav;
    {
        std::lock_guard<std::mutex> l(g_nav_lock);
        nav.swap(g_nav);
    }
    for (Nav n : nav) {
        if (n == Nav::PrevTab || n == Nav::NextTab) {
            g_tab = (g_tab + (n == Nav::NextTab ? 1 : kTabCount - 1)) % kTabCount;
            g_row = 0;
            break;  // the rows belong to the old tab
        }
        if (!nrows) continue;
        if (n == Nav::Up) g_row = (g_row + nrows - 1) % nrows;
        if (n == Nav::Down) g_row = (g_row + 1) % nrows;
        if (n == Nav::Left) rows[size_t(g_row)].change(-1);
        if (n == Nav::Right || n == Nav::Accept) rows[size_t(g_row)].change(+1);
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##options", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // backdrop: the game dimmed, a red glow from the left, the title band
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, 205));
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W * 0.5f, H), IM_COL32(130, 8, 14, 140), IM_COL32(130, 8, 14, 0),
                                IM_COL32(130, 8, 14, 0), IM_COL32(130, 8, 14, 140));
    float band_y = H * 0.08f, band_h = t_title * 1.25f;
    float left = W * 0.5f - 660 * u, right = W * 0.5f + 660 * u;
    dl->AddQuadFilled(ImVec2(0, band_y), ImVec2(left + 520 * u, band_y), ImVec2(left + 520 * u - band_h * 0.6f, band_y + band_h),
                      ImVec2(0, band_y + band_h), kRed);
    dl->AddText(title, t_title, ImVec2(left, band_y + (band_h - t_title) * 0.5f), kWhite, "OPTIONS");
    const char* sub = "SHADOW THE HEDGEHOG   |   NATIVE PC PORT";
    dl->AddText(small, t_small, ImVec2(right - Measure(small, t_small, sub).x, band_y + (band_h - t_small) * 0.5f),
                IM_COL32(255, 255, 255, 170), sub);

    // tabs
    float ty = band_y + band_h + 50 * u, tabx = left;
    for (int i = 0; i < kTabCount; i++) {
        ImVec2 sz = Measure(body, t_body, kTabs[i]);
        ImVec2 a(tabx - 16 * u, ty - 10 * u), b(tabx + sz.x + 16 * u, ty + sz.y + 10 * u);
        bool hot = ImGui::IsMouseHoveringRect(a, b);
        if (hot && ImGui::IsMouseClicked(0)) {
            g_tab = i;
            g_row = 0;
        }
        if (i == g_tab) {
            dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 22));
            dl->AddRectFilled(ImVec2(a.x, b.y - 5 * u), ImVec2(b.x, b.y), kRed);
        }
        dl->AddText(body, t_body, ImVec2(tabx, ty), i == g_tab ? kWhite : hot ? kGrey : kDim, kTabs[i]);
        tabx = b.x + 36 * u;
    }
    const char* tabhint = "Q / LB      E / RB";
    dl->AddText(small, t_small, ImVec2(right - Measure(small, t_small, tabhint).x, ty + (t_body - t_small) * 0.5f), kDim, tabhint);
    float line_y = ty + t_body + 24 * u;
    dl->AddLine(ImVec2(left - 16 * u, line_y), ImVec2(right, line_y), IM_COL32(255, 255, 255, 45), 2 * u);

    float ry = line_y + 36 * u;
    if (g_tab < 3) {
        float rh = t_body + 34 * u;
        for (int i = 0; i < nrows; i++) {
            Row& row = rows[size_t(i)];
            ImVec2 a(left - 16 * u, ry), b(right, ry + rh);
            bool over = ImGui::IsMouseHoveringRect(a, b);
            if (over && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) g_row = i;
            bool sel = i == g_row;
            if (sel) {
                dl->AddRectFilledMultiColor(a, b, IM_COL32(205, 26, 34, 220), IM_COL32(110, 10, 16, 110),
                                            IM_COL32(110, 10, 16, 110), IM_COL32(205, 26, 34, 220));
                dl->AddRectFilled(a, ImVec2(a.x + 7 * u, b.y), kWhite);
            } else {
                dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, (i & 1) ? 10 : 20));
            }
            float cy = ry + (rh - t_body) * 0.5f;
            dl->AddText(body, t_body, ImVec2(left + 20 * u, cy), sel ? kWhite : kGrey, row.label);
            // < value >
            std::string v = row.value();
            float vw = 500 * u, vx = right - vw - 24 * u;
            ImVec2 vs = Measure(body, t_body, v.c_str());
            dl->AddText(body, t_body, ImVec2(vx + (vw - vs.x) * 0.5f, cy), kWhite, v.c_str());
            float as = t_body * 0.55f, acy = ry + rh * 0.5f;
            ImVec2 la(vx + 12 * u, acy), ra(vx + vw - 12 * u, acy);
            bool on_left = io.MousePos.x < vx + vw * 0.5f;
            ImU32 ac = sel ? kWhite : kDim;
            Arrow(dl, la, as, true, ac);
            Arrow(dl, ra, as, false, ac);
            if (over && ImGui::IsMouseClicked(0)) {
                g_row = i;
                row.change(on_left && io.MousePos.x >= vx ? -1 : +1);
            }
            ry += rh + 10 * u;
        }
        // what the selected option does
        if (nrows) {
            float dy = ry + 30 * u;
            dl->AddRectFilled(ImVec2(left - 16 * u, dy), ImVec2(left - 9 * u, dy + t_small * 2.2f), kRed);
            dl->AddText(small, t_small, ImVec2(left + 10 * u, dy + t_small * 0.6f), IM_COL32(225, 225, 230, 255),
                        rows[size_t(g_row)].desc, nullptr, right - left - 30 * u);
        }
    } else {
        float rh2 = t_small + 22 * u;
        int n = int(sizeof kControls / sizeof kControls[0]);
        for (int i = 0; i < n; i++) {
            dl->AddRectFilled(ImVec2(left - 16 * u, ry), ImVec2(right, ry + rh2), IM_COL32(255, 255, 255, (i & 1) ? 10 : 20));
            float cy = ry + (rh2 - t_small) * 0.5f;
            dl->AddText(small, t_small, ImVec2(left + 20 * u, cy), kGrey, kControls[i][0]);
            dl->AddText(small, t_small, ImVec2(left + 440 * u, cy), kWhite, kControls[i][1]);
            ry += rh2 + 4 * u;
        }
        dl->AddText(small, t_small, ImVec2(left, ry + 20 * u), kDim,
                    "Xbox, PlayStation and Switch controllers work automatically.");
    }

    // button hints
    float fy = H - 90 * u;
    dl->AddLine(ImVec2(left - 16 * u, fy - 22 * u), ImVec2(right, fy - 22 * u), IM_COL32(255, 255, 255, 45), 2 * u);
    dl->AddText(small, t_small, ImVec2(left, fy), kGrey,
                "UP / DOWN   SELECT          LEFT / RIGHT   CHANGE          F1   CLOSE");
    const char* saved = "SAVED AUTOMATICALLY";
    dl->AddText(small, t_small, ImVec2(right - Measure(small, t_small, saved).x, fy), kDim, saved);

    ImGui::End();
    ImGui::PopStyleVar(2);
}

void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    app::Mode out = app::Output();
    g_font_px = float(std::max<uint32_t>(out.height, 480));
    float u = g_font_px / 1080.0f;
    char windir[MAX_PATH] = "C:\\Windows";
    GetWindowsDirectoryA(windir, MAX_PATH);
    auto font = [&](const char* name, float px) -> ImFont* {
        std::string path = std::string(windir) + "\\Fonts\\" + name;
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        return io.Fonts->AddFontFromFileTTF(path.c_str(), px, &cfg);
    };
    io.Fonts->AddFontDefault();  // stays the default font for anything else
    // Bahnschrift (Windows 10 and later) has the condensed, game-like look; Segoe UI Bold otherwise
    g_title = font("bahnschrift.ttf", 88 * u);
    if (!g_title) g_title = font("segoeuib.ttf", 78 * u);
    g_body = font("bahnschrift.ttf", 38 * u);
    if (!g_body) g_body = font("segoeuib.ttf", 34 * u);
    g_small = font("bahnschrift.ttf", 27 * u);
    if (!g_small) g_small = font("segoeui.ttf", 26 * u);
}

}  // namespace

void MenuWindow(void* hwnd) {
    g_hwnd = static_cast<HWND>(hwnd);
    LoadFonts();  // before the renderer builds ImGui's font atlas
}

bool MenuMessage(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    (void)hwnd;
    if (msg != WM_KEYDOWN) return false;
    bool repeat = (lp & (1 << 30)) != 0;
    if (wp == VK_F1) {
        if (!repeat) g_menu_open = !g_menu_open;
        *result = 0;
        return true;
    }
    if (!g_menu_open) return false;
    switch (wp) {
        case VK_UP: case 'W': Push(Nav::Up); break;
        case VK_DOWN: case 'S': Push(Nav::Down); break;
        case VK_LEFT: case 'A': Push(Nav::Left); break;
        case VK_RIGHT: case 'D': Push(Nav::Right); break;
        case 'Q': if (!repeat) Push(Nav::PrevTab); break;
        case 'E': case VK_TAB: if (!repeat) Push(Nav::NextTab); break;
        case VK_RETURN: case VK_SPACE: if (!repeat) Push(Nav::Accept); break;
        default: return false;
    }
    *result = 0;
    return true;
}

// the real controller (before keyboard keys are merged in): Back+Start toggles the menu; while it is open the d-pad,
// stick, A, triggers and Black/White drive it and Start closes it
void MenuPad(int port, const input::Pad& pad) {
    if (port != 0) return;
    static uint16_t prev = 0;
    static uint8_t prev_an[8] = {};
    static int16_t prev_lx = 0, prev_ly = 0;
    constexpr uint16_t kUp = 1, kDown = 2, kLeft = 4, kRight = 8, kStart = 16, kBack = 32;
    uint16_t b = pad.buttons, pressed = uint16_t(b & ~prev);
    auto edge = [&](int i) { return pad.analog[i] > 128 && prev_an[i] <= 128; };
    if ((b & kBack) && (pressed & kStart)) {
        g_menu_open = !g_menu_open;
    } else if (g_menu_open) {
        if (pressed & kUp) Push(Nav::Up);
        if (pressed & kDown) Push(Nav::Down);
        if (pressed & kLeft) Push(Nav::Left);
        if (pressed & kRight) Push(Nav::Right);
        if (pressed & kStart) g_menu_open = false;
        if (edge(0)) Push(Nav::Accept);
        if (edge(6) || edge(5)) Push(Nav::PrevTab);  // left trigger, White
        if (edge(7) || edge(4)) Push(Nav::NextTab);  // right trigger, Black
        constexpr int16_t kTilt = 20000;
        if (pad.ly > kTilt && prev_ly <= kTilt) Push(Nav::Up);
        if (pad.ly < -kTilt && prev_ly >= -kTilt) Push(Nav::Down);
        if (pad.lx < -kTilt && prev_lx >= -kTilt) Push(Nav::Left);
        if (pad.lx > kTilt && prev_lx <= kTilt) Push(Nav::Right);
    }
    prev = b;
    std::copy(pad.analog, pad.analog + 8, prev_an);
    prev_lx = pad.lx;
    prev_ly = pad.ly;
}

void MenuUi(double fps) {
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = g_menu_open.load();
    if (g_show_fps) {
        ImGui::SetNextWindowPos(ImVec2(12, 12));
        ImGui::SetNextWindowBgAlpha(0.5f);
        ImGui::Begin("##fps", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                          ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        if (g_small) ImGui::PushFont(g_small);
        ImGui::Text("%.0f FPS", fps);
        if (g_small) ImGui::PopFont();
        ImGui::End();
    }
    if (g_menu_open) Draw();
}

}  // namespace shadow
#else
namespace shadow {
void MenuWindow(void*) {}
bool MenuMessage(void*, uint32_t, uintptr_t, intptr_t, intptr_t*) { return false; }
void MenuPad(int, const input::Pad&) {}
void MenuUi(double) {}
}  // namespace shadow
#endif
