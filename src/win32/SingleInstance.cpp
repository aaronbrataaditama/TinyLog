// Spec 8.2. Named Local mutex + registered activation message.
#include "win32/SingleInstance.h"

bool SingleInstance::Acquire() {
    HANDLE h = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!h) {
        // Cannot tell whether another instance exists; do not block the user from running.
        return true;
    }
    const bool alreadyExists = (GetLastError() == ERROR_ALREADY_EXISTS);
    mutex_.reset(h);   // keep the handle for the lifetime of this object either way
    return !alreadyExists;
}

void SingleInstance::ActivateExisting() {
    HWND owner = FindWindowW(kOwnerClass, nullptr);
    if (!owner) return;
    const UINT msg = RegisterWindowMessageW(kActivateMsgName);
    if (!msg) return;
    // Let the first instance take the foreground (the foreground lock would otherwise refuse it).
    DWORD pid = 0;
    GetWindowThreadProcessId(owner, &pid);
    if (pid) AllowSetForegroundWindow(pid);
    PostMessageW(owner, msg, 0, 0);
}
