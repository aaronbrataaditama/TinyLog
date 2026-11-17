#pragma once
// FROZEN CONTRACT (Wave 0). Implemented inline here.
#include <windows.h>
#include <objbase.h>

// Balanced CoInitializeEx(COINIT_APARTMENTTHREADED). Check ok() before COM work.
class ComInit {
public:
    ComInit() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ComInit() { if (SUCCEEDED(hr)) CoUninitialize(); }
    ComInit(const ComInit&) = delete;
    ComInit& operator=(const ComInit&) = delete;
    bool ok() const { return SUCCEEDED(hr); }   // S_OK or S_FALSE (already initialized)
    HRESULT hr;
};

// Owns a CoTaskMemAlloc'd string (e.g. from SHGetKnownFolderPath, IShellItem::GetDisplayName).
class CoTaskMemString {
public:
    CoTaskMemString() = default;
    ~CoTaskMemString() { CoTaskMemFree(p_); }
    CoTaskMemString(const CoTaskMemString&) = delete;
    CoTaskMemString& operator=(const CoTaskMemString&) = delete;
    PWSTR* put() { CoTaskMemFree(p_); p_ = nullptr; return &p_; }
    PCWSTR get() const { return p_; }

private:
    PWSTR p_ = nullptr;
};
