#include "audio.h"

#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <map>
#include <set>

#include "store.h"
#include "util.h"

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

namespace audio {
namespace {

// Called on a Core Audio worker thread: only posts to the UI thread.
class SessionNotification final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IAudioSessionNotification> {
public:
    SessionNotification(HWND wnd, UINT msg) : wnd_(wnd), msg_(msg) {}

    IFACEMETHODIMP OnSessionCreated(IAudioSessionControl*) override {
        PostMessageW(wnd_, msg_, 0, 0);
        return S_OK;
    }

private:
    HWND wnd_;
    UINT msg_;
};

using Endpoints = std::map<std::wstring, ComPtr<IAudioSessionManager2>>;  // by device id

ComPtr<IMMDeviceEnumerator> g_enumerator;
ComPtr<IAudioSessionNotification> g_notification;
Endpoints g_endpoints;
std::set<std::wstring> g_seen;  // session instance ids

std::wstring SessionExe(IAudioSessionControl2* session) {
    DWORD pid = 0;
    if (session->GetProcessId(&pid) != S_OK) return {};
    return ExePathOfProcess(pid);
}

void SetMute(IAudioSessionControl2* session, bool muted) {
    ComPtr<ISimpleAudioVolume> volume;
    if (SUCCEEDED(session->QueryInterface(IID_PPV_ARGS(&volume)))) volume->SetMute(muted, nullptr);
}

void Unregister(const Endpoints& endpoints) {
    for (const auto& [id, manager] : endpoints) manager->UnregisterSessionNotification(g_notification.Get());
}

void RefreshEndpoints() {
    if (!g_enumerator) return;
    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(g_enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) return;
    UINT count = 0;
    devices->GetCount(&count);

    Endpoints current;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        LPWSTR rawId = nullptr;
        if (FAILED(devices->Item(i, &device)) || FAILED(device->GetId(&rawId))) continue;
        const std::wstring id = rawId;
        CoTaskMemFree(rawId);

        if (const auto known = g_endpoints.find(id); known != g_endpoints.end()) {
            current.emplace(id, std::move(known->second));
            g_endpoints.erase(known);
            continue;
        }
        ComPtr<IAudioSessionManager2> manager;
        if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                    reinterpret_cast<void**>(manager.GetAddressOf()))))
            continue;
        manager->RegisterSessionNotification(g_notification.Get());
        current.emplace(id, std::move(manager));
    }
    Unregister(g_endpoints);
    g_endpoints = std::move(current);
}

template <class F>
void ForEachSession(F&& visit) {
    for (auto& [id, manager] : g_endpoints) {
        ComPtr<IAudioSessionEnumerator> sessions;
        if (FAILED(manager->GetSessionEnumerator(&sessions))) continue;
        int count = 0;
        sessions->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            ComPtr<IAudioSessionControl> control;
            ComPtr<IAudioSessionControl2> control2;
            if (FAILED(sessions->GetSession(i, &control)) || FAILED(control.As(&control2))) continue;
            visit(control2.Get());
        }
    }
}

}  // namespace

void Init(HWND notifyWnd, UINT notifyMsg) {
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&g_enumerator)))) return;
    g_notification = Microsoft::WRL::Make<SessionNotification>(notifyWnd, notifyMsg);
}

void Shutdown() {
    Unregister(g_endpoints);
    g_endpoints.clear();
    g_notification.Reset();
    g_enumerator.Reset();
}

void Scan() {
    RefreshEndpoints();
    std::set<std::wstring> present;
    ForEachSession([&](IAudioSessionControl2* session) {
        LPWSTR rawId = nullptr;
        if (FAILED(session->GetSessionInstanceIdentifier(&rawId))) return;
        std::wstring id = rawId;
        CoTaskMemFree(rawId);
        const bool known = g_seen.contains(id);
        present.insert(std::move(id));
        if (known) return;
        if (const auto muted = store::Get(SessionExe(session))) SetMute(session, *muted);
    });
    g_seen = std::move(present);
}

void Apply(const std::wstring& exe, bool muted) {
    RefreshEndpoints();
    ForEachSession([&](IAudioSessionControl2* session) {
        if (SessionExe(session) == exe) SetMute(session, muted);
    });
}

}  // namespace audio
