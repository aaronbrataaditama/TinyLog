#pragma once
// FROZEN CONTRACT (Wave 0). RAII wrappers, move-only. Implemented inline here.
// COM interfaces: use Microsoft::WRL::ComPtr from <wrl/client.h>.
#include <windows.h>
#include <utility>

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE h) : h_(h) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& o) noexcept : h_(o.release()) {}
    UniqueHandle& operator=(UniqueHandle&& o) noexcept { if (this != &o) reset(o.release()); return *this; }

    HANDLE get() const { return h_; }
    bool valid() const { return h_ != nullptr && h_ != INVALID_HANDLE_VALUE; }
    explicit operator bool() const { return valid(); }
    HANDLE release() { HANDLE h = h_; h_ = nullptr; return h; }
    void reset(HANDLE h = nullptr) { if (valid()) CloseHandle(h_); h_ = h; }

private:
    HANDLE h_ = nullptr;
};

// HFONT / HBRUSH / HPEN / HRGN / HBITMAP. Never destroy an object still selected into a DC
// (use SelectGuard so it is deselected first).
template <class T>
class UniqueGdi {
public:
    UniqueGdi() = default;
    explicit UniqueGdi(T h) : h_(h) {}
    ~UniqueGdi() { reset(); }
    UniqueGdi(const UniqueGdi&) = delete;
    UniqueGdi& operator=(const UniqueGdi&) = delete;
    UniqueGdi(UniqueGdi&& o) noexcept : h_(o.release()) {}
    UniqueGdi& operator=(UniqueGdi&& o) noexcept { if (this != &o) reset(o.release()); return *this; }

    T get() const { return h_; }
    explicit operator bool() const { return h_ != nullptr; }
    T release() { T h = h_; h_ = nullptr; return h; }   // e.g. after SetWindowRgn succeeds
    void reset(T h = nullptr) { if (h_) DeleteObject(h_); h_ = h; }

private:
    T h_ = nullptr;
};

// Memory DCs from CreateCompatibleDC only (not GetDC/BeginPaint DCs).
class UniqueDC {
public:
    UniqueDC() = default;
    explicit UniqueDC(HDC h) : h_(h) {}
    ~UniqueDC() { reset(); }
    UniqueDC(const UniqueDC&) = delete;
    UniqueDC& operator=(const UniqueDC&) = delete;
    UniqueDC(UniqueDC&& o) noexcept : h_(o.release()) {}
    UniqueDC& operator=(UniqueDC&& o) noexcept { if (this != &o) reset(o.release()); return *this; }

    HDC get() const { return h_; }
    explicit operator bool() const { return h_ != nullptr; }
    HDC release() { HDC h = h_; h_ = nullptr; return h; }
    void reset(HDC h = nullptr) { if (h_) DeleteDC(h_); h_ = h; }

private:
    HDC h_ = nullptr;
};

class UniqueIcon {
public:
    UniqueIcon() = default;
    explicit UniqueIcon(HICON h) : h_(h) {}
    ~UniqueIcon() { reset(); }
    UniqueIcon(const UniqueIcon&) = delete;
    UniqueIcon& operator=(const UniqueIcon&) = delete;
    UniqueIcon(UniqueIcon&& o) noexcept : h_(o.release()) {}
    UniqueIcon& operator=(UniqueIcon&& o) noexcept { if (this != &o) reset(o.release()); return *this; }

    HICON get() const { return h_; }
    explicit operator bool() const { return h_ != nullptr; }
    HICON release() { HICON h = h_; h_ = nullptr; return h; }
    void reset(HICON h = nullptr) { if (h_) DestroyIcon(h_); h_ = h; }

private:
    HICON h_ = nullptr;
};

// Selects obj into dc and restores the previous object on destruction.
class SelectGuard {
public:
    SelectGuard(HDC dc, HGDIOBJ obj) : dc_(dc), old_(SelectObject(dc, obj)) {}
    ~SelectGuard() { if (dc_ && old_ && old_ != HGDI_ERROR) SelectObject(dc_, old_); }
    SelectGuard(const SelectGuard&) = delete;
    SelectGuard& operator=(const SelectGuard&) = delete;

private:
    HDC dc_;
    HGDIOBJ old_;
};
