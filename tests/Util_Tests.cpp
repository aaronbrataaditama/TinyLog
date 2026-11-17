// Tests for WP-A pure utilities (string, time, crc32, file helpers).
#include "TestHarness.h"
#include "util/Crc32.h"
#include "util/FileHelpers.h"
#include "util/StringHelpers.h"
#include "util/TimeHelpers.h"

#include <cstdint>
#include <string>

// ---- CRC-32 ----

TEST(Util_Crc32_CheckValue) {
    CHECK_EQ(Crc32Update(0, "123456789", 9), 0xCBF43926u);
    CHECK_EQ(Crc32Update(0, "", 0), 0u);
}

TEST(Util_Crc32_SplitEqualsOneShot) {
    const char* all = "123456789";
    const uint32_t one = Crc32Update(0, all, 9);
    uint32_t split = Crc32Update(0, all, 4);
    split = Crc32Update(split, all + 4, 5);
    CHECK_EQ(split, one);
}

// ---- Dates ----

TEST(Util_Excel_Serials) {
    CHECK_EQ(ExcelSerialFromCivil(2026, 11, 17), int64_t(46343));
    CHECK_EQ(ExcelSerialFromCivil(2025, 1, 1), int64_t(45658));
    CHECK_EQ(ExcelSerialFromCivil(2000, 2, 29), int64_t(36585));
    CHECK_EQ(ExcelSerialFromCivil(1900, 3, 1), int64_t(61));
}

TEST(Util_Days_Epoch) {
    CHECK_EQ(DaysFromCivil(1970, 1, 1), int64_t(0));
    CHECK_EQ(DaysFromCivil(1969, 12, 31), int64_t(-1));
    int y = 0, m = 0, d = 0;
    CivilFromDays(-1, y, m, d);
    CHECK_EQ(y, 1969);
    CHECK_EQ(m, 12);
    CHECK_EQ(d, 31);
}

TEST(Util_CivilRoundTrip_1900_2100) {
    const int64_t first = DaysFromCivil(1900, 1, 1);
    const int64_t last = DaysFromCivil(2100, 12, 31);
    int bad = 0;
    for (int64_t dd = first; dd <= last; ++dd) {
        int y = 0, m = 0, d = 0;
        CivilFromDays(dd, y, m, d);
        if (DaysFromCivil(y, m, d) != dd) ++bad;  // also catches invalid dates such as Feb 30
    }
    CHECK_EQ(bad, 0);
}

// ---- ISO / local time ----

TEST(Util_IsoUtc_Format) {
    CHECK_EQ(FormatIsoUtc(0), std::string("1970-01-01T00:00:00Z"));
    CHECK_EQ(FormatIsoUtc(-1), std::string("1969-12-31T23:59:59Z"));
}

TEST(Util_IsoUtc_RoundTrip) {
    const int64_t values[] = {0, -1, 1795222800, 951782400, 4102444799};
    for (int64_t v : values) {
        int64_t back = 12345;
        CHECK(ParseIsoUtc(FormatIsoUtc(v), back));
        CHECK_EQ(back, v);
    }
}

TEST(Util_IsoUtc_Rejects) {
    int64_t u = 0;
    CHECK(!ParseIsoUtc("2026-11-17 01:00:00", u));
    CHECK(!ParseIsoUtc("2026-11-17T01:00:00Z ", u));
    CHECK(!ParseIsoUtc("2026-11-17T01:00:00", u));
    CHECK(!ParseIsoUtc("2026-13-17T01:00:00Z", u));
    CHECK(!ParseIsoUtc("2026-02-29T00:00:00Z", u));
    CHECK(!ParseIsoUtc("2026-11-17T24:00:00Z", u));
    CHECK(ParseIsoUtc("2028-02-29T00:00:00Z", u));
}

TEST(Util_FormatLocal) {
    const int64_t utc = 1795222800;
    const std::string iso = FormatIsoUtc(utc + 480 * 60);
    const std::string expected = iso.substr(0, 10) + " " + iso.substr(11, 8);
    CHECK_EQ(FormatLocal(utc, 480), expected);
    CHECK_EQ(FormatLocal(0, -300), std::string("1969-12-31 19:00:00"));
}

TEST(Util_UnixFromFileTime) {
    const auto mk = [](uint64_t ticks) {
        ULARGE_INTEGER u;
        u.QuadPart = ticks;
        FILETIME f{};
        f.dwLowDateTime = u.LowPart;
        f.dwHighDateTime = u.HighPart;
        return f;
    };
    const uint64_t epoch = 116444736000000000ULL;
    CHECK_EQ(UnixFromFileTime(mk(epoch)), int64_t(0));
    CHECK_EQ(UnixFromFileTime(mk(epoch + 9999999ULL)), int64_t(0));  // floors to whole seconds
    CHECK_EQ(UnixFromFileTime(mk(epoch + 10000000ULL)), int64_t(1));
    CHECK_EQ(UnixFromFileTime(mk(epoch - 1)), int64_t(-1));
}

