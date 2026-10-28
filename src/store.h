#pragma once

#include <optional>
#include <string>

// Mute state per executable path, persisted in %LOCALAPPDATA%\DamnShutUp\mute.txt.
namespace store {

void Load();
std::optional<bool> Get(const std::wstring& exe);
inline bool IsMuted(const std::wstring& exe) { return Get(exe).value_or(false); }
void Set(const std::wstring& exe, bool muted);

}  // namespace store
