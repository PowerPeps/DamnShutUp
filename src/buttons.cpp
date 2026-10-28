#include "buttons.h"

#include <dwmapi.h>
#include <gdiplus.h>
#include <windowsx.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "audio.h"
#include "store.h"
#include "util.h"

namespace buttons {
namespace {

constexpr wchar_t kButtonClass[] = L"DamnShutUp.TitleBarButton";
constexpr wchar_t kVolumeGlyph = L'';
constexpr wchar_t kMuteGlyph = L'';
constexpr UINT kRecolorDelayMs = 150;
constexpr float kGlyphSizeAt96Dpi = 11.5f;

struct Button {
    HWND wnd = nullptr;
    std::wstring exe;
    RECT rect{};
    COLORREF caption = RGB(255, 255, 255);
    bool active = false;
    bool hover = false;
    bool pressed = false;
};

HINSTANCE g_instance = nullptr;
ULONG_PTR g_gdiplusToken = 0;
std::vector<HWINEVENTHOOK> g_hooks;
UINT_PTR g_recolorTimer = 0;
std::unordered_map<HWND, Button> g_buttons;  // by target window

bool IsTopmost(HWND wnd) { return (GetWindowLongPtrW(wnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0; }

int Luma(COLORREF color) { return (GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114) / 1000; }

// Screen rectangle left of the caption buttons; false when the window has no standard caption.
bool ComputeButtonRect(HWND target, RECT* out) {
    if (!IsWindowVisible(target) || IsIconic(target)) return false;
    const LONG_PTR style = GetWindowLongPtrW(target, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(target, GWL_EXSTYLE);
    if ((style & WS_CAPTION) != WS_CAPTION || !(style & WS_SYSMENU) || (style & WS_CHILD)) return false;
    if (exStyle & WS_EX_TOOLWINDOW) return false;

    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(target, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return false;

    RECT window;
    POINT client{0, 0};
    if (!GetWindowRect(target, &window) || !ClientToScreen(target, &client)) return false;
    // A client area reaching into the caption means the app draws its own title bar.
    if (client.y - window.top < GetSystemMetricsForDpi(SM_CYCAPTION, GetDpiForWindow(target))) return false;

    RECT captionButtons;
    if (FAILED(DwmGetWindowAttribute(target, DWMWA_CAPTION_BUTTON_BOUNDS, &captionButtons, sizeof(captionButtons))))
        return false;
    int count = 1;
    if (style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX))
        count += 2;
    else if (exStyle & WS_EX_CONTEXTHELP)
        count += 1;
    const int width = (captionButtons.right - captionButtons.left) / count;
    const int height = captionButtons.bottom - captionButtons.top;
    if (width <= 0 || height <= 0) return false;

    out->right = window.left + captionButtons.left;
    out->left = out->right - width;
    out->top = window.top + captionButtons.top;
    out->bottom = out->top + height;
    // Maximized windows hang their border off the monitor: keep only the visible part of the caption.
    RECT frame;
    if (SUCCEEDED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))) &&
        frame.top > out->top && frame.top < out->bottom)
        out->top = frame.top;
    return out->left >= client.x + 2 * width;
}

COLORREF SampleCaption(const RECT& button) {
    HDC screen = GetDC(nullptr);
    const COLORREF color = GetPixel(screen, button.left - 4, button.top + 4);
    ReleaseDC(nullptr, screen);
    return color == CLR_INVALID ? RGB(255, 255, 255) : color;
}

void Render(Button& button) {
    const int width = button.rect.right - button.rect.left;
    const int height = button.rect.bottom - button.rect.top;
    if (width <= 0 || height <= 0) return;

    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ previous = SelectObject(memory, dib);
    {
        Gdiplus::Bitmap canvas(width, height, width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Gdiplus::Graphics graphics(&canvas);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

        const BYTE ink = Luma(button.caption) < 128 ? 255 : 0;
        if (button.pressed || button.hover)
            graphics.Clear(Gdiplus::Color(button.pressed ? 0x36 : 0x1A, ink, ink, ink));
        else  // alpha 1 keeps the otherwise invisible background hit-testable
            graphics.Clear(Gdiplus::Color(1, GetRValue(button.caption), GetGValue(button.caption), GetBValue(button.caption)));

        Gdiplus::FontFamily family(L"Segoe MDL2 Assets");
        const float glyphSize = kGlyphSizeAt96Dpi * GetDpiForWindow(button.wnd) / 96.0f;
        Gdiplus::Font font(&family, glyphSize, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush brush(Gdiplus::Color(button.active ? 255 : 0x73, ink, ink, ink));
        Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        const wchar_t glyph = store::IsMuted(button.exe) ? kMuteGlyph : kVolumeGlyph;
        graphics.DrawString(&glyph, 1, &font,
                            Gdiplus::RectF(0, 0, static_cast<Gdiplus::REAL>(width), static_cast<Gdiplus::REAL>(height)),
                            &format, &brush);
    }
    POINT source{0, 0};
    POINT destination{button.rect.left, button.rect.top};
    SIZE size{width, height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(button.wnd, screen, &destination, &size, memory, &source, 0, &blend, ULW_ALPHA);

    SelectObject(memory, previous);
    DeleteObject(dib);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}

void Refresh(const std::wstring& exe) {
    for (auto& [target, button] : g_buttons)
        if (IsWindowVisible(button.wnd) && button.exe == exe) Render(button);
}

void Recolor() {
    const HWND foreground = GetForegroundWindow();
    for (auto& [target, button] : g_buttons) {
        if (!IsWindowVisible(button.wnd)) continue;
        const COLORREF caption = SampleCaption(button.rect);
        const bool active = foreground == target;
        if (caption == button.caption && active == button.active) continue;
        button.caption = caption;
        button.active = active;
        Render(button);
    }
}

void CALLBACK OnRecolorTimer(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr, id);
    g_recolorTimer = 0;
    Recolor();
}

void Place(HWND target, Button& button, const RECT& rect) {
    const bool active = GetForegroundWindow() == target;
    const bool moved = !EqualRect(&rect, &button.rect);
    const bool resized = rect.right - rect.left != button.rect.right - button.rect.left ||
                         rect.bottom - rect.top != button.rect.bottom - button.rect.top;
    const bool wasVisible = IsWindowVisible(button.wnd);
    button.rect = rect;
    if (!wasVisible || resized || active != button.active) {
        button.active = active;
        Render(button);
    }
    if (!wasVisible || moved) ScheduleRecolor();

    // Keep the overlay directly above its target in the Z order.
    const bool topmost = IsTopmost(target);
    if (topmost != IsTopmost(button.wnd))
        SetWindowPos(button.wnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    HWND above = GetWindow(target, GW_HWNDPREV);
    if (above == button.wnd && wasVisible && !moved) return;
    UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW;
    HWND after = above;
    if (above == button.wnd)
        flags |= SWP_NOZORDER;
    else if (!above || (!topmost && IsTopmost(above)))
        after = topmost ? HWND_TOPMOST : HWND_TOP;
    SetWindowPos(button.wnd, after, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, flags);
}

void Sync(HWND target) {
    DWORD pid = 0;
    if (!GetWindowThreadProcessId(target, &pid) || pid == GetCurrentProcessId()) return;
    auto it = g_buttons.find(target);
    RECT rect;
    if (!ComputeButtonRect(target, &rect)) {
        if (it != g_buttons.end()) ShowWindow(it->second.wnd, SW_HIDE);
        return;
    }
    if (it == g_buttons.end()) {
        Button button;
        button.exe = ExePathOfProcess(pid);
        if (button.exe.empty()) return;
        button.wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kButtonClass, L"", WS_POPUP, 0,
                                     0, 0, 0, nullptr, nullptr, g_instance, nullptr);
        if (!button.wnd) return;
        SetWindowLongPtrW(button.wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(target));
        it = g_buttons.emplace(target, std::move(button)).first;
    }
    Place(target, it->second, rect);
}

void SyncTracked() {
    std::vector<HWND> targets;
    targets.reserve(g_buttons.size());
    for (const auto& [target, button] : g_buttons) targets.push_back(target);
    for (HWND target : targets) Sync(target);
}

void Remove(HWND target) {
    const auto it = g_buttons.find(target);
    if (it == g_buttons.end()) return;
    DestroyWindow(it->second.wnd);
    g_buttons.erase(it);
}

void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    if (event == EVENT_SYSTEM_FOREGROUND || event == EVENT_OBJECT_REORDER) {
        if (event == EVENT_SYSTEM_FOREGROUND && hwnd) Sync(hwnd);
        SyncTracked();
        ScheduleRecolor();
        return;
    }
    if (!hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    if (event == EVENT_OBJECT_DESTROY)
        Remove(hwnd);
    else
        Sync(hwnd);
}

void Toggle(const std::wstring& exe) {
    const bool muted = !store::IsMuted(exe);
    store::Set(exe, muted);
    audio::Apply(exe, muted);
    Refresh(exe);
}

void OnMouse(HWND target, UINT msg, LPARAM lp) {
    const auto it = g_buttons.find(target);
    if (it == g_buttons.end()) return;
    Button& button = it->second;
    const POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    const RECT client{0, 0, button.rect.right - button.rect.left, button.rect.bottom - button.rect.top};
    bool hover = button.hover;
    bool pressed = button.pressed;
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!button.hover) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, button.wnd, 0};
            TrackMouseEvent(&track);
        }
        hover = PtInRect(&client, point) != FALSE;
        break;
    case WM_MOUSELEAVE:
        hover = false;
        break;
    case WM_LBUTTONDOWN:
        pressed = true;
        SetCapture(button.wnd);
        break;
    case WM_LBUTTONUP:
        if (!button.pressed) break;
        button.pressed = false;
        ReleaseCapture();
        if (PtInRect(&client, point)) Toggle(button.exe);
        Render(button);
        return;
    case WM_CAPTURECHANGED:
        pressed = false;
        break;
    }
    if (hover == button.hover && pressed == button.pressed) return;
    button.hover = hover;
    button.pressed = pressed;
    Render(button);
}

