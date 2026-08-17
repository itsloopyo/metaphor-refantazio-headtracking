#pragma once

#include <Windows.h>

#include <string>

namespace metaphor {

// Returns "<directory of the running EXE>\<filename>". Keeps the mod's log, INI,
// and dump files next to the game executable regardless of the working
// directory. Overloaded on character type so wide and narrow callers (the INI
// reader takes a std::string path) share the same logic.
inline std::wstring ExeRelativePath(const wchar_t* filename) {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    size_t slash = path.find_last_of(L"\\/");
    std::wstring dir = (slash == std::wstring::npos) ? L"." : path.substr(0, slash);
    return dir + L"\\" + filename;
}

inline std::string ExeRelativePath(const char* filename) {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string path(buf);
    size_t slash = path.find_last_of("\\/");
    std::string dir = (slash == std::string::npos) ? "." : path.substr(0, slash);
    return dir + "\\" + filename;
}

}  // namespace metaphor
