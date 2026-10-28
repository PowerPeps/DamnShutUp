#pragma once

#include <windows.h>

#include <string>

// Lowercase full image path, empty when the process cannot be queried.
std::wstring ExePathOfProcess(DWORD pid);
