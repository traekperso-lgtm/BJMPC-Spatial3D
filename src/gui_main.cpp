// Spatial3D — application graphique Windows (Dear ImGui + Direct3D 11)
#include "engine.h"
#include "renderer.h"
#include "ui.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <d3d11.h>

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

#include <chrono>
#include <future>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

using namespace s3d;

namespace {

const wchar_t* kClass = L"Spatial3DWnd";
const UINT WM_APP_TRAY = WM_APP + 1;
enum { IDM_SHOW = 100, IDM_TOGGLE, IDM_QUIT };

HWND g_hwnd = nullptr;
ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
UINT g_resizeW = 0, g_resizeH = 0;
float g_scale = 1.f;
bool g_fontRebuild = false;
UINT g_taskbarCreated = 0;
NOTIFYICONDATAW g_nid{};
bool g_trayBalloonShown = false;
bool g_quitting = false;

LiveEngine g_live;
UiModel g_m;
std::string g_liveStartError;
std::future<std::string> g_liveStart;     // démarrage en arrière-plan
bool g_pendingShow = false;

// ---------------------------------------------------------------------------------------------
std::string utf8(const std::wstring& w) { return toUtf8(w); }

float dpiScaleFor(HWND h) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn f = (Fn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    UINT dpi = 96;
    if (f && h) dpi = f(h);
    else { HDC dc = GetDC(nullptr); dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc); }
    return dpi / 96.f;
}

// ---------------------------------------------------------------------------------------------
//  Direct3D 11
// ---------------------------------------------------------------------------------------------
void createRT() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
    if (back) { g_dev->CreateRenderTargetView(back, nullptr, &g_rtv); back->Release(); }
}
void cleanupRT() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

bool createDevice(HWND h) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = h;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                               &sd, &g_swap, &g_dev, &got, &g_ctx);
    if (hr == DXGI_ERROR_UNSUPPORTED)   // pas de GPU compatible : rendu logiciel WARP
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, &g_swap, &g_dev, &got, &g_ctx);
    if (FAILED(hr)) return false;
    createRT();
    return true;
}

void cleanupDevice() {
    cleanupRT();
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
}

// ---------------------------------------------------------------------------------------------
//  Polices (Segoe UI + symboles), à l'échelle de l'écran
// ---------------------------------------------------------------------------------------------
void buildFonts(float s) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    wchar_t win[MAX_PATH]; GetWindowsDirectoryW(win, MAX_PATH);
    const std::string dir = utf8(win) + "\\Fonts\\";
    static const ImWchar ranges[] = { 0x0020, 0x024F, 0x2000, 0x206F, 0x2190, 0x21FF, 0 };
    static const ImWchar sym[] = { 0x2190, 0x21FF, 0x2580, 0x25FF, 0x2700, 0x27BF, 0 };
    auto add = [&](const char* file, float px) {
        ImFont* f = nullptr;
        const std::string p = dir + file;
        if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) f = io.Fonts->AddFontFromFileTTF(p.c_str(), px * s, nullptr, ranges);
        if (!f) { ImFontConfig c; c.SizePixels = px * s; return io.Fonts->AddFontDefault(&c); }
        const std::string ps = dir + "seguisym.ttf";
        if (GetFileAttributesA(ps.c_str()) != INVALID_FILE_ATTRIBUTES) {
            ImFontConfig m; m.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(ps.c_str(), px * s, &m, sym);
        }
        return f;
    };
    add("segoeui.ttf", 17);     // texte
    add("segoeuib.ttf", 28);    // titre
    add("segoeuib.ttf", 18);    // intertitres
    add("segoeui.ttf", 14);     // petit
    uiSetupStyle(s);
}

