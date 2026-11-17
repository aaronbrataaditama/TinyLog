#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: SessionRecord.cpp (WP-C).
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct CompletedSession {
    std::string  id;                 // lowercase GUID without braces
    int64_t      startUtc = 0;       // Unix seconds
    int64_t      endUtc = 0;
    int32_t      startOffsetMin = 0; // local - UTC at start
    int32_t      endOffsetMin = 0;
    int64_t      durationSeconds = 0;
    std::wstring notes;              // as typed (CRLF preserved)
};

enum class ActiveStatus { Recording, Completing };

struct ActiveState {
    std::string  id;
    ActiveStatus status = ActiveStatus::Recording;
    int64_t      startUtc = 0;
    int32_t      startOffsetMin = 0;
    std::wstring notes;
    std::optional<int64_t> pendingEndUtc;        // set only when Completing
    std::optional<int32_t> pendingEndOffsetMin;  // set only when Completing
};

// One JSON line WITHOUT trailing '\n'. Field order (Spec 5.2): schema(1), id, startUtc, endUtc,
// startLocal, endLocal, startOffsetMinutes, endOffsetMinutes, durationSeconds, notes.
// *Utc as FormatIsoUtc, *Local as FormatLocal(utc, offset).
std::string SerializeCompleted(const CompletedSession& s);
bool        ParseCompleted(std::string_view line, CompletedSession& out);

// Spec 5.3 fields: schema, id, status ("recording"/"completing"), startUtc, startLocal,
// startOffsetMinutes, notes, pendingEndUtc, pendingEndLocal, pendingEndOffsetMinutes (null when absent).
std::string SerializeActive(const ActiveState& s);
// Rejects: schema != 1, missing/empty id, missing startUtc, unknown status,
// status completing without both pendingEnd* values.
bool        ParseActive(std::string_view text, ActiveState& out);

std::string NewSessionId();   // CoCreateGuid -> "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" lowercase
