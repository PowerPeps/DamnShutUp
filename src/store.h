#pragma once

#include <string>

// Muted executable paths, persisted as values of HKCU\Software\DamnShutUp.
namespace store {

bool IsMuted(const std::wstring& exe);
void SetMuted(const std::wstring& exe, bool muted);

}  // namespace store
