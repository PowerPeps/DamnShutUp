#pragma once

#include <windows.h>

#include <string>

// Applies mute states to the audio sessions of every active render endpoint.
namespace audio {

// notifyMsg is posted to notifyWnd whenever a new audio session appears.
void Init(HWND notifyWnd, UINT notifyMsg);
void Shutdown();
// Applies stored states to sessions not seen by a previous scan.
void Scan();
void Apply(const std::wstring& exe, bool muted);

}  // namespace audio
