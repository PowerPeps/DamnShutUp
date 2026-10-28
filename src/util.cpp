#include "util.h"

std::wstring ExePathOfProcess(DWORD pid) {
    if (pid == 0) return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t buffer[MAX_PATH * 2];
    DWORD length = ARRAYSIZE(buffer);
    std::wstring path;
    if (QueryFullProcessImageNameW(process, 0, buffer, &length)) path.assign(buffer, length);
    CloseHandle(process);
    if (!path.empty()) CharLowerBuffW(path.data(), static_cast<DWORD>(path.size()));
    return path;
}
