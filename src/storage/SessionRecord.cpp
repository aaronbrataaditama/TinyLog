// Session record (de)serialization and ID generation (WP-C). Spec 5.2 / 5.3.
#include "storage/SessionRecord.h"

#include <windows.h>
#include <objbase.h>

#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

#include "storage/Json.h"
#include "util/StringHelpers.h"
#include "util/TimeHelpers.h"

namespace {

using Fields = std::vector<std::pair<std::string, JsonValue>>;

const JsonValue* Find(const Fields& f, std::string_view key) {
    const JsonValue* found = nullptr;
    for (const auto& kv : f) {
        if (kv.first == key) found = &kv.second;   // last duplicate wins
    }
    return found;
}

bool GetString(const Fields& f, std::string_view key, std::string& out) {
    const JsonValue* v = Find(f, key);
    if (!v || v->kind != JsonValue::String) return false;
    out = v->str;
    return true;
}

bool GetNumber(const Fields& f, std::string_view key, int64_t& out) {
    const JsonValue* v = Find(f, key);
    if (!v || v->kind != JsonValue::Number) return false;
    out = v->num;
    return true;
}

bool GetInt32(const Fields& f, std::string_view key, int32_t& out) {
    int64_t n = 0;
    if (!GetNumber(f, key, n)) return false;
    if (n < std::numeric_limits<int32_t>::min() || n > std::numeric_limits<int32_t>::max()) return false;
    out = static_cast<int32_t>(n);
    return true;
}

bool GetIsoUtc(const Fields& f, std::string_view key, int64_t& out) {
    std::string s;
    return GetString(f, key, s) && ParseIsoUtc(s, out);
}

bool SchemaIsOne(const Fields& f) {
    int64_t schema = 0;
    return GetNumber(f, "schema", schema) && schema == 1;
}

}  // namespace

std::string SerializeCompleted(const CompletedSession& s) {
    JsonObjectWriter w;
    w.Num("schema", 1);
    w.Str("id", s.id);
    w.Str("startUtc", FormatIsoUtc(s.startUtc));
    w.Str("endUtc", FormatIsoUtc(s.endUtc));
    w.Str("startLocal", FormatLocal(s.startUtc, s.startOffsetMin));
    w.Str("endLocal", FormatLocal(s.endUtc, s.endOffsetMin));
    w.Num("startOffsetMinutes", s.startOffsetMin);
    w.Num("endOffsetMinutes", s.endOffsetMin);
    w.Num("durationSeconds", s.durationSeconds);
    w.Str("notes", Utf8FromWide(s.notes));
    return w.Finish();
}

bool ParseCompleted(std::string_view line, CompletedSession& out) {
    Fields f;
    if (!ParseFlatJsonObject(line, f)) return false;
    if (!SchemaIsOne(f)) return false;

    CompletedSession c;
    if (!GetString(f, "id", c.id) || c.id.empty()) return false;
    if (!GetIsoUtc(f, "startUtc", c.startUtc)) return false;
    if (!GetIsoUtc(f, "endUtc", c.endUtc)) return false;
    if (!GetInt32(f, "startOffsetMinutes", c.startOffsetMin)) return false;
    if (!GetInt32(f, "endOffsetMinutes", c.endOffsetMin)) return false;
    if (!GetNumber(f, "durationSeconds", c.durationSeconds)) return false;
    std::string notes;
    if (!GetString(f, "notes", notes)) return false;
    if (!WideFromUtf8(notes, c.notes)) return false;
    // startLocal / endLocal are display-only and ignored on read.
    out = std::move(c);
    return true;
}

