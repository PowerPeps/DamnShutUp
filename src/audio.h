#pragma once

#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <map>
#include <set>
#include <string>

class MuteStore;

// Applies mute states to the audio sessions of every active render endpoint.
class AudioMuter {
public:
    // notifyMsg is posted to notifyWnd whenever a new audio session appears.
    bool Init(HWND notifyWnd, UINT notifyMsg);
    void Shutdown();
    // Applies stored states to sessions not seen by a previous scan.
    void Scan(const MuteStore& store);
    void Apply(const std::wstring& exe, bool muted);

private:
    void RefreshEndpoints();
    template <class F>
    void ForEachSession(F&& visit);

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<IAudioSessionNotification> notification_;
    std::map<std::wstring, Microsoft::WRL::ComPtr<IAudioSessionManager2>> endpoints_;  // by device id
    std::set<std::wstring> seen_;                                                       // session instance ids
};
