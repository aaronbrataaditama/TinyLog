#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: FileHelpers.cpp (WP-A).
#include <string>
#include <string_view>
#include "util/Status.h"

Status GetExePath(std::wstring& path);        // GetModuleFileNameW with growing buffer
Status GetExeDirectory(std::wstring& dir);    // directory of the EXE, no trailing backslash

// Spec 5.3 atomic replace: write <target>.tmp (same dir), WriteFile loop (check short writes),
// FlushFileBuffers, close, then ReplaceFileW if target exists else MoveFileExW(MOVEFILE_WRITE_THROUGH).
// On failure the target is left untouched and the .tmp is deleted.
Status WriteFileAtomic(const std::wstring& target, std::string_view bytes);

// Missing file -> failed Status with code == ERROR_FILE_NOT_FOUND (or ERROR_PATH_NOT_FOUND).
Status ReadWholeFile(const std::wstring& path, std::string& bytes);

bool   FileExists(const std::wstring& path);   // true for files only (not directories)
