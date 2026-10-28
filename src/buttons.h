#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Layered overlay windows drawn left of the minimize button of every window using the standard caption.
class TitleBarButtons {
public:
    using IsMutedFn = std::function<bool(const std::wstring& exe)>;
    using ToggleFn = std::function<void(const std::wstring& exe)>;

    bool Init(HINSTANCE instance, IsMutedFn isMuted, ToggleFn toggle);
    void Shutdown();
    // Full pass over top-level windows, catches anything the event hooks missed.
    void Resync();
    void Refresh(const std::wstring& exe);
    void ScheduleRecolor();

private:
    struct Button {
        HWND wnd = nullptr;
        std::wstring exe;
        RECT rect{};
        COLORREF caption = RGB(255, 255, 255);
        bool visible = false;
        bool active = false;
        bool hover = false;
        bool pressed = false;
    };

    static void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    static void CALLBACK OnRecolorTimer(HWND, UINT, UINT_PTR, DWORD);
    static LRESULT CALLBACK ButtonProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);

    void Sync(HWND target);
    void SyncTracked();
    void Remove(HWND target);
    void Place(HWND target, Button& button, const RECT& rect);
    void Render(Button& button);
    void Recolor();
    void OnMouse(HWND target, UINT msg, LPARAM lp);

    HINSTANCE instance_ = nullptr;
    ULONG_PTR gdiplusToken_ = 0;
    std::vector<HWINEVENTHOOK> hooks_;
    UINT_PTR recolorTimer_ = 0;
    std::unordered_map<HWND, Button> buttons_;  // by target window
    IsMutedFn isMuted_;
    ToggleFn toggle_;
};
