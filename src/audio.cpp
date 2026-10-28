#include "audio.h"

#include "store.h"
#include "util.h"

using Microsoft::WRL::ComPtr;

namespace {

// Called on a Core Audio worker thread: only posts to the UI thread.
class SessionNotification final : public IAudioSessionNotification {
public:
    SessionNotification(HWND wnd, UINT msg) : wnd_(wnd), msg_(msg) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioSessionNotification)) {
            *object = static_cast<IAudioSessionNotification*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG refs = InterlockedDecrement(&refs_);
        if (refs == 0) delete this;
        return refs;
    }
    IFACEMETHODIMP OnSessionCreated(IAudioSessionControl*) override {
        PostMessageW(wnd_, msg_, 0, 0);
        return S_OK;
    }

private:
    LONG refs_ = 1;
    HWND wnd_;
    UINT msg_;
};

std::wstring SessionExe(IAudioSessionControl2* session) {
    DWORD pid = 0;
    if (session->GetProcessId(&pid) != S_OK) return {};
    return ExePathOfProcess(pid);
}

void SetMute(IAudioSessionControl2* session, bool muted) {
    ComPtr<ISimpleAudioVolume> volume;
    if (SUCCEEDED(session->QueryInterface(IID_PPV_ARGS(&volume)))) volume->SetMute(muted, nullptr);
}

}  // namespace

bool AudioMuter::Init(HWND notifyWnd, UINT notifyMsg) {
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator_)))) return false;
    notification_.Attach(new SessionNotification(notifyWnd, notifyMsg));
    return true;
}

void AudioMuter::Shutdown() {
    for (auto& [id, manager] : endpoints_) manager->UnregisterSessionNotification(notification_.Get());
    endpoints_.clear();
    notification_.Reset();
    enumerator_.Reset();
}

void AudioMuter::RefreshEndpoints() {
    if (!enumerator_) return;
    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(enumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) return;
    UINT count = 0;
    devices->GetCount(&count);

    std::map<std::wstring, ComPtr<IAudioSessionManager2>> current;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        LPWSTR rawId = nullptr;
        if (FAILED(devices->Item(i, &device)) || FAILED(device->GetId(&rawId))) continue;
        const std::wstring id = rawId;
        CoTaskMemFree(rawId);

        if (const auto known = endpoints_.find(id); known != endpoints_.end()) {
            current.emplace(id, std::move(known->second));
            endpoints_.erase(known);
            continue;
        }
        ComPtr<IAudioSessionManager2> manager;
        if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                    reinterpret_cast<void**>(manager.GetAddressOf()))))
            continue;
        manager->RegisterSessionNotification(notification_.Get());
        current.emplace(id, std::move(manager));
    }
    for (auto& [id, manager] : endpoints_) manager->UnregisterSessionNotification(notification_.Get());
    endpoints_ = std::move(current);
}

template <class F>
void AudioMuter::ForEachSession(F&& visit) {
    for (auto& [id, manager] : endpoints_) {
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

void AudioMuter::Scan(const MuteStore& store) {
    RefreshEndpoints();
    std::set<std::wstring> present;
    ForEachSession([&](IAudioSessionControl2* session) {
        LPWSTR rawId = nullptr;
        if (FAILED(session->GetSessionInstanceIdentifier(&rawId))) return;
        std::wstring id = rawId;
        CoTaskMemFree(rawId);
        const bool known = seen_.contains(id);
        present.insert(std::move(id));
        if (known) return;
        if (const auto muted = store.Get(SessionExe(session))) SetMute(session, *muted);
    });
    seen_ = std::move(present);
}

void AudioMuter::Apply(const std::wstring& exe, bool muted) {
    RefreshEndpoints();
    ForEachSession([&](IAudioSessionControl2* session) {
        if (SessionExe(session) == exe) SetMute(session, muted);
    });
}
