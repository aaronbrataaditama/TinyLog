#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: TimeHelpers.cpp (WP-A).
#include <windows.h>
#include <cstdint>
#include <string>
#include <string_view>

struct Timestamp {
    int64_t utc = 0;            // Unix seconds (UTC)
    int32_t offsetMinutes = 0;  // local - UTC at that instant, e.g. +480 for UTC+8
};

int64_t      UnixFromFileTime(const FILETIME& ft);   // floor to whole seconds
// GetSystemTimePreciseAsFileTime; offset via SystemTimeToTzSpecificLocalTimeEx (dynamic TZ) for that instant.
Timestamp    CaptureNow();

std::string  FormatIsoUtc(int64_t utc);                     // "2026-11-17T01:00:00Z"
bool         ParseIsoUtc(std::string_view s, int64_t& utc);  // exactly "YYYY-MM-DDTHH:MM:SSZ", validated ranges
std::string  FormatLocal(int64_t utc, int32_t offsetMin);    // "2026-11-17 09:00:00" for utc + offsetMin*60

// Howard Hinnant's civil-date algorithms; days since 1970-01-01 (negative before).
void         CivilFromDays(int64_t days, int& y, int& m, int& d);
int64_t      DaysFromCivil(int y, int m, int d);

// Excel 1900 date system: DaysFromCivil(y,m,d) - DaysFromCivil(1899,12,30). Valid for dates >= 1900-03-01.
int64_t      ExcelSerialFromCivil(int y, int m, int d);

// "HH:MM:SS"; hours may exceed 99 ("123:04:05"); negative -> "00:00:00".
std::wstring FormatElapsed(int64_t seconds);
