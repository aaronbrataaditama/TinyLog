// WP-E tests: shortcut core + PathsEqual. Never touches the real Startup folder, tray or registry.
#include "TestHarness.h"

#include "util/ComHelpers.h"
#include "win32/StartupShortcut.h"

namespace {

void Touch(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

}  // namespace

TEST(Startup_CreateAndReadShortcut) {
    ComInit com;
    CHECK(com.ok());
    TempDir dir;
    const std::wstring target = dir.File(L"app.exe");
    Touch(target);
    const std::wstring lnk = dir.File(L"TinyLog.lnk");

    Status s = startup::CreateShortcut(lnk, target, dir.path());
    CHECK(s.ok);
    CHECK(GetFileAttributesW(lnk.c_str()) != INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesW((lnk + L".tmp").c_str()) == INVALID_FILE_ATTRIBUTES);

    std::wstring read;
    s = startup::ReadShortcutTarget(lnk, read);
    CHECK(s.ok);
    // The shell may normalize short (8.3) temp paths to long ones; PathsEqual is what the product uses.
    CHECK(startup::PathsEqual(read, target));
}

TEST(Startup_OverwriteShortcut) {
    ComInit com;
    CHECK(com.ok());
    TempDir dir;
    const std::wstring t1 = dir.File(L"one.exe");
    const std::wstring t2 = dir.File(L"two.exe");
    Touch(t1);
    Touch(t2);
    const std::wstring lnk = dir.File(L"TinyLog.lnk");

    CHECK(startup::CreateShortcut(lnk, t1, dir.path()).ok);
    CHECK(startup::CreateShortcut(lnk, t2, dir.path()).ok);

    std::wstring read;
    CHECK(startup::ReadShortcutTarget(lnk, read).ok);
    CHECK(startup::PathsEqual(read, t2));
    CHECK(!startup::PathsEqual(read, t1));
}

TEST(Startup_ReadMissingShortcutFails) {
    ComInit com;
    CHECK(com.ok());
    TempDir dir;
    std::wstring read = L"junk";
    const Status s = startup::ReadShortcutTarget(dir.File(L"nope.lnk"), read);
    CHECK(!s.ok);
}

TEST(Startup_PathsEqualCaseInsensitive) {
    TempDir dir;
    const std::wstring file = dir.File(L"MixedCase.exe");
    Touch(file);
    std::wstring upper = file;
    for (auto& c : upper) c = static_cast<wchar_t>(towupper(c));
    CHECK(startup::PathsEqual(file, upper));

    // Non-existent paths fall back to the normalized ordinal comparison.
    CHECK(startup::PathsEqual(dir.File(L"Ghost.exe"), dir.File(L"GHOST.EXE")));
}

TEST(Startup_PathsEqualNormalizesDotDot) {
    TempDir dir;
    CreateDirectoryW(dir.File(L"sub").c_str(), nullptr);
    const std::wstring file = dir.File(L"file.txt");
    Touch(file);
    CHECK(startup::PathsEqual(dir.path() + L"\\sub\\..\\file.txt", file));
    // Same, but for paths that do not exist.
    CHECK(startup::PathsEqual(dir.path() + L"\\sub\\..\\ghost.txt", dir.File(L"ghost.txt")));
}

TEST(Startup_PathsEqualDifferentFiles) {
    TempDir dir;
    const std::wstring a = dir.File(L"a.txt");
    const std::wstring b = dir.File(L"b.txt");
    Touch(a);
    Touch(b);
    CHECK(!startup::PathsEqual(a, b));
    CHECK(!startup::PathsEqual(a, dir.File(L"missing.txt")));
}
