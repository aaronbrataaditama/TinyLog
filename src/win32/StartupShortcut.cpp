// Spec 6. Startup-folder shortcut via Shell COM (no Run key, no scheduled task).
#include "win32/StartupShortcut.h"

#include <knownfolders.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include "util/ComHelpers.h"
#include "util/Handles.h"
#include "util/FileHelpers.h"

using Microsoft::WRL::ComPtr;

namespace startup {
namespace {

constexpr wchar_t kShortcutName[] = L"TinyLog.lnk";

Status GetShortcutPath(std::wstring& lnk) {
    CoTaskMemString folder;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_Startup, KF_FLAG_DEFAULT, nullptr, folder.put());
    if (FAILED(hr) || !folder.get()) return Status::FromHr(FAILED(hr) ? hr : E_FAIL, L"Resolve Startup folder");
    lnk = folder.get();
    if (!lnk.empty() && lnk.back() != L'\\') lnk += L'\\';
    lnk += kShortcutName;
    return Status::Ok();
}

bool FullPath(const std::wstring& in, std::wstring& out) {
    DWORD cap = MAX_PATH;
    for (int i = 0; i < 4; ++i) {
        out.assign(cap, L'\0');
        const DWORD n = GetFullPathNameW(in.c_str(), cap, out.data(), nullptr);
        if (n == 0) return false;
        if (n < cap) {
            out.resize(n);
            return true;
        }
        cap = n + 1;
    }
    return false;
}

// Opens a file or directory with no access rights, just to query its identity. Returns invalid if it fails.
UniqueHandle OpenForQuery(const std::wstring& path) {
    return UniqueHandle(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
}

bool FinalPath(HANDLE h, std::wstring& out) {
    DWORD cap = MAX_PATH;
    for (int i = 0; i < 4; ++i) {
        out.assign(cap, L'\0');
        const DWORD n = GetFinalPathNameByHandleW(h, out.data(), cap, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (n == 0) return false;
        if (n < cap) {
            out.resize(n);
            return true;
        }
        cap = n + 1;
    }
    return false;
}

bool EqualNoCase(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool IsNotFound(DWORD e) { return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND; }

}  // namespace

Status CreateShortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& workDir) {
    ComPtr<IShellLinkW> link;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (FAILED(hr)) return Status::FromHr(hr, L"Create ShellLink");
    hr = link->SetPath(target.c_str());
    if (FAILED(hr)) return Status::FromHr(hr, L"ShellLink SetPath");
    hr = link->SetWorkingDirectory(workDir.c_str());
    if (FAILED(hr)) return Status::FromHr(hr, L"ShellLink SetWorkingDirectory");
    ComPtr<IPersistFile> file;
    hr = link.As(&file);
    if (FAILED(hr)) return Status::FromHr(hr, L"ShellLink IPersistFile");

    // Save to a sibling temp file and move it into place so a failure never leaves a partial
    // shortcut and never destroys an existing good one.
    const std::wstring tmp = lnk + L".tmp";
    hr = file->Save(tmp.c_str(), TRUE);
    if (FAILED(hr)) {
        DeleteFileW(tmp.c_str());
        return Status::FromHr(hr, L"Save shortcut");
    }
    if (!MoveFileExW(tmp.c_str(), lnk.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        Status s = Status::FromLastError(L"Move shortcut into place");
        DeleteFileW(tmp.c_str());
        return s;
    }
    return Status::Ok();
}

Status ReadShortcutTarget(const std::wstring& lnk, std::wstring& target) {
    target.clear();
    ComPtr<IShellLinkW> link;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (FAILED(hr)) return Status::FromHr(hr, L"Create ShellLink");
    ComPtr<IPersistFile> file;
    hr = link.As(&file);
    if (FAILED(hr)) return Status::FromHr(hr, L"ShellLink IPersistFile");
    hr = file->Load(lnk.c_str(), STGM_READ);
    if (FAILED(hr)) return Status::FromHr(hr, L"Load shortcut");

    constexpr int kBuf = 32768;
    std::wstring buf(kBuf, L'\0');
    hr = link->GetPath(buf.data(), kBuf, nullptr, SLGP_RAWPATH);
    if (FAILED(hr)) return Status::FromHr(hr, L"ShellLink GetPath");
    buf.resize(wcsnlen(buf.c_str(), kBuf));
    target = std::move(buf);
    return Status::Ok();
}

bool PathsEqual(const std::wstring& a, const std::wstring& b) {
    std::wstring fa, fb;
    if (!FullPath(a, fa) || !FullPath(b, fb)) return EqualNoCase(a, b);

    UniqueHandle ha = OpenForQuery(fa);
    UniqueHandle hb = OpenForQuery(fb);
    if (ha.valid() && hb.valid()) {
        std::wstring pa, pb;
        if (FinalPath(ha.get(), pa) && FinalPath(hb.get(), pb)) return EqualNoCase(pa, pb);
    }
    return EqualNoCase(fa, fb);
}

Status IsEnabled(bool& enabled) {
    enabled = false;
    std::wstring lnk;
    Status s = GetShortcutPath(lnk);
    if (!s) return s;

    const DWORD attr = GetFileAttributesW(lnk.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        const DWORD e = GetLastError();
        if (IsNotFound(e)) return Status::Ok();
        return Status::FromLastError(L"Check startup shortcut");
    }

    std::wstring target;
    s = ReadShortcutTarget(lnk, target);
    if (!s) return s;

    std::wstring exe;
    s = GetExePath(exe);
    if (!s) return s;

    if (PathsEqual(target, exe)) {
        enabled = true;
        return Status::Ok();
    }
    DeleteFileW(lnk.c_str());   // stale shortcut (EXE moved); errors ignored
    return Status::Ok();
}

Status Enable() {
    std::wstring lnk;
    Status s = GetShortcutPath(lnk);
    if (!s) return s;
    std::wstring exe, dir;
    s = GetExePath(exe);
    if (!s) return s;
    s = GetExeDirectory(dir);
    if (!s) return s;
    return CreateShortcut(lnk, exe, dir);
}

Status Disable() {
    std::wstring lnk;
    Status s = GetShortcutPath(lnk);
    if (!s) return s;
    if (!DeleteFileW(lnk.c_str())) {
        const DWORD e = GetLastError();
        if (IsNotFound(e)) return Status::Ok();
        return Status::FromLastError(L"Delete startup shortcut");
    }
    return Status::Ok();
}

}  // namespace startup
