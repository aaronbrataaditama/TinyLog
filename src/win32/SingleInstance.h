#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: SingleInstance.cpp (WP-E). Spec 8.2.
#include <windows.h>
#include "util/Handles.h"

inline constexpr wchar_t kMutexName[]       = L"Local\\TinyLog.Singleton.v1";
inline constexpr wchar_t kOwnerClass[]      = L"TinyLog.TrayOwner.v1";   // App registers this window class
inline constexpr wchar_t kActivateMsgName[] = L"TinyLog.Activate.v1";    // RegisterWindowMessageW name

class SingleInstance {
public:
    // CreateMutexW(kMutexName); false if ERROR_ALREADY_EXISTS (another instance owns it).
    // The mutex handle is kept for the lifetime of this object.
    bool Acquire();
    // FindWindowW(kOwnerClass) + PostMessageW(RegisterWindowMessageW(kActivateMsgName)). Best effort.
    static void ActivateExisting();

private:
    UniqueHandle mutex_;
};
