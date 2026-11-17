#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: StartupShortcut.cpp (WP-E). Spec 6.
// Callers must have COM initialized (apartment-threaded) on the calling thread.
#include <string>
#include "util/Status.h"

namespace startup {

// Spec 6.3: true only if <Startup>\TinyLog.lnk resolves to the current EXE. A TinyLog.lnk pointing
// elsewhere is deleted (delete errors ignored) and enabled=false. Missing shortcut -> false, Ok.
Status IsEnabled(bool& enabled);
Status Enable();    // create/overwrite <Startup>\TinyLog.lnk -> current EXE, working dir = EXE dir
Status Disable();   // delete <Startup>\TinyLog.lnk; missing = Ok

// ---- testable core (no Startup folder involved) ----
// IShellLinkW SetPath/SetWorkingDirectory + IPersistFile::Save(lnk, TRUE). On failure no partial file remains.
Status CreateShortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& workDir);
// IPersistFile::Load + IShellLinkW::GetPath(SLGP_RAWPATH) with a 32K-char buffer.
Status ReadShortcutTarget(const std::wstring& lnk, std::wstring& target);
// GetFullPathNameW both, case-insensitive ordinal compare; if both exist, compare GetFinalPathNameByHandleW.
bool   PathsEqual(const std::wstring& a, const std::wstring& b);

}  // namespace startup
