// ShadowNativePort setup: installs the port next to the player's own copy of the game.
//
// The release zip holds Setup.exe and a "files" folder (the port, without any game code or game data). Setup copies
// "files" to the chosen folder, then the player's disc image into assets\ and, if given, the HD texture pack zip into
// textures\pack\. The game builds its code from the disc and imports the pack on its first start.
// Build: x86_64-w64-mingw32-clang++ -O2 -municode -mwindows -static setup.cpp -lole32 -luuid -lshell32 -lcomctl32
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kTitle[] = L"Shadow the Hedgehog - Native PC Port Setup";
enum Id { kDest = 100, kDestBrowse, kDisc, kDiscBrowse, kPack, kPackBrowse, kDesktop, kInstall, kStatus, kBar };

HWND g_wnd, g_dest, g_disc, g_pack, g_desktop, g_install, g_status, g_bar;
HFONT g_font;
std::atomic<bool> g_busy{false};

std::wstring Text(HWND h) {
    std::wstring s(size_t(GetWindowTextLengthW(h)), L'\0');
    GetWindowTextW(h, s.data(), int(s.size() + 1));
    return s;
}

fs::path ExeDir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return fs::path(p).parent_path();
}

std::wstring Pick(bool folder, const wchar_t* filter_name, const wchar_t* filter) {
    std::wstring out;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : 0));
    COMDLG_FILTERSPEC spec[2] = {{filter_name, filter}, {L"All files", L"*.*"}};
    if (!folder) dlg->SetFileTypes(2, spec);
    if (SUCCEEDED(dlg->Show(g_wnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) out = p, CoTaskMemFree(p);
            item->Release();
        }
    }
    dlg->Release();
    return out;
}

void Status(const std::wstring& s) { SetWindowTextW(g_status, s.c_str()); }

struct CopyState {
    uint64_t base = 0, total = 1;
};
CopyState g_copy;

DWORD CALLBACK CopyProgress(LARGE_INTEGER, LARGE_INTEGER done, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE,
                            LPVOID) {
    uint64_t pos = g_copy.base + uint64_t(done.QuadPart);
    SendMessageW(g_bar, PBM_SETPOS, WPARAM(pos * 1000 / g_copy.total), 0);
    return PROGRESS_CONTINUE;
}

bool CopyOne(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    bool ok = CopyFileExW(from.c_str(), to.c_str(), CopyProgress, nullptr, nullptr, 0);
    g_copy.base += fs::file_size(from, ec);
    return ok;
}

bool Shortcut(const fs::path& link, const fs::path& target) {
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return false;
    sl->SetPath(target.c_str());
    sl->SetWorkingDirectory(target.parent_path().c_str());
    sl->SetIconLocation(target.c_str(), 0);
    IPersistFile* pf = nullptr;
    bool ok = SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf))) && SUCCEEDED(pf->Save(link.c_str(), TRUE));
    if (pf) pf->Release();
    sl->Release();
    return ok;
}

fs::path Known(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) out = p;
    CoTaskMemFree(p);
    return out;
}

