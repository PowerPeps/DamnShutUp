#include "store.h"

#include <windows.h>

namespace store {
namespace {

constexpr wchar_t kKey[] = L"Software\\DamnShutUp";

}  // namespace

bool IsMuted(const std::wstring& exe) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kKey, exe.c_str(), RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS &&
           value != 0;
}

void SetMuted(const std::wstring& exe, bool muted) {
    if (!muted) {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kKey, exe.c_str());
        return;
    }
    const DWORD value = 1;
    RegSetKeyValueW(HKEY_CURRENT_USER, kKey, exe.c_str(), REG_DWORD, &value, sizeof(value));
}

}  // namespace store
