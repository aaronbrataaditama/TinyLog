#include "util/TimeHelpers.h"

#include <cstdint>
#include <cstdio>

namespace {

constexpr int64_t kSecPerDay = 86400;
constexpr uint64_t kTicksPerSecond = 10000000ULL;
constexpr int64_t kUnixEpochInFileTimeSeconds = 11644473600LL;  // 1601-01-01 -> 1970-01-01

int64_t FloorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

bool IsLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int DaysInMonth(int y, int m) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && IsLeap(y)) ? 29 : kDays[m - 1];
}

// "YYYY-MM-DD HH:MM:SS" (iso == false) or "YYYY-MM-DDTHH:MM:SSZ" (iso == true) for unix time t.
std::string FormatCivil(int64_t t, bool iso) {
    const int64_t days = FloorDiv(t, kSecPerDay);
    const int64_t rem = t - days * kSecPerDay;
    int y = 0, m = 0, d = 0;
    CivilFromDays(days, y, m, d);
    const int hh = static_cast<int>(rem / 3600);
    const int mm = static_cast<int>((rem % 3600) / 60);
    const int ss = static_cast<int>(rem % 60);
    char buf[64];
    if (iso) {
        snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ", y, m, d, hh, mm, ss);
    } else {
        snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d", y, m, d, hh, mm, ss);
    }
    return buf;
}

}  // namespace

int64_t UnixFromFileTime(const FILETIME& ft) {
    const uint64_t ticks = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<int64_t>(ticks / kTicksPerSecond) - kUnixEpochInFileTimeSeconds;
}

Timestamp CaptureNow() {
    Timestamp t;
    FILETIME ft{};
    GetSystemTimePreciseAsFileTime(&ft);
    t.utc = UnixFromFileTime(ft);

    SYSTEMTIME utcSt{};
    if (!FileTimeToSystemTime(&ft, &utcSt)) return t;
    DYNAMIC_TIME_ZONE_INFORMATION dtzi{};
    if (GetDynamicTimeZoneInformation(&dtzi) == TIME_ZONE_ID_INVALID) return t;
    SYSTEMTIME localSt{};
    if (!SystemTimeToTzSpecificLocalTimeEx(&dtzi, &utcSt, &localSt)) return t;
    FILETIME localFt{};
    if (!SystemTimeToFileTime(&localSt, &localFt)) return t;

    // Local wall clock read as if it were UTC, minus the true UTC: the offset in seconds.
    const int64_t diff = UnixFromFileTime(localFt) - t.utc;
    t.offsetMinutes = static_cast<int32_t>((diff >= 0 ? diff + 30 : diff - 30) / 60);
    return t;
}

std::string FormatIsoUtc(int64_t utc) { return FormatCivil(utc, true); }

bool ParseIsoUtc(std::string_view s, int64_t& utc) {
    if (s.size() != 20) return false;
    static const size_t kDigitPos[] = {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18};
    for (size_t p : kDigitPos) {
        if (s[p] < '0' || s[p] > '9') return false;
    }
    if (s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[19] != 'Z') return false;

    auto num = [&s](size_t at, size_t n) {
        int v = 0;
        for (size_t k = 0; k < n; ++k) v = v * 10 + (s[at + k] - '0');
        return v;
    };
    const int y = num(0, 4);
    const int mo = num(5, 2);
    const int d = num(8, 2);
    const int h = num(11, 2);
    const int mi = num(14, 2);
    const int se = num(17, 2);
    if (mo < 1 || mo > 12 || d < 1 || d > DaysInMonth(y, mo)) return false;
    if (h > 23 || mi > 59 || se > 59) return false;

    utc = DaysFromCivil(y, mo, d) * kSecPerDay + h * 3600 + mi * 60 + se;
    return true;
}

std::string FormatLocal(int64_t utc, int32_t offsetMin) {
    return FormatCivil(utc + static_cast<int64_t>(offsetMin) * 60, false);
}

void CivilFromDays(int64_t days, int& y, int& m, int& d) {
    // Howard Hinnant's civil_from_days.
    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int64_t dd = doy - (153 * mp + 2) / 5 + 1;
    const int64_t mm = mp < 10 ? mp + 3 : mp - 9;
    const int64_t yy = yoe + era * 400 + (mm <= 2 ? 1 : 0);
    y = static_cast<int>(yy);
    m = static_cast<int>(mm);
    d = static_cast<int>(dd);
}

int64_t DaysFromCivil(int y, int m, int d) {
    // Howard Hinnant's days_from_civil.
    const int64_t yy = static_cast<int64_t>(y) - (m <= 2 ? 1 : 0);
    const int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
    const int64_t yoe = yy - era * 400;
    const int64_t mp = m > 2 ? m - 3 : m + 9;
    const int64_t doy = (153 * mp + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int64_t ExcelSerialFromCivil(int y, int m, int d) {
    return DaysFromCivil(y, m, d) - DaysFromCivil(1899, 12, 30);
}

std::wstring FormatElapsed(int64_t seconds) {
    if (seconds < 0) seconds = 0;
    const long long h = seconds / 3600;
    const long long m = (seconds % 3600) / 60;
    const long long s = seconds % 60;
    wchar_t buf[64];
    swprintf_s(buf, L"%02lld:%02lld:%02lld", h, m, s);
    return buf;
}
