#pragma once
// FROZEN CONTRACT (Wave 0). Implemented inline here.
#include <windows.h>
#include <cwchar>
#include <string>

struct Status {
    bool ok = true;
    DWORD code = 0;
    std::wstring message;

    static Status Ok() { return {}; }
    static Status Fail(std::wstring msg, DWORD code = 0) {
        Status s;
        s.ok = false;
        s.code = code;
        s.message = std::move(msg);
        return s;
    }
    // Call immediately after the failing Win32 call. Message = ctx + ": " + system message.
    static Status FromLastError(std::wstring ctx) {
        const DWORD e = GetLastError();
        return Fail(std::move(ctx) + L": " + SystemMessage(e), e);
    }
    static Status FromHr(HRESULT hr, std::wstring ctx) {
        return Fail(std::move(ctx) + L": " + SystemMessage(static_cast<DWORD>(hr)), static_cast<DWORD>(hr));
    }
    static std::wstring SystemMessage(DWORD code) {
        wchar_t* buf = nullptr;
        const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                           FORMAT_MESSAGE_IGNORE_INSERTS,
                                       nullptr, code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
        std::wstring msg;
        if (n && buf) {
            msg.assign(buf, n);
            while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' || msg.back() == L' ' || msg.back() == L'.'))
                msg.pop_back();
        }
        if (buf) LocalFree(buf);
        wchar_t hex[32];
        swprintf_s(hex, L" (0x%08X)", code);
        return (msg.empty() ? std::wstring(L"error") : msg) + hex;
    }
    explicit operator bool() const { return ok; }
};
