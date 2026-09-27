#pragma once

#include <Windows.h>

#include <string>

namespace metaphor {

// Returns "<directory of the running EXE>\<filename>". Keeps the mod's log and dump files next to
// the game executable regardless of the working directory.
inline std::wstring ExeRelativePath(const wchar_t* filename) {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    size_t slash = path.find_last_of(L"\\/");
    std::wstring dir = (slash == std::wstring::npos) ? L"." : path.substr(0, slash);
    return dir + L"\\" + filename;
}

}  // namespace metaphor
