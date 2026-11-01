// HD texture pack import (Windows build only).
//
// The fan-made HD texture pack (Dolphin DDS files, shadow_the_hedgehog_hd.zip) cannot ship with the port. The player
// drops the zip, or its extracted folder, into textures\pack. On the next start it is turned into
// textures\replacement\pack_import.pak:
// - textures\pack_map.txt (made by tools/host/make_pack_map.py) names the pack image for each Xbox texture key.
// - Each image is scaled to at most 4x the Xbox texture and a max side (1024 for stages, 2048 for menus), given a
//   full mip chain, and encoded BC1 (opaque) or BC3.
// A stamp file remembers which zip or folder was imported, so this happens once. Removing the zip keeps the import;
// deleting textures\replacement\pack_import.pak and its .txt undoes it.

#include "common/pack_import.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "app/app.h"
#include "core/log.h"

namespace shadow {
namespace {

namespace fs = std::filesystem;

struct Entry {
    uint64_t key = 0;
    uint32_t xw = 0, xh = 0, max_side = 1024;
    std::string path;  // inside the pack, '/' separated
};

struct Rgba {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px;  // RGBA8
};

// ---- DDS in ----

uint32_t Rd32(const std::vector<uint8_t>& f, size_t at) {
    uint32_t v;
    std::memcpy(&v, f.data() + at, 4);
    return v;
}

void Color565(uint16_t c, uint8_t* o) {
    o[0] = uint8_t(((c >> 11) & 31) * 255 / 31);
    o[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
    o[2] = uint8_t((c & 31) * 255 / 31);
}

// one BC1 colour block into 16 RGBA texels; three colour mode only when the block asks for it
void DecodeColorBlock(const uint8_t* b, uint8_t out[16][4], bool bc1) {
    uint16_t c0 = uint16_t(b[0] | b[1] << 8), c1 = uint16_t(b[2] | b[3] << 8);
    uint8_t pal[4][4] = {};
    Color565(c0, pal[0]);
    Color565(c1, pal[1]);
    pal[0][3] = pal[1][3] = pal[2][3] = pal[3][3] = 255;
    if (c0 > c1 || !bc1) {
        for (int i = 0; i < 3; i++) {
            pal[2][i] = uint8_t((2 * pal[0][i] + pal[1][i]) / 3);
            pal[3][i] = uint8_t((pal[0][i] + 2 * pal[1][i]) / 3);
        }
    } else {
        for (int i = 0; i < 3; i++) pal[2][i] = uint8_t((pal[0][i] + pal[1][i]) / 2);
        pal[3][3] = 0;
    }
    uint32_t idx = Rd32(std::vector<uint8_t>(b + 4, b + 8), 0);
    for (int t = 0; t < 16; t++) std::memcpy(out[t], pal[(idx >> (2 * t)) & 3], 4);
}

void DecodeAlphaBc3(const uint8_t* b, uint8_t out[16][4]) {
    uint8_t a[8] = {b[0], b[1]};
    if (a[0] > a[1]) {
        for (int i = 1; i < 7; i++) a[i + 1] = uint8_t(((7 - i) * a[0] + i * a[1]) / 7);
    } else {
        for (int i = 1; i < 5; i++) a[i + 1] = uint8_t(((5 - i) * a[0] + i * a[1]) / 5);
        a[6] = 0;
        a[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= uint64_t(b[2 + i]) << (8 * i);
    for (int t = 0; t < 16; t++) out[t][3] = a[(bits >> (3 * t)) & 7];
}

bool ReadDds(const fs::path& path, Rgba& img) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> f{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (f.size() < 128 || std::memcmp(f.data(), "DDS ", 4) != 0) return false;
    img.h = Rd32(f, 12);
    img.w = Rd32(f, 16);
    if (!img.w || !img.h || img.w > 8192 || img.h > 8192) return false;
    uint32_t pf_flags = Rd32(f, 80), fourcc = Rd32(f, 84), bits = Rd32(f, 88);
    img.px.assign(size_t(img.w) * img.h * 4, 255);
    size_t at = 128;
    if (pf_flags & 4) {  // FourCC
        int kind = fourcc == 0x31545844 ? 1 : fourcc == 0x33545844 ? 2 : fourcc == 0x35545844 ? 3 : 0;
        if (!kind) return false;
        size_t block = kind == 1 ? 8 : 16;
        uint32_t bw = (img.w + 3) / 4, bh = (img.h + 3) / 4;
        if (f.size() < at + block * bw * bh) return false;
        for (uint32_t by = 0; by < bh; by++)
            for (uint32_t bx = 0; bx < bw; bx++) {
                const uint8_t* b = f.data() + at + block * (by * bw + bx);
                uint8_t t[16][4];
                DecodeColorBlock(kind == 1 ? b : b + 8, t, kind == 1);
                if (kind == 3) DecodeAlphaBc3(b, t);
                if (kind == 2)
                    for (int i = 0; i < 16; i++) t[i][3] = uint8_t(((b[i / 2] >> (4 * (i & 1))) & 15) * 17);
                for (int i = 0; i < 16; i++) {
                    uint32_t x = bx * 4 + i % 4, y = by * 4 + i / 4;
                    if (x < img.w && y < img.h) std::memcpy(&img.px[(size_t(y) * img.w + x) * 4], t[i], 4);
                }
            }
        return true;
    }
    if (bits != 32 || f.size() < at + size_t(img.w) * img.h * 4) return false;
    uint32_t rmask = Rd32(f, 92), amask = Rd32(f, 104);
    bool bgra = rmask == 0x00FF0000;
    for (size_t i = 0; i < size_t(img.w) * img.h; i++) {
        const uint8_t* s = f.data() + at + i * 4;
        uint8_t* d = &img.px[i * 4];
        d[0] = bgra ? s[2] : s[0];
        d[1] = s[1];
        d[2] = bgra ? s[0] : s[2];
        d[3] = amask ? s[3] : 255;
    }
    return true;
}

// ---- scaling ----

// area average (box filter) to w x h, alpha weighted so clear texels do not darken edges
Rgba Resize(const Rgba& s, uint32_t w, uint32_t h) {
    if (s.w == w && s.h == h) return s;
    Rgba d{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
    double sx = double(s.w) / w, sy = double(s.h) / h;
    for (uint32_t y = 0; y < h; y++) {
        double y0 = y * sy, y1 = y0 + sy;
        for (uint32_t x = 0; x < w; x++) {
            double x0 = x * sx, x1 = x0 + sx;
            double acc[4] = {}, wsum = 0;
            for (uint32_t yy = uint32_t(y0); yy < std::min<double>(std::ceil(y1), s.h); yy++) {
                double wy = std::min<double>(yy + 1, y1) - std::max<double>(yy, y0);
                for (uint32_t xx = uint32_t(x0); xx < std::min<double>(std::ceil(x1), s.w); xx++) {
                    double wt = wy * (std::min<double>(xx + 1, x1) - std::max<double>(xx, x0));
                    const uint8_t* p = &s.px[(size_t(yy) * s.w + xx) * 4];
                    double a = p[3] / 255.0 * wt;
                    acc[0] += p[0] * a;
                    acc[1] += p[1] * a;
                    acc[2] += p[2] * a;
                    acc[3] += a;
                    wsum += wt;
                }
            }
            uint8_t* o = &d.px[(size_t(y) * w + x) * 4];
            for (int c = 0; c < 3; c++) o[c] = uint8_t(acc[3] > 0 ? std::clamp(acc[c] / acc[3] + 0.5, 0.0, 255.0) : 0);
            o[3] = uint8_t(wsum > 0 ? std::clamp(acc[3] / wsum * 255 + 0.5, 0.0, 255.0) : 0);
        }
    }
    return d;
}

// ---- BC encoding ----

uint16_t To565(const double* c) {
    auto q = [](double v, int m) { return uint16_t(std::clamp(int(v / 255.0 * m + 0.5), 0, m)); };
    return uint16_t(q(c[0], 31) << 11 | q(c[1], 63) << 5 | q(c[2], 31));
}

// colour endpoints along the block's principal axis, inset a little; four colour mode
void EncodeColorBlock(const uint8_t t[16][4], uint8_t* out) {
    double mean[3] = {};
    for (int i = 0; i < 16; i++)
        for (int c = 0; c < 3; c++) mean[c] += t[i][c] / 16.0;
    double cov[6] = {};
    for (int i = 0; i < 16; i++) {
        double r = t[i][0] - mean[0], g = t[i][1] - mean[1], b = t[i][2] - mean[2];
        cov[0] += r * r, cov[1] += r * g, cov[2] += r * b, cov[3] += g * g, cov[4] += g * b, cov[5] += b * b;
    }
    double axis[3] = {1, 1, 1};
    for (int it = 0; it < 8; it++) {  // power iteration
        double n[3] = {cov[0] * axis[0] + cov[1] * axis[1] + cov[2] * axis[2],
                       cov[1] * axis[0] + cov[3] * axis[1] + cov[4] * axis[2],
                       cov[2] * axis[0] + cov[4] * axis[1] + cov[5] * axis[2]};
        double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len < 1e-9) break;
        for (int c = 0; c < 3; c++) axis[c] = n[c] / len;
    }
    double lo = 1e9, hi = -1e9;
    for (int i = 0; i < 16; i++) {
        double p = (t[i][0] - mean[0]) * axis[0] + (t[i][1] - mean[1]) * axis[1] + (t[i][2] - mean[2]) * axis[2];
        lo = std::min(lo, p), hi = std::max(hi, p);
    }
    double inset = (hi - lo) / 32.0;
    lo += inset, hi -= inset;
    double e0[3], e1[3];
    for (int c = 0; c < 3; c++) e0[c] = std::clamp(mean[c] + axis[c] * hi, 0.0, 255.0), e1[c] = std::clamp(mean[c] + axis[c] * lo, 0.0, 255.0);
    uint16_t c0 = To565(e0), c1 = To565(e1);
    if (c0 < c1) std::swap(c0, c1);
    uint8_t pal[4][4];
    Color565(c0, pal[0]);
    Color565(c1, pal[1]);
    for (int c = 0; c < 3; c++) {
        pal[2][c] = uint8_t((2 * pal[0][c] + pal[1][c]) / 3);
        pal[3][c] = uint8_t((pal[0][c] + 2 * pal[1][c]) / 3);
    }
    uint32_t idx = 0;
    if (c0 != c1)
        for (int i = 0; i < 16; i++) {
            int best = 0, bd = INT32_MAX;
            for (int k = 0; k < 4; k++) {
                int d = 0;
                for (int c = 0; c < 3; c++) d += (t[i][c] - pal[k][c]) * (t[i][c] - pal[k][c]);
                if (d < bd) bd = d, best = k;
            }
            idx |= uint32_t(best) << (2 * i);
        }
    out[0] = uint8_t(c0), out[1] = uint8_t(c0 >> 8), out[2] = uint8_t(c1), out[3] = uint8_t(c1 >> 8);
    std::memcpy(out + 4, &idx, 4);
}

void EncodeAlphaBlock(const uint8_t t[16][4], uint8_t* out) {
    int lo = 255, hi = 0;
    for (int i = 0; i < 16; i++) lo = std::min<int>(lo, t[i][3]), hi = std::max<int>(hi, t[i][3]);
    out[0] = uint8_t(hi), out[1] = uint8_t(lo);
    uint8_t a[8] = {uint8_t(hi), uint8_t(lo)};
    for (int i = 1; i < 7; i++) a[i + 1] = uint8_t(((7 - i) * hi + i * lo) / 7);
    uint64_t bits = 0;
    if (hi != lo)
        for (int i = 0; i < 16; i++) {
            int best = 0, bd = 1 << 30;
            for (int k = 0; k < 8; k++) {
                int d = std::abs(int(t[i][3]) - a[k]);
                if (d < bd) bd = d, best = k;
            }
            bits |= uint64_t(best) << (3 * i);
        }
    for (int i = 0; i < 6; i++) out[2 + i] = uint8_t(bits >> (8 * i));
}

void EncodeLevel(const Rgba& img, bool alpha, std::vector<uint8_t>& out) {
    uint32_t bw = (img.w + 3) / 4, bh = (img.h + 3) / 4;
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++) {
            uint8_t t[16][4];
            for (int i = 0; i < 16; i++) {
                uint32_t x = std::min(bx * 4 + i % 4, img.w - 1), y = std::min(by * 4 + i / 4, img.h - 1);
                std::memcpy(t[i], &img.px[(size_t(y) * img.w + x) * 4], 4);
            }
            uint8_t b[16];
            if (alpha) {
                EncodeAlphaBlock(t, b);
                EncodeColorBlock(t, b + 8);
            } else {
                EncodeColorBlock(t, b);
            }
            out.insert(out.end(), b, b + (alpha ? 16 : 8));
        }
}

void Put32(std::vector<uint8_t>& v, size_t at, uint32_t x) { std::memcpy(v.data() + at, &x, 4); }

// the image as a DDS: scaled for the Xbox texture, full mip chain, BC1 or BC3
std::vector<uint8_t> MakeDds(const Rgba& src, const Entry& e) {
    double s = std::min({1.0, 4.0 * e.xw / src.w, 4.0 * e.xh / src.h,
                         double(e.max_side) / std::max(src.w, src.h)});
    uint32_t w = std::max(1u, uint32_t(src.w * s + 0.5)), h = std::max(1u, uint32_t(src.h * s + 0.5));
    Rgba lvl = Resize(src, w, h);
    bool alpha = false;
    for (size_t i = 3; i < lvl.px.size() && !alpha; i += 4) alpha = lvl.px[i] < 250;
    std::vector<uint8_t> dds(128), body;
    uint32_t levels = 0;
    for (;;) {
        EncodeLevel(lvl, alpha, body);
        levels++;
        if (lvl.w == 1 && lvl.h == 1) break;
        lvl = Resize(lvl, std::max(1u, lvl.w / 2), std::max(1u, lvl.h / 2));
    }
    std::memcpy(dds.data(), "DDS ", 4);
    Put32(dds, 4, 124);
    Put32(dds, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000);
    Put32(dds, 12, h);
    Put32(dds, 16, w);
    Put32(dds, 20, ((w + 3) / 4) * ((h + 3) / 4) * (alpha ? 16 : 8));
    Put32(dds, 28, levels);
    Put32(dds, 76, 32);
    Put32(dds, 80, 0x4);
    std::memcpy(dds.data() + 84, alpha ? "DXT5" : "DXT1", 4);
    Put32(dds, 108, 0x1000 | (levels > 1 ? 0x8 | 0x400000 : 0));
    dds.insert(dds.end(), body.begin(), body.end());
    return dds;
}

// ---- the import ----

std::vector<Entry> ReadMap(const fs::path& path) {
    std::vector<Entry> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        Entry e;
        std::string key, group;
        if (!(ls >> key >> e.xw >> e.xh >> e.max_side >> group)) continue;
        std::getline(ls, e.path);
        e.path.erase(0, e.path.find_first_not_of(' '));
        while (!e.path.empty() && (e.path.back() == '\r' || e.path.back() == ' ')) e.path.pop_back();
        e.key = std::stoull(key, nullptr, 16);
        if (e.xw && e.xh && !e.path.empty()) out.push_back(std::move(e));
    }
    return out;
}

// the folder the map's paths start from: the pack's GUP folder, wherever the zip put it
fs::path FindBase(const fs::path& dir, const std::vector<Entry>& map) {
    std::error_code ec;
    auto has = [&](const fs::path& base) { return fs::exists(base / fs::path(std::u8string(map[0].path.begin(), map[0].path.end())), ec); };
    std::vector<fs::path> tries{dir, dir / "GUP"};
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.is_directory(ec)) tries.push_back(e.path()), tries.push_back(e.path() / "GUP");
    for (auto& t : tries)
        if (has(t)) return t;
    return {};
}

struct Progress {
    std::atomic<uint32_t> done{0};
    uint32_t total = 0;
    std::atomic<bool> extracting{false}, finished{false};
};
Progress g_progress;

LRESULT CALLBACK ProgressProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER) {
        InvalidateRect(hwnd, nullptr, TRUE);
        if (g_progress.finished) DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(227, 232, 219));
        std::wstring text = L"Installing the HD texture pack (only once)\n\n";
        if (g_progress.extracting) {
            text += L"Unpacking the zip...";
        } else {
            text += std::to_wstring(g_progress.done.load()) + L" / " + std::to_wstring(g_progress.total) + L" textures";
        }
        RECT box = rc;
        InflateRect(&box, -20, -20);
        DrawTextW(dc, text.c_str(), -1, &box, DT_CENTER | DT_WORDBREAK);
        uint32_t total = std::max(1u, g_progress.total);
        RECT bar{20, rc.bottom - 40, 20 + LONG((rc.right - 40) * uint64_t(g_progress.done) / total), rc.bottom - 28};
        HBRUSH b = CreateSolidBrush(RGB(145, 158, 130));
        FillRect(dc, &bar, b);
        DeleteObject(b);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// runs work on a thread while a small window shows the progress
template <class F>
void WithProgressWindow(F work) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = ProgressProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ShadowPackImport";
    wc.hCursor = LoadCursorW(nullptr, IDC_WAIT);
    RegisterClassW(&wc);
    int w = 460, h = 150;
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Shadow the Hedgehog", WS_POPUP | WS_BORDER | WS_VISIBLE,
                                (GetSystemMetrics(SM_CXSCREEN) - w) / 2, (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h,
                                nullptr, nullptr, wc.hInstance, nullptr);
    std::thread worker([&] {
        work();
        g_progress.finished = true;
    });
    if (hwnd) {
        SetTimer(hwnd, 1, 200, nullptr);
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    worker.join();
}

bool Extract(const fs::path& zip, const fs::path& to) {
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    fs::path tar = fs::path(sys) / L"tar.exe";
    std::error_code ec;
    if (!fs::exists(tar, ec)) return false;
    fs::create_directories(to, ec);
    std::wstring cmd = L"\"" + tar.wstring() + L"\" -xf \"" + zip.wstring() + L"\" -C \"" + to.wstring() + L"\"";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

std::string U8(const fs::path& p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string Stamp(const fs::path& p) {
    std::error_code ec;
    auto t = fs::last_write_time(p, ec).time_since_epoch().count();
    uintmax_t size = fs::is_regular_file(p, ec) ? fs::file_size(p, ec) : 0;
    return U8(p.filename()) + " " + std::to_string(size) + " " + std::to_string((long long)t);
}

}  // namespace

void ImportTexturePack() {
    std::error_code ec;
    fs::path root = app::ExeDir() / "textures";
    fs::path drop = root / "pack", out_dir = root / "replacement";
    fs::create_directories(drop, ec);
    std::vector<Entry> map = ReadMap(root / "pack_map.txt");
    if (map.empty()) return;

    // the zip (the biggest), else a folder the player extracted it to
    fs::path source;
    uintmax_t best = 0;
    for (auto& e : fs::directory_iterator(drop, ec)) {
        std::wstring ext = e.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ext == L".zip" && e.is_regular_file(ec) && e.file_size(ec) > best) best = e.file_size(ec), source = e.path();
    }
    bool zip = !source.empty();
    if (!zip) {
        for (auto& e : fs::directory_iterator(drop, ec))
            if (e.is_directory(ec) && e.path().filename() != "_unpacked" && !FindBase(e.path(), map).empty()) source = e.path();
        if (source.empty() && !FindBase(drop, map).empty()) source = drop;
    }
    if (source.empty()) return;
    std::string stamp = Stamp(source);
    fs::path stamp_file = out_dir / "pack_import.txt", pak = out_dir / "pack_import.pak";
    {
        std::ifstream s(stamp_file);
        std::string line;
        if (s && std::getline(s, line) && line == stamp && fs::exists(pak, ec)) return;
    }
    LOGR("texture pack: importing %s", U8(source).c_str());
    fs::create_directories(out_dir, ec);

    fs::path unpacked = drop / "_unpacked";
    bool ok = true;
    std::wstring failure;
    WithProgressWindow([&] {
        fs::path dir = source;
        if (zip) {
            g_progress.extracting = true;
            fs::remove_all(unpacked, ec);
            ok = Extract(source, unpacked);
            g_progress.extracting = false;
            dir = unpacked;
            if (!ok) {
                failure = L"The texture pack zip could not be unpacked. Extract it into the textures\\pack folder "
                          L"yourself, delete the zip, then start the game again.";
                return;
            }
        }
        fs::path base = FindBase(dir, map);
        if (base.empty()) {
            ok = false;
            failure = L"The texture pack in textures\\pack is not the Shadow the Hedgehog HD pack this port knows.";
            return;
        }
        // one encode per pack image, shared by every key that uses it
        std::map<std::string, size_t> by_path;
        std::vector<const Entry*> unique;
        for (const Entry& e : map)
            if (by_path.emplace(e.path, unique.size()).second) unique.push_back(&e);
        std::vector<std::vector<uint8_t>> blobs(unique.size());
        g_progress.total = uint32_t(unique.size());
        std::atomic<size_t> next{0};
        std::vector<std::thread> pool;
        unsigned n = std::max(1u, std::thread::hardware_concurrency() - (std::thread::hardware_concurrency() > 2));
        for (unsigned t = 0; t < n; t++)
            pool.emplace_back([&] {
                for (size_t i; (i = next++) < unique.size();) {
                    Rgba img;
                    if (ReadDds(base / fs::path(std::u8string(unique[i]->path.begin(), unique[i]->path.end())), img)) blobs[i] = MakeDds(img, *unique[i]);
                    g_progress.done++;
                }
            });
        for (auto& t : pool) t.join();

        // "SNPK", u32 1, u32 count, count x {u64 key, u64 offset, u32 size}, then the DDS files
        std::vector<std::pair<uint64_t, size_t>> rows;
        for (const Entry& e : map) {
            size_t i = by_path[e.path];
            if (!blobs[i].empty()) rows.push_back({e.key, i});
        }
        std::vector<uint64_t> offset(blobs.size());
        uint64_t at = 12 + 20ull * rows.size();
        for (size_t i = 0; i < blobs.size(); i++) offset[i] = at, at += blobs[i].size();
        fs::path tmp = pak;
        tmp += ".tmp";
        std::ofstream f(tmp, std::ios::binary);
        uint32_t head[3] = {0x4B504E53, 1, uint32_t(rows.size())};
        f.write(reinterpret_cast<const char*>(head), 12);
        for (auto& [key, i] : rows) {
            uint32_t size = uint32_t(blobs[i].size());
            f.write(reinterpret_cast<const char*>(&key), 8);
            f.write(reinterpret_cast<const char*>(&offset[i]), 8);
            f.write(reinterpret_cast<const char*>(&size), 4);
        }
        for (auto& b : blobs) f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
        f.close();
        ok = bool(f);
        if (ok) {
            fs::rename(tmp, pak, ec);
            ok = !ec;
        }
        if (!ok) failure = L"Writing textures\\replacement\\pack_import.pak failed (is the disk full?).";
        LOGR("texture pack: %zu textures from %zu images", rows.size(), unique.size());
    });
    if (zip) fs::remove_all(unpacked, ec);
    if (!ok) {
        LOGE("texture pack: import failed");
        MessageBoxW(nullptr, failure.c_str(), L"Shadow the Hedgehog", MB_ICONWARNING);
        return;
    }
    std::ofstream(stamp_file) << stamp << "\n";
}

}  // namespace shadow
#else
namespace shadow {
void ImportTexturePack() {}  // headless harness
}  // namespace shadow
#endif
