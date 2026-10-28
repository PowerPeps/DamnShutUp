#include "store.h"

#include <windows.h>
#include <shlobj.h>

#include <fstream>
#include <map>

namespace store {
namespace {

std::wstring g_path;
std::map<std::wstring, bool> g_states;

std::string ToUtf8(const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring FromUtf8(const std::string& text) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

void Save() {
    if (g_path.empty()) return;
    const std::wstring temp = g_path + L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        for (const auto& [exe, muted] : g_states) out << (muted ? '1' : '0') << '\t' << ToUtf8(exe) << '\n';
        if (!out) return;
    }
    MoveFileExW(temp.c_str(), g_path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

}  // namespace

void Load() {
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) return;
    const std::wstring dir = std::wstring(localAppData) + L"\\DamnShutUp";
    CoTaskMemFree(localAppData);
    CreateDirectoryW(dir.c_str(), nullptr);
    g_path = dir + L"\\mute.txt";

    // One "<0|1>\t<exe path>" entry per line.
    std::ifstream in(g_path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 3 || line[1] != '\t' || (line[0] != '0' && line[0] != '1')) continue;
        g_states[FromUtf8(line.substr(2))] = line[0] == '1';
    }
}

std::optional<bool> Get(const std::wstring& exe) {
    const auto it = g_states.find(exe);
    if (it == g_states.end()) return std::nullopt;
    return it->second;
}

void Set(const std::wstring& exe, bool muted) {
    g_states[exe] = muted;
    Save();
}

}  // namespace store