void Install(fs::path dest, fs::path disc, fs::path pack, bool desktop) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::error_code ec;
    fs::path files = ExeDir() / "files";
    auto fail = [&](const std::wstring& why) {
        Status(L"Not installed: " + why);
        MessageBoxW(g_wnd, why.c_str(), kTitle, MB_ICONERROR);
    };
    if (!fs::exists(files / "Shadow.exe", ec)) return fail(L"the \"files\" folder is missing beside Setup.exe. Extract the whole zip first.");
    // what gets copied, for the progress bar
    std::vector<std::pair<fs::path, fs::path>> jobs;
    for (auto it = fs::recursive_directory_iterator(files, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec)) jobs.push_back({it->path(), dest / fs::relative(it->path(), files, ec)});
    bool folder_disc = fs::is_directory(disc, ec);
    if (folder_disc) {
        for (auto it = fs::recursive_directory_iterator(disc, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            if (it->is_regular_file(ec)) jobs.push_back({it->path(), dest / "assets" / fs::relative(it->path(), disc, ec)});
    } else {
        jobs.push_back({disc, dest / "assets" / disc.filename()});
    }
    if (!pack.empty()) jobs.push_back({pack, dest / "textures" / "pack" / pack.filename()});
    g_copy = {0, 1};
    for (auto& j : jobs) g_copy.total += fs::file_size(j.first, ec);
    fs::space_info sp = fs::space(dest.root_path(), ec);
    if (!ec && sp.available < g_copy.total + (2ull << 30))
        return fail(L"there is not enough free space on that drive (the game needs about " +
                    std::to_wstring(g_copy.total / (1ull << 30) + 3) + L" GB).");
    for (auto& [from, to] : jobs) {
        Status(L"Copying " + to.filename().wstring() + L"...");
        if (!CopyOne(from, to)) return fail(L"copying " + from.filename().wstring() + L" failed (error " + std::to_wstring(GetLastError()) + L").");
    }
    for (const char* d : {"assets", "textures/pack", "hdd"}) fs::create_directories(dest / d, ec);
    fs::path exe = dest / "Shadow.exe";
    if (desktop) Shortcut(Known(FOLDERID_Desktop) / L"Shadow the Hedgehog.lnk", exe);
    Shortcut(Known(FOLDERID_Programs) / L"Shadow the Hedgehog.lnk", exe);
    SendMessageW(g_bar, PBM_SETPOS, 1000, 0);
    Status(L"Installed.");
    std::wstring done = L"Shadow the Hedgehog is installed in\n" + dest.wstring() +
                        L"\n\nThe first start builds the game code from your disc (a few minutes, once)"
                        + std::wstring(pack.empty() ? L"" : L" and installs the HD texture pack") +
                        L". The folder can be moved or renamed later.\n\nStart the game now?";
    if (MessageBoxW(g_wnd, done.c_str(), kTitle, MB_YESNO | MB_ICONINFORMATION) == IDYES)
        ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, dest.c_str(), SW_SHOWNORMAL);
    PostMessageW(g_wnd, WM_CLOSE, 0, 0);
}

HWND Make(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_wnd, HMENU(INT_PTR(id)),
                             GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, WPARAM(g_font), TRUE);
    return c;
}