std::string SerializeActive(const ActiveState& s) {
    JsonObjectWriter w;
    w.Num("schema", 1);
    w.Str("id", s.id);
    w.Str("status", s.status == ActiveStatus::Completing ? "completing" : "recording");
    w.Str("startUtc", FormatIsoUtc(s.startUtc));
    w.Str("startLocal", FormatLocal(s.startUtc, s.startOffsetMin));
    w.Num("startOffsetMinutes", s.startOffsetMin);
    w.Str("notes", Utf8FromWide(s.notes));
    if (s.pendingEndUtc && s.pendingEndOffsetMin) {
        w.Str("pendingEndUtc", FormatIsoUtc(*s.pendingEndUtc));
        w.Str("pendingEndLocal", FormatLocal(*s.pendingEndUtc, *s.pendingEndOffsetMin));
        w.Num("pendingEndOffsetMinutes", *s.pendingEndOffsetMin);
    } else {
        w.Null("pendingEndUtc");
        w.Null("pendingEndLocal");
        w.Null("pendingEndOffsetMinutes");
    }
    return w.Finish();
}

bool ParseActive(std::string_view text, ActiveState& out) {
    Fields f;
    if (!ParseFlatJsonObject(text, f)) return false;
    if (!SchemaIsOne(f)) return false;

    ActiveState a;
    if (!GetString(f, "id", a.id) || a.id.empty()) return false;

    std::string status;
    if (!GetString(f, "status", status)) return false;
    if (status == "recording") a.status = ActiveStatus::Recording;
    else if (status == "completing") a.status = ActiveStatus::Completing;
    else return false;

    if (!GetIsoUtc(f, "startUtc", a.startUtc)) return false;

    // Offset: absent/null tolerated (treated as 0); a present value of the wrong type is rejected.
    if (const JsonValue* v = Find(f, "startOffsetMinutes"); v && v->kind != JsonValue::Null) {
        if (!GetInt32(f, "startOffsetMinutes", a.startOffsetMin)) return false;
    }

    // Notes: absent/null tolerated (empty); wrong type or invalid UTF-8 is rejected.
    if (const JsonValue* v = Find(f, "notes"); v && v->kind != JsonValue::Null) {
        if (v->kind != JsonValue::String) return false;
        if (!WideFromUtf8(v->str, a.notes)) return false;
    }

    if (a.status == ActiveStatus::Completing) {
        int64_t endUtc = 0;
        int32_t endOff = 0;
        if (!GetIsoUtc(f, "pendingEndUtc", endUtc)) return false;
        if (!GetInt32(f, "pendingEndOffsetMinutes", endOff)) return false;
        a.pendingEndUtc = endUtc;
        a.pendingEndOffsetMin = endOff;
    }
    out = std::move(a);
    return true;
}

std::string NewSessionId() {
    GUID g{};
    if (FAILED(CoCreateGuid(&g))) {
        // Extremely unlikely; fall back to time + counter entropy so IDs stay unique within a run.
        static uint32_t counter = 0;
        FILETIME ft{};
        GetSystemTimePreciseAsFileTime(&ft);
        g.Data1 = ft.dwLowDateTime;
        g.Data2 = static_cast<unsigned short>(ft.dwHighDateTime);
        g.Data3 = static_cast<unsigned short>(ft.dwHighDateTime >> 16);
        const uint64_t tail = (static_cast<uint64_t>(GetCurrentProcessId()) << 32) | ++counter;
        for (int i = 0; i < 8; ++i) g.Data4[i] = static_cast<unsigned char>(tail >> (8 * i));
    }
    char buf[40];
    snprintf(buf, sizeof buf, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             static_cast<unsigned long>(g.Data1), static_cast<unsigned>(g.Data2), static_cast<unsigned>(g.Data3),
             static_cast<unsigned>(g.Data4[0]), static_cast<unsigned>(g.Data4[1]), static_cast<unsigned>(g.Data4[2]),
             static_cast<unsigned>(g.Data4[3]), static_cast<unsigned>(g.Data4[4]), static_cast<unsigned>(g.Data4[5]),
             static_cast<unsigned>(g.Data4[6]), static_cast<unsigned>(g.Data4[7]));
    return buf;
}