// ---------------------------------------------------------------------------------------------
//  Zone de notification
// ---------------------------------------------------------------------------------------------
void trayAdd() {
    g_nid = {};
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wcscpy_s(g_nid.szTip, L"Spatial3D");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void trayTip(const wchar_t* tip) {
    if (wcscmp(g_nid.szTip, tip) == 0) return;
    wcscpy_s(g_nid.szTip, tip);
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void trayBalloon(const wchar_t* text) {
    g_nid.uFlags = NIF_INFO;
    wcscpy_s(g_nid.szInfoTitle, L"Spatial3D");
    wcscpy_s(g_nid.szInfo, text);
    g_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void showWindow() {
    ShowWindow(g_hwnd, IsIconic(g_hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_hwnd);
}

// ---------------------------------------------------------------------------------------------
//  Actions
// ---------------------------------------------------------------------------------------------
void refreshDevices();

void startLiveAsync() {
    if (g_liveStart.valid()) return;
    g_liveStartError.clear();
    g_m.liveScene = Scene{};
    const Settings s = g_m.settings;
    g_liveStart = std::async(std::launch::async, [s] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::string err;
        g_live.start(s, err);
        CoUninitialize();
        return err;
    });
}

void stopLive() {
    if (g_liveStart.valid()) { g_liveStartError = g_liveStart.get(); }
    // la marge apprise devient le point de départ (moins de pops au prochain démarrage)
    const auto st = g_live.status();
    if (st.running && (int)st.marginMs > g_m.settings.marginMs) {
        g_m.settings.marginMs = (int)st.marginMs;
        saveSettings(g_m.settings);
    }
    g_live.stop();
}

std::wstring fileDialog(const wchar_t* filter, const wchar_t* title) {
    wchar_t buf[MAX_PATH * 2] = {};
    OPENFILENAMEW of{};
    of.lStructSize = sizeof of;
    of.hwndOwner = g_hwnd;
    of.lpstrFilter = filter;
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH * 2;
    of.lpstrTitle = title;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetOpenFileNameW(&of) ? std::wstring(buf) : std::wstring();
}

void applyActions(const UiActions& a) {
    if (a.gainChanged) g_live.setGainDb(g_m.settings.gainDb);
    if (a.speakersMoved && g_live.running() && g_live.moveSpeakers(g_m.liveScene.out)) g_m.liveScene = g_live.scene();
    if (a.settingsChanged) { saveSettings(g_m.settings); refreshDevices(); }
    if (a.startWithWindowsChanged) setStartWithWindows(g_m.settings.startWithWindows, L"--tray");
    if (a.toggleLive) {
        if (g_live.running()) stopLive(); else startLiveAsync();
    }
    if (a.needRestart) {
        if (g_live.running()) { stopLive(); startLiveAsync(); }
    }
    if (a.browseSofa) {
        const std::wstring f = fileDialog(L"HRTF SOFA\0*.sofa\0", L"Choisir une HRTF (.sofa)");
        if (!f.empty()) {
            g_m.settings.sofaPath = utf8(f);
            saveSettings(g_m.settings);
            UiActions r; r.needRestart = true; applyActions(r);
        }
    }
    if (a.restoreSound) {
        stopLive();
        std::string err;
        const std::wstring id = !g_m.settings.outputId.empty() ? g_m.settings.outputId : loadSavedOutput();
        if (!id.empty()) setDefaultEndpoint(id, err);
    }
    if (a.openVbCableSite) ShellExecuteW(nullptr, L"open", L"https://vb-audio.com/Cable/", nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------------------------------------
//  Modèle affiché
// ---------------------------------------------------------------------------------------------
void refreshDevices() {
    std::vector<Endpoint> v; std::string err;
    listEndpoints(false, v, err);
    g_m.outputs.clear();
    g_m.vbCableInstalled = false;
    for (auto& e : v) {
        if (e.isVbCable()) { g_m.vbCableInstalled = true; continue; }
        UiDevice d;
        d.name = e.name; d.id = e.id; d.headphones = e.headphones;
        d.layoutName = e.headphones ? "casque" : layoutForEndpoint(e).name;
        g_m.outputs.push_back(d);
    }
    Endpoint cur;
    if (resolveOutput(g_m.settings, cur, err)) {
        g_m.outputSel = -1;
        for (int i = 0; i < (int)g_m.outputs.size(); ++i) if (g_m.outputs[i].id == cur.id) g_m.outputSel = i;
        g_m.previewLayout = layoutFor(g_m.settings, cur);
    }
}

void updateModel() {
    // fin d'un démarrage en arrière-plan
    if (g_liveStart.valid() && g_liveStart.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        g_liveStartError = g_liveStart.get();
        refreshDevices();
    }
    g_m.liveStarting = g_liveStart.valid();
    if (g_m.liveStarting) return;          // le moteur se reconstruit dans un autre fil
    const auto st = g_live.status();
    // le moteur s'est arrêté tout seul (carte débranchée…) : on rend la sortie normale à Windows
    static bool wasRunning = false;
    if (wasRunning && !st.running) g_live.stop();
    wasRunning = st.running;
    g_m.liveRunning = st.running;
    g_m.liveIdle = st.idle;
    g_m.marginMs = st.marginMs; g_m.lowMs = st.lowMs; g_m.driftPpm = st.driftPpm; g_m.inHz = st.inHz;
    g_m.underruns = st.underruns;
    g_m.drops = st.drops;
    g_m.captureGlitches = st.captureGlitches;
    g_m.liveOutputMode = st.outputMode;
    g_m.liveCaptureMode = st.captureMode;
    g_m.followVolume = st.followVolume; g_m.windowsMuted = st.windowsMuted; g_m.windowsVolumeDb = st.windowsVolumeDb;
    g_m.capturePeriodMs = st.capturePeriodMs; g_m.processingMs = st.processingMs;
    g_m.outputBufferMs = st.outputBufferMs; g_m.totalLatencyMs = st.totalLatencyMs;
    g_m.liveError = !g_liveStartError.empty() ? g_liveStartError : st.error;
    g_m.liveWarning = st.running ? st.warning : std::string();
    if (st.running && g_m.liveScene.out.speakers.empty()) g_m.liveScene = g_live.scene();
    if (!st.running) g_m.liveScene = Scene{};
    for (int i = 0; i < 32; ++i) { g_m.liveIn[i] = g_live.meters.in[i].exchange(0.f); g_m.liveOut[i] = g_live.meters.out[i].exchange(0.f); }

    trayTip(g_m.liveRunning ? L"Spatial3D — son 3D actif" : L"Spatial3D");
}


// ---------------------------------------------------------------------------------------------
LRESULT WINAPI wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp)) return true;
    if (msg == g_taskbarCreated && g_taskbarCreated) { trayAdd(); return 0; }
    switch (msg) {
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) return 0;
        g_resizeW = LOWORD(lp); g_resizeH = HIWORD(lp);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = (LONG)(980 * g_scale); mm->ptMinTrackSize.y = (LONG)(680 * g_scale);
        return 0;
    }
    case WM_DPICHANGED: {
        g_scale = HIWORD(wp) / 96.f;
        g_fontRebuild = true;
        const RECT* r = (const RECT*)lp;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_CLOSE:
        if (!g_quitting && g_m.settings.closeToTray) {
            ShowWindow(h, SW_HIDE);
            if (!g_trayBalloonShown) {
                trayBalloon(L"Spatial3D continue dans la zone de notification. Clic droit sur l'icône pour quitter.");
                g_trayBalloonShown = true;
            }
            return 0;
        }
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_COPYDATA: {   // une deuxième instance demande l'affichage de la fenêtre
        auto* cd = (COPYDATASTRUCT*)lp;
        if (cd->dwData == 1) {
            g_pendingShow = true;
        }
        return TRUE;
    }
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: showWindow(); break;
        case WM_RBUTTONUP: case WM_CONTEXTMENU: {
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, IDM_SHOW, L"Ouvrir Spatial3D");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, IDM_TOGGLE, g_live.running() ? L"Arrêter le son 3D" : L"Activer le son 3D");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, IDM_QUIT, L"Quitter");
            POINT pt; GetCursorPos(&pt);
            SetForegroundWindow(h);
            TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr);
            DestroyMenu(m);
            break;
        }
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_SHOW: showWindow(); break;
        case IDM_TOGGLE: { UiActions a; a.toggleLive = true; applyActions(a); break; }
        case IDM_QUIT: g_quitting = true; DestroyWindow(h); break;
        }
        return 0;
    case WM_ENDSESSION:
        if (wp) g_live.stop();   // fermeture de session : rendre la sortie normale
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;
        break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