void Build() {
    int y = 14;
    Make(L"STATIC",
         L"Installs the port. You need your own copy of the game as a disc image (.iso) made from your Xbox disc. "
         L"No game files come with this download.",
         0, 16, y, 560, 36, 0);
    y += 48;
    Make(L"STATIC", L"Install folder:", 0, 16, y, 560, 18, 0);
    g_dest = Make(L"EDIT", (L"C:\\Games\\Shadow the Hedgehog"), WS_BORDER | ES_AUTOHSCROLL, 16, y + 20, 470, 24, kDest);
    Make(L"BUTTON", L"Browse...", 0, 494, y + 20, 82, 24, kDestBrowse);
    y += 56;
    Make(L"STATIC", L"Your game disc image (.iso), or the folder holding default.xbe:", 0, 16, y, 560, 18, 0);
    g_disc = Make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 16, y + 20, 470, 24, kDisc);
    Make(L"BUTTON", L"Browse...", 0, 494, y + 20, 82, 24, kDiscBrowse);
    y += 56;
    Make(L"STATIC", L"Optional: the Shadow the Hedgehog HD texture pack (.zip):", 0, 16, y, 560, 18, 0);
    g_pack = Make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 16, y + 20, 470, 24, kPack);
    Make(L"BUTTON", L"Browse...", 0, 494, y + 20, 82, 24, kPackBrowse);
    y += 52;
    Make(L"STATIC", L"You can also drop the pack zip into textures\\pack later.", 0, 16, y, 560, 18, 0);
    y += 26;
    g_desktop = Make(L"BUTTON", L"Desktop shortcut", BS_AUTOCHECKBOX, 16, y, 200, 22, kDesktop);
    SendMessageW(g_desktop, BM_SETCHECK, BST_CHECKED, 0);
    g_install = Make(L"BUTTON", L"Install", BS_DEFPUSHBUTTON, 476, y - 2, 100, 30, kInstall);
    y += 40;
    g_bar = Make(PROGRESS_CLASSW, L"", 0, 16, y, 560, 16, kBar);
    SendMessageW(g_bar, PBM_SETRANGE32, 0, 1000);
    g_status = Make(L"STATIC", L"", 0, 16, y + 22, 560, 18, kStatus);
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case kDestBrowse: {
            std::wstring p = Pick(true, nullptr, nullptr);
            if (!p.empty()) SetWindowTextW(g_dest, (fs::path(p) / L"Shadow the Hedgehog").c_str());
            break;
        }
        case kDiscBrowse: {
            std::wstring p = Pick(false, L"Xbox disc image or default.xbe", L"*.iso;default.xbe");
            if (!p.empty()) {
                if (fs::path(p).filename() == L"default.xbe") p = fs::path(p).parent_path().wstring();
                SetWindowTextW(g_disc, p.c_str());
            }
            break;
        }
        case kPackBrowse: {
            std::wstring p = Pick(false, L"Texture pack", L"*.zip");
            if (!p.empty()) SetWindowTextW(g_pack, p.c_str());
            break;
        }
        case kInstall: {
            if (g_busy) break;
            std::error_code ec;
            fs::path dest = Text(g_dest), disc = Text(g_disc), pack = Text(g_pack);
            if (dest.empty() || !dest.is_absolute()) {
                MessageBoxW(h, L"Choose a full install folder path, for example C:\\Games\\Shadow the Hedgehog.", kTitle, MB_ICONWARNING);
                break;
            }
            std::wstring low = dest.wstring();
            for (auto& c : low) c = wchar_t(towlower(c));
            if (low.find(L"\\program files") != std::wstring::npos) {
                MessageBoxW(h, L"Program Files is write protected, and the game saves into its own folder. Choose another folder, for example C:\\Games.", kTitle, MB_ICONWARNING);
                break;
            }
            bool disc_ok = fs::is_regular_file(disc, ec) || fs::exists(disc / "default.xbe", ec);
            if (!disc_ok) {
                MessageBoxW(h, L"Choose your game disc image (.iso), or the folder that holds default.xbe.", kTitle, MB_ICONWARNING);
                break;
            }
            if (!pack.empty() && !fs::is_regular_file(pack, ec)) {
                MessageBoxW(h, L"The texture pack file was not found. Leave it empty to skip it.", kTitle, MB_ICONWARNING);
                break;
            }
            g_busy = true;
            EnableWindow(g_install, FALSE);
            bool desktop = SendMessageW(g_desktop, BM_GETCHECK, 0, 0) == BST_CHECKED;
            std::thread([=] {
                Install(dest, disc, pack, desktop);
                g_busy = false;
                EnableWindow(g_install, TRUE);
            }).detach();
            break;
        }
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode(HDC(wp), TRANSPARENT);
        return LRESULT(GetSysColorBrush(COLOR_WINDOW));
    case WM_CLOSE:
        if (g_busy && MessageBoxW(h, L"Setup is still copying. Quit anyway?", kTitle, MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);
    NONCLIENTMETRICSW ncm{sizeof(ncm)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = L"ShadowSetup";
    RegisterClassW(&wc);
    RECT rc{0, 0, 592, 380};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);
    g_wnd = CreateWindowW(wc.lpszClassName, kTitle, style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left,
                          rc.bottom - rc.top, nullptr, nullptr, inst, nullptr);
    Build();
    ShowWindow(g_wnd, show);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
