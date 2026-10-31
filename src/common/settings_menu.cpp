// In-game settings menu (F1): display, resolution, graphics and game options.
// Drawn with Dear ImGui over the game (GameInfo::ui). Every change is saved to settings.toml at once; display
// changes go through app::ApplyDisplay (window thread), the render scale is picked up by the renderer each frame.

#include "common/settings_menu.h"

#include "common/keyboard.h"
#include "common/render_ext.h"

namespace shadow {
extern bool g_skip_cutscenes;  // src/game/hooks.cpp
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
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "app/app.h"

namespace shadow {
namespace {

HWND g_hwnd = nullptr;

struct Res {
    uint32_t w, h;
};

// the resolutions the game window's monitor offers, largest first, at least 640x480
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
    for (auto it = seen.rbegin(); it != seen.rend(); ++it) out.push_back({it->first, it->second});
    return out;
}

std::string Label(uint32_t w, uint32_t h) {
    char b[48];
    // aspect name for the common ones
    const char* a = "";
    if (w * 9 == h * 16) a = "  16:9";
    else if (w * 10 == h * 16) a = "  16:10";
    else if (w * 3 == h * 4) a = "  4:3";
    else if (w * 9 == h * 21 || (w * 1000 / h) >= 2330) a = "  21:9";
    std::snprintf(b, sizeof b, "%u x %u%s", w, h, a);
    return b;
}

void Saved(bool display) {
    app::Save();
    if (display) app::ApplyDisplay();
}

void Heading(const char* text) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.30f, 0.25f, 1.0f));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::Separator();
}

void Style() {
    static bool done = false;
    if (done) return;
    done = true;
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 6;
    s.FrameRounding = 4;
    s.GrabRounding = 4;
    s.WindowPadding = ImVec2(18, 14);
    s.ItemSpacing = ImVec2(10, 8);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.07f, 0.94f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.45f, 0.06f, 0.06f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.18f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.30f, 0.10f, 0.10f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.45f, 0.08f, 0.08f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.65f, 0.12f, 0.12f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.45f, 0.08f, 0.08f, 0.8f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.65f, 0.12f, 0.12f, 0.9f);
    c[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.30f, 0.25f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.85f, 0.20f, 0.18f, 1.0f);
}

