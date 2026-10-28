#include <windows.h>
#include <shellapi.h>

#include "audio.h"
#include "buttons.h"

namespace {

constexpr wchar_t kMainClass[] = L"DamnShutUp.Main";
constexpr WORD kIconId = 1;  // ICON resource in app.rc
constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT WM_APP_AUDIO_SESSION = WM_APP + 2;
constexpr UINT_PTR kTimer = 1;
constexpr UINT kTimerIntervalMs = 1000;
constexpr UINT kTrayId = 1;
constexpr UINT kExitCommand = 1;

UINT g_taskbarCreated = 0;

void AddTrayIcon(HWND wnd) {
    NOTIFYICONDATAW data{sizeof(data)};
    data.hWnd = wnd;
    data.uID = kTrayId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = WM_APP_TRAY;
    data.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kIconId), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    wcscpy_s(data.szTip, L"DamnShutUp");
    Shell_NotifyIconW(NIM_ADD, &data);
}

void ShowTrayMenu(HWND wnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kExitCommand, L"Quitter");
    POINT cursor;
    GetCursorPos(&cursor);
    SetForegroundWindow(wnd);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, wnd, nullptr);
    PostMessageW(wnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (command == kExitCommand) DestroyWindow(wnd);
}

LRESULT CALLBACK MainProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        buttons::Resync();
        audio::Scan();
        return 0;
    case WM_APP_AUDIO_SESSION:
        audio::Scan();
        return 0;
    case WM_APP_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP) ShowTrayMenu(wnd);
        return 0;
    case WM_SETTINGCHANGE:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        buttons::ScheduleRecolor();
        return 0;
    case WM_DESTROY: {
        NOTIFYICONDATAW data{sizeof(data)};
        data.hWnd = wnd;
        data.uID = kTrayId;
        Shell_NotifyIconW(NIM_DELETE, &data);
        PostQuitMessage(0);
        return 0;
    }
    }
    if (msg == g_taskbarCreated && msg != 0) {
        AddTrayIcon(wnd);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\DamnShutUp");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    // Core Audio only delivers session notifications to processes with an MTA.
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(kIconId));
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);
    HWND wnd = CreateWindowExW(WS_EX_TOOLWINDOW, kMainClass, L"DamnShutUp", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                               instance, nullptr);
    if (!wnd) return 1;
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    audio::Init(wnd, WM_APP_AUDIO_SESSION);
    audio::Scan();
    buttons::Init(instance);
    AddTrayIcon(wnd);
    SetTimer(wnd, kTimer, kTimerIntervalMs, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    buttons::Shutdown();
    audio::Shutdown();
    CoUninitialize();
    CloseHandle(mutex);
    return 0;
}
