#include "store.h"

#include <windows.h>
#include <shlobj.h>

#include <fstream>

namespace {

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

}  // namespace

void MuteStore::Load() {
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) return;
    const std::wstring dir = std::wstring(localAppData) + L"\\DamnShutUp";
    CoTaskMemFree(localAppData);
    CreateDirectoryW(dir.c_str(), nullptr);
    path_ = dir + L"\\mute.txt";

    // One "<0|1>\t<exe path>" entry per line.
    std::ifstream in(path_, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 3 || line[1] != '\t' || (line[0] != '0' && line[0] != '1')) continue;
        states_[FromUtf8(line.substr(2))] = line[0] == '1';
    }
}

std::optional<bool> MuteStore::Get(const std::wstring& exe) const {
    const auto it = states_.find(exe);
    if (it == states_.end()) return std::nullopt;
    return it->second;
}

void MuteStore::Set(const std::wstring& exe, bool muted) {
    states_[exe] = muted;
    Save();
}

void MuteStore::Save() const {
    if (path_.empty()) return;
    const std::wstring temp = path_ + L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        for (const auto& [exe, muted] : states_) out << (muted ? '1' : '0') << '\t' << ToUtf8(exe) << '\n';
        if (!out) return;
    }
    MoveFileExW(temp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING);
}