void Menu() {
    app::Settings& s = app::Get();
    ImGuiIO& io = ImGui::GetIO();
    float ui = std::max(1.0f, io.DisplaySize.y / 1080.0f);  // readable at 1440p and 4K too
    io.FontGlobalScale = ui * 1.25f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620 * ui, 0), ImGuiCond_Always);
    bool open = true;
    if (!ImGui::Begin("Settings  -  Shadow the Hedgehog", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                                                     ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    const float label_w = 230 * ui;

    // ---------------------------------------------------------------- display
    Heading("DISPLAY");
    {
        static const char* kModes[] = {"Fullscreen", "Borderless window", "Window"};
        int m = int(s.display_mode);
        ImGui::TextUnformatted("Display mode");
        ImGui::SameLine(label_w);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##mode", &m, kModes, 3)) {
            s.display_mode = app::DisplayMode(m);
            Saved(true);
        }
    }
    {
        static std::vector<Res> modes;
        static int refresh = 0;
        if (refresh++ % 120 == 0) modes = MonitorModes();  // a monitor change shows up within two seconds
        app::Mode desk = app::Desktop();
        std::string current = s.width && s.height ? Label(s.width, s.height) : "Desktop (" + Label(desk.width, desk.height) + ")";
        ImGui::TextUnformatted("Resolution");
        ImGui::SameLine(label_w);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##res", current.c_str())) {
            std::string dl = "Desktop (" + Label(desk.width, desk.height) + ")";
            if (ImGui::Selectable(dl.c_str(), !s.width)) {
                s.width = s.height = 0;
                Saved(true);
            }
            for (const Res& r : modes) {
                bool sel = s.width == r.w && s.height == r.h;
                if (ImGui::Selectable(Label(r.w, r.h).c_str(), sel)) {
                    s.width = r.w;
                    s.height = r.h;
                    Saved(true);
                }
            }
            ImGui::EndCombo();
        }
        if (s.display_mode == app::DisplayMode::Borderless && s.width)
            ImGui::TextDisabled("Borderless uses the desktop size; pick Fullscreen or Window for this resolution.");
    }
    {
        ImGui::TextUnformatted("V-Sync");
        ImGui::SameLine(label_w);
        if (ImGui::Checkbox("##vsync", &s.vsync)) Saved(false);
    }
    {
        static const char* kCaps[] = {"30 FPS", "60 FPS (original speed)"};
        static const uint32_t kCapValues[] = {30, 60};
        int c = s.fps_cap == 30 ? 0 : 1;
        ImGui::TextUnformatted("Frame rate");
        ImGui::SameLine(label_w);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##fps", &c, kCaps, 2)) {
            s.fps_cap = kCapValues[c];
            Saved(false);
        }
    }

    // ---------------------------------------------------------------- graphics
    Heading("GRAPHICS");
    {
        int pct = int(s.render_percent);
        ImGui::TextUnformatted("Render resolution");
        ImGui::SameLine(label_w);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##rp", &pct, 50, 200, "%d%%")) {
            s.render_percent = uint32_t(std::clamp(pct / 5 * 5, 50, 200));
            Saved(false);
        }
        float sc = app::RenderScale(480);
        float scene = sc * (g_hd_scene ? 1.25f : 1.0f);
        app::Mode out = app::Output();
        ImGui::SameLine(0, 0);
        ImGui::NewLine();
        ImGui::SetCursorPosX(label_w);
        ImGui::TextDisabled("Output %u x %u.  Menus %u x %u, 3D world %u x %u.", out.width, out.height, unsigned(640 * sc + 0.5f),
                            unsigned(480 * sc + 0.5f), unsigned(512 * scene + 0.5f), unsigned(512 * scene + 0.5f));
        ImGui::SetCursorPosX(label_w);
        ImGui::TextDisabled("100%% = sharp at your output. Above 100%% supersamples (smoother, slower).");
    }
    {
        static const char* kAniso[] = {"Off (as the game asks)", "2x", "4x", "8x", "16x"};
        static const uint32_t kAnisoValues[] = {0, 2, 4, 8, 16};
        int a = 0;
        for (int i = 0; i < 5; i++)
            if (kAnisoValues[i] == s.aniso) a = i;
        ImGui::TextUnformatted("Texture filtering");
        ImGui::SameLine(label_w);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##af", &a, kAniso, 5)) {
            s.aniso = kAnisoValues[a];
            Saved(false);
        }
    }
    {
        ImGui::TextUnformatted("HD 3D world");
        ImGui::SameLine(label_w);
        if (ImGui::Checkbox("##hd", &g_hd_scene)) Saved(false);
        ImGui::SameLine();
        ImGui::TextDisabled("renders stages at full output resolution");
    }

    // ---------------------------------------------------------------- game
    Heading("GAME");
    {
        ImGui::TextUnformatted("Skip any cutscene");
        ImGui::SameLine(label_w);
        if (ImGui::Checkbox("##skip", &g_skip_cutscenes)) Saved(false);
        ImGui::SameLine();
        ImGui::TextDisabled("START skips, even unseen ones");
    }
    {
        ImGui::TextUnformatted("Keyboard controls");
        ImGui::SameLine(label_w);
        if (ImGui::Checkbox("##kb", &keyboard::g_enabled)) Saved(false);
        ImGui::SameLine();
        ImGui::TextDisabled("layout in CONTROLS.txt");
    }
    {
        ImGui::TextUnformatted("Show FPS");
        ImGui::SameLine(label_w);
        if (ImGui::Checkbox("##fpsshow", &g_show_fps)) Saved(false);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(-1, 0))) open = false;
    ImGui::TextDisabled("F1 opens and closes this menu. Settings are saved as you change them.");
    ImGui::End();
    if (!open) g_menu_open = false;
}

}  // namespace

void MenuWindow(void* hwnd) { g_hwnd = static_cast<HWND>(hwnd); }

bool MenuMessage(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    (void)hwnd;
    if (msg == WM_KEYDOWN && wp == VK_F1 && !(lp & (1 << 30))) {  // not on auto-repeat
        g_menu_open = !g_menu_open;
        *result = 0;
        return true;
    }
    return false;
}

void MenuUi(double fps) {
    Style();
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = g_menu_open.load();
    if (g_show_fps) {
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::Begin("##fps", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                          ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::Text("%.0f FPS", fps);
        ImGui::End();
    }
    if (g_menu_open) Menu();
}

}  // namespace shadow
#else
namespace shadow {
void MenuWindow(void*) {}
bool MenuMessage(void*, uint32_t, uintptr_t, intptr_t, intptr_t*) { return false; }
void MenuUi(double) {}
}  // namespace shadow
#endif
