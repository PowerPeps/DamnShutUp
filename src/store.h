#pragma once

#include <map>
#include <optional>
#include <string>

// Mute state per executable path, persisted in %LOCALAPPDATA%\DamnShutUp\mute.txt.
class MuteStore {
public:
    void Load();
    std::optional<bool> Get(const std::wstring& exe) const;
    bool IsMuted(const std::wstring& exe) const { return Get(exe).value_or(false); }
    void Set(const std::wstring& exe, bool muted);

private:
    void Save() const;

    std::wstring path_;
    std::map<std::wstring, bool> states_;
};