// =============================================================================================
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int nShow) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool tray = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--configurer-cable") { CoInitializeEx(nullptr, COINIT_MULTITHREADED); return configureCable(); }
        if (a == L"--tray") tray = true;
    }
    LocalFree(argv);

    // Instance unique : on réveille la fenêtre existante
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Spatial3D_Instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kClass, nullptr)) {
            COPYDATASTRUCT cd{ 1, 0, nullptr };
            SendMessageW(other, WM_COPYDATA, 0, (LPARAM)&cd);
        }
        return 0;
    }

    using SetCtx = BOOL(WINAPI*)(HANDLE);
    if (auto f = (SetCtx)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))
        f((HANDLE)-4);   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof wc;
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));   // IDC_ARROW, indépendant de UNICODE
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    g_scale = dpiScaleFor(nullptr);
    g_hwnd = CreateWindowExW(0, kClass, L"Spatial3D", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             (int)(1240 * g_scale), (int)(800 * g_scale), nullptr, nullptr, inst, nullptr);
    g_scale = dpiScaleFor(g_hwnd);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);

    if (!createDevice(g_hwnd)) {
        MessageBoxW(nullptr, L"Direct3D 11 indisponible : impossible d'afficher l'interface.", L"Spatial3D", MB_ICONERROR);
        return 1;
    }
    trayAdd();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    buildFonts(g_scale);
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    g_m.settings = loadSettings();
    g_m.sofaSupported = sofaSupported();
    refreshDevices();
    if (!tray) { ShowWindow(g_hwnd, nShow); UpdateWindow(g_hwnd); }
    if (g_m.settings.liveOnLaunch) startLiveAsync();

    auto lastRefresh = std::chrono::steady_clock::now();
    auto lastInput = lastRefresh, lastFrame = lastRefresh;
    bool done = false;
    while (!done) {
        const bool visible = IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd);
        if (!visible) MsgWaitForMultipleObjects(0, nullptr, FALSE, 250, QS_ALLINPUT);
        else {
            // Interface économe : pleine fluidité pendant l'interaction, puis ~30 i/s (vumètres)
            // ou ~10 i/s à l'arrêt, pour laisser le processeur au fil audio. Toute entrée réveille.
            const auto t = std::chrono::steady_clock::now();
            if (t - lastInput > std::chrono::seconds(1)) {
                const auto period = std::chrono::milliseconds(g_m.liveRunning ? 33 : 100);
                const auto wait = period - std::chrono::duration_cast<std::chrono::milliseconds>(t - lastFrame);
                if (wait.count() > 0) MsgWaitForMultipleObjects(0, nullptr, FALSE, (DWORD)wait.count(), QS_ALLINPUT);
            }
            lastFrame = std::chrono::steady_clock::now();
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if ((msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) ||
                (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) || msg.message == WM_SIZE)
                lastInput = std::chrono::steady_clock::now();
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_pendingShow) { showWindow(); g_pendingShow = false; }

        const auto now = std::chrono::steady_clock::now();
        if (visible && now - lastRefresh > std::chrono::seconds(3)) { refreshDevices(); lastRefresh = now; }
        updateModel();
        if (!visible) continue;

        if (g_resizeW) {
            cleanupRT();
            g_swap->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            createRT();
        }
        if (g_fontRebuild) {
            buildFonts(g_scale);
            ImGui_ImplDX11_InvalidateDeviceObjects();
            g_fontRebuild = false;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        UiActions act;
        uiDraw(g_m, act, g_scale);
        ImGui::Render();
        const float clear[4] = { 16 / 255.f, 18 / 255.f, 24 / 255.f, 1 };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (g_swap->Present(1, 0) == DXGI_STATUS_OCCLUDED) Sleep(10);
        applyActions(act);
    }

    // Sortie propre : la vraie carte son redevient la sortie par défaut
    stopLive();
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanupDevice();
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}
