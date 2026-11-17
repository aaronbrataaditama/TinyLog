#pragma once
// Minimal header-only test harness (Wave 0, frozen). No third-party dependencies.
//
//   TEST(MyCase) { CHECK(x); CHECK_EQ(a, b); }      // names must be unique within a file
//   TempDir dir;  dir.path();  dir.File(L"x.txt");   // unique dir under %TEMP%\TinyLogTests, deleted after
//
// Run: TinyLogTests.exe [substring-filter]
#include <windows.h>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

namespace th {

struct TestCase {
    const char* name;
    const char* file;
    void (*fn)();
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> r;
    return r;
}
inline int& FailureCount() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const char* name, const char* file, void (*fn)()) { Registry().push_back({name, file, fn}); }
};

inline std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

template <class T>
std::string ToStr(const T& v) {
    using U = std::decay_t<T>;
    if constexpr (std::is_same_v<U, bool>) {
        return v ? "true" : "false";
    } else if constexpr (std::is_enum_v<U>) {
        return std::to_string(static_cast<long long>(v));
    } else if constexpr (std::is_floating_point_v<U>) {
        char b[64];
        snprintf(b, sizeof b, "%.17g", static_cast<double>(v));
        return b;
    } else if constexpr (std::is_integral_v<U>) {
        return std::to_string(v);
    } else if constexpr (std::is_convertible_v<U, std::string_view>) {
        return "\"" + std::string(std::string_view(v)) + "\"";
    } else if constexpr (std::is_convertible_v<U, std::wstring_view>) {
        return "L\"" + Narrow(std::wstring(std::wstring_view(v))) + "\"";
    } else {
        return "<value>";
    }
}

inline void Fail(const char* file, int line, const char* expr, const std::string& detail) {
    ++FailureCount();
    std::printf("  FAILED %s(%d): %s%s%s\n", file, line, expr, detail.empty() ? "" : "\n         ", detail.c_str());
}

}  // namespace th

#define TL_CAT2(a, b) a##b
#define TL_CAT(a, b) TL_CAT2(a, b)

#define TEST(Name)                                                                     \
    static void TL_CAT(test_, Name)();                                                 \
    static th::Registrar TL_CAT(reg_, Name)(#Name, __FILE__, &TL_CAT(test_, Name));     \
    static void TL_CAT(test_, Name)()

#define CHECK(expr)                                                     \
    do {                                                                \
        if (!(expr)) th::Fail(__FILE__, __LINE__, #expr, std::string()); \
    } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        const auto& tl_a_ = (a);                                                                    \
        const auto& tl_b_ = (b);                                                                    \
        if (!(tl_a_ == tl_b_))                                                                      \
            th::Fail(__FILE__, __LINE__, #a " == " #b, th::ToStr(tl_a_) + " vs " + th::ToStr(tl_b_)); \
    } while (0)

#define CHECK_STR(a, b) CHECK_EQ(a, b)

// Unique temp directory, recursively deleted (read-only attributes cleared) on destruction.
class TempDir {
public:
    TempDir() {
        wchar_t tmp[MAX_PATH + 1];
        GetTempPathW(MAX_PATH + 1, tmp);
        std::wstring root = std::wstring(tmp) + L"TinyLogTests";
        CreateDirectoryW(root.c_str(), nullptr);
        static unsigned counter = 0;
        wchar_t name[96];
        swprintf_s(name, L"\\%lu-%u-%llu", GetCurrentProcessId(), ++counter,
                   static_cast<unsigned long long>(GetTickCount64()));
        path_ = root + name;
        CreateDirectoryW(path_.c_str(), nullptr);
    }
    ~TempDir() { DeleteTree(path_); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::wstring& path() const { return path_; }
    std::wstring File(const std::wstring& name) const { return path_ + L"\\" + name; }

    static void DeleteTree(const std::wstring& dir) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring n = fd.cFileName;
                if (n == L"." || n == L"..") continue;
                const std::wstring p = dir + L"\\" + n;
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    DeleteTree(p);
                else
                    DeleteFileW(p.c_str());
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        SetFileAttributesW(dir.c_str(), FILE_ATTRIBUTE_NORMAL);
        RemoveDirectoryW(dir.c_str());
    }

private:
    std::wstring path_;
};