TEST(Util_CaptureNow_Sane) {
    const Timestamp t = CaptureNow();
    CHECK(t.utc > 1700000000);
    CHECK(t.offsetMinutes >= -840 && t.offsetMinutes <= 840);
    int64_t back = 0;
    CHECK(ParseIsoUtc(FormatIsoUtc(t.utc), back));
    CHECK_EQ(back, t.utc);
}

TEST(Util_FormatElapsed) {
    CHECK_EQ(FormatElapsed(45296), std::wstring(L"12:34:56"));
    CHECK_EQ(FormatElapsed(360000), std::wstring(L"100:00:00"));
    CHECK_EQ(FormatElapsed(-5), std::wstring(L"00:00:00"));
    CHECK_EQ(FormatElapsed(0), std::wstring(L"00:00:00"));
}

// ---- Strings ----

TEST(Util_Utf8_RoundTrip) {
    const std::wstring w = L"Fix login \U0001F600 ü\r\n";
    const std::string u = Utf8FromWide(w);
    CHECK_EQ(u, std::string("Fix login \xF0\x9F\x98\x80 \xC3\xBC\r\n"));
    std::wstring back;
    CHECK(WideFromUtf8(u, back));
    CHECK_EQ(back, w);
}

TEST(Util_Utf8_RejectsInvalid) {
    std::wstring out;
    CHECK(!WideFromUtf8("\xC3\x28", out));
    CHECK(WideFromUtf8("", out));
    CHECK(out.empty());
}

TEST(Util_JsonEscape) {
    CHECK_EQ(JsonEscape("a\"b\\c\n\x01"), std::string("a\\\"b\\\\c\\n\\u0001"));
    CHECK_EQ(JsonEscape("\b\f\r\t"), std::string("\\b\\f\\r\\t"));
    CHECK_EQ(JsonEscape("\xC3\xBC"), std::string("\xC3\xBC"));
}

TEST(Util_XmlEscape) {
    CHECK_EQ(XmlEscape("<a&'\">\x01\t"), std::string("&lt;a&amp;&apos;&quot;&gt;\t"));
    CHECK_EQ(XmlEscape("\xEF\xBF\xBE" "x" "\xEF\xBF\xBF"), std::string("x"));
    CHECK_EQ(XmlEscape("\xC3\xBC\xEF\xBF\xBD\n\r"), std::string("\xC3\xBC\xEF\xBF\xBD\n\r"));
}

TEST(Util_NormalizeNewlines) {
    CHECK_EQ(NormalizeNewlinesToLf(L"a\r\nb\rc\n\r\n"), std::wstring(L"a\nb\nc\n\n"));
}

TEST(Util_StartsWithFormulaChar) {
    CHECK(StartsWithFormulaChar(L"=1+1"));
    CHECK(StartsWithFormulaChar(L"+x"));
    CHECK(StartsWithFormulaChar(L"-x"));
    CHECK(StartsWithFormulaChar(L"@SUM"));
    CHECK(!StartsWithFormulaChar(L" =x"));
    CHECK(!StartsWithFormulaChar(L"abc"));
    CHECK(!StartsWithFormulaChar(L""));
}

// ---- Files ----

TEST(Util_WriteFileAtomic_CreateOverwrite) {
    TempDir dir;
    const std::wstring target = dir.File(L"data.txt");
    CHECK(WriteFileAtomic(target, "first"));
    CHECK(WriteFileAtomic(target, "second-longer-content"));
    std::string bytes;
    CHECK(ReadWholeFile(target, bytes));
    CHECK_EQ(bytes, std::string("second-longer-content"));
    CHECK(FileExists(target));
    CHECK(GetFileAttributesW(dir.File(L"data.txt.tmp").c_str()) == INVALID_FILE_ATTRIBUTES);
}

TEST(Util_WriteFileAtomic_BinaryAndEmpty) {
    TempDir dir;
    const std::wstring target = dir.File(L"bin.dat");
    std::string payload(3, '\0');
    payload[1] = static_cast<char>(0xFF);
    CHECK(WriteFileAtomic(target, payload));
    std::string bytes;
    CHECK(ReadWholeFile(target, bytes));
    CHECK_EQ(bytes, payload);
    CHECK(WriteFileAtomic(target, ""));
    CHECK(ReadWholeFile(target, bytes));
    CHECK(bytes.empty());
}

TEST(Util_ReadWholeFile_Missing) {
    TempDir dir;
    std::string bytes;
    const Status s = ReadWholeFile(dir.File(L"nope.txt"), bytes);
    CHECK(!s);
    CHECK(s.code == ERROR_FILE_NOT_FOUND || s.code == ERROR_PATH_NOT_FOUND);
    CHECK(!FileExists(dir.File(L"nope.txt")));
}

TEST(Util_FileExists_DirIsNotFile) {
    TempDir dir;
    CHECK(!FileExists(dir.path()));
}

TEST(Util_GetExeDirectory_Sane) {
    std::wstring dir;
    CHECK(GetExeDirectory(dir));
    CHECK(!dir.empty());
    CHECK(dir.back() != L'\\');
    std::wstring exe;
    CHECK(GetExePath(exe));
    CHECK(exe.size() > 4 && exe.compare(exe.size() - 4, 4, L".exe") == 0);
    CHECK(exe.compare(0, dir.size(), dir) == 0);
}