LRESULT CALLBACK ButtonProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DPICHANGED:
        return 0;
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        OnMouse(reinterpret_cast<HWND>(GetWindowLongPtrW(wnd, GWLP_USERDATA)), msg, lp);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

}  // namespace

void Init(HINSTANCE instance) {
    g_instance = instance;

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = ButtonProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kButtonClass;
    if (!RegisterClassExW(&wc)) return;

    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) return;

    const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    const std::pair<DWORD, DWORD> ranges[] = {
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_REORDER},
        {EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE},
        {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED},
    };
    for (const auto& [first, last] : ranges)
        if (HWINEVENTHOOK hook = SetWinEventHook(first, last, nullptr, OnWinEvent, 0, 0, flags)) g_hooks.push_back(hook);

    Resync();
}

void Shutdown() {
    for (HWINEVENTHOOK hook : g_hooks) UnhookWinEvent(hook);
    g_hooks.clear();
    if (g_recolorTimer) KillTimer(nullptr, g_recolorTimer);
    g_recolorTimer = 0;
    for (auto& [target, button] : g_buttons) DestroyWindow(button.wnd);
    g_buttons.clear();
    if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
    g_gdiplusToken = 0;
}

void Resync() {
    for (auto it = g_buttons.begin(); it != g_buttons.end();) {
        if (IsWindow(it->first)) {
            ++it;
            continue;
        }
        DestroyWindow(it->second.wnd);
        it = g_buttons.erase(it);
    }
    EnumWindows(
        [](HWND hwnd, LPARAM) -> BOOL {
            Sync(hwnd);
            return TRUE;
        },
        0);
}

void ScheduleRecolor() {
    g_recolorTimer = SetTimer(nullptr, g_recolorTimer, kRecolorDelayMs, OnRecolorTimer);
}

}  // namespace buttons
