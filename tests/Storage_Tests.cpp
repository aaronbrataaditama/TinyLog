// Tests for WP-C storage: JSON codec, record (de)serialization, JSONL store, state store.
#include "TestHarness.h"

#include <windows.h>

#include <string>
#include <utility>
#include <vector>

#include "storage/Json.h"
#include "storage/JsonLinesStore.h"
#include "storage/SessionRecord.h"
#include "storage/StateStore.h"
#include "util/FileHelpers.h"
#include "util/StringHelpers.h"
#include "util/TimeHelpers.h"

namespace {

using Fields = std::vector<std::pair<std::string, JsonValue>>;

CompletedSession MakeCompleted(const std::string& id, const std::wstring& notes = L"work") {
    CompletedSession c;
    c.id = id;
    c.startUtc = 1763341200;   // 2025-11-17T01:00:00Z
    c.endUtc = c.startUtc + 2730;
    c.startOffsetMin = 480;
    c.endOffsetMin = 480;
    c.durationSeconds = 2730;
    c.notes = notes;
    return c;
}

ActiveState MakeActive(const std::string& id, ActiveStatus status = ActiveStatus::Recording) {
    ActiveState a;
    a.id = id;
    a.status = status;
    a.startUtc = 1763341200;
    a.startOffsetMin = 480;
    a.notes = L"notes";
    if (status == ActiveStatus::Completing) {
        a.pendingEndUtc = a.startUtc + 100;
        a.pendingEndOffsetMin = 480;
    }
    return a;
}

void WriteRaw(const std::wstring& path, const std::string& bytes) {
    const Status st = WriteFileAtomic(path, bytes);
    CHECK(st.ok);
}

std::string ReadRaw(const std::wstring& path) {
    std::string b;
    ReadWholeFile(path, b);
    return b;
}

size_t CountRecords(JsonLinesStore& s, size_t* skipped = nullptr) {
    size_t n = 0;
    const Status st = s.ForEach([&](const CompletedSession&) { ++n; return true; }, skipped);
    CHECK(st.ok);
    return n;
}

bool Contains(JsonLinesStore& s, const std::string& id) {
    bool found = false;
    const Status st = s.Contains(id, found);
    CHECK(st.ok);
    return found;
}

}  // namespace

// ---- Json ----

TEST(Storage_Json_ParseBasic) {
    Fields f;
    CHECK(ParseFlatJsonObject(R"(  {"a": 1, "b":"x\ny", "c": null, "d": -42}  )", f));
    CHECK_EQ(f.size(), size_t(4));
    CHECK_EQ(f[0].first, std::string("a"));
    CHECK(f[0].second.kind == JsonValue::Number);
    CHECK_EQ(f[0].second.num, int64_t(1));
    CHECK(f[1].second.kind == JsonValue::String);
    CHECK_EQ(f[1].second.str, std::string("x\ny"));
    CHECK(f[2].second.kind == JsonValue::Null);
    CHECK_EQ(f[3].second.num, int64_t(-42));
    CHECK(ParseFlatJsonObject("{}", f));
    CHECK(f.empty());
}

TEST(Storage_Json_ParseEscapesAndUnicode) {
    Fields f;
    CHECK(ParseFlatJsonObject(R"({"k":"é中😀\/\"\\\b\f\r\t"})", f));
    CHECK_EQ(f[0].second.str, std::string("\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80/\"\\\b\f\r\t"));
}

TEST(Storage_Json_Rejects) {
    Fields f;
    CHECK(!ParseFlatJsonObject("", f));
    CHECK(!ParseFlatJsonObject("[]", f));
    CHECK(!ParseFlatJsonObject(R"({"a":{"b":1}})", f));      // nested object
    CHECK(!ParseFlatJsonObject(R"({"a":[1]})", f));          // array
    CHECK(!ParseFlatJsonObject(R"({"a":true})", f));         // bool
    CHECK(!ParseFlatJsonObject(R"({"a":1.5})", f));          // fraction
    CHECK(!ParseFlatJsonObject(R"({"a":1e3})", f));          // exponent
    CHECK(!ParseFlatJsonObject(R"({"a":01})", f));           // leading zero
    CHECK(!ParseFlatJsonObject(R"({"a":99999999999999999999})", f));   // overflow
    CHECK(!ParseFlatJsonObject(R"({"a":"\x"})", f));         // bad escape
    CHECK(!ParseFlatJsonObject(R"({"a":"\ud83d"})", f));     // lone high surrogate
    CHECK(!ParseFlatJsonObject(R"({"a":"\ude00"})", f));     // lone low surrogate
    CHECK(!ParseFlatJsonObject(R"({"a":"x"} x)", f));        // trailing garbage
    CHECK(!ParseFlatJsonObject(R"({"a":"x")", f));           // unterminated object
    CHECK(!ParseFlatJsonObject(R"({"a":"x)", f));            // unterminated string
    CHECK(!ParseFlatJsonObject(R"({"a":1,})", f));           // trailing comma
    CHECK(!ParseFlatJsonObject("{\"a\":\"x\ty\"}", f));      // raw control char in string
    CHECK(ParseFlatJsonObject(R"({"a":9223372036854775807,"b":-9223372036854775808})", f));
    CHECK_EQ(f[1].second.num, INT64_MIN);
}

TEST(Storage_Json_Writer) {
    JsonObjectWriter w;
    w.Str("a", "x\"y\n");
    w.Num("b", -7);
    w.Null("c");
    CHECK_EQ(w.Finish(), std::string(R"({"a":"x\"y\n","b":-7,"c":null})"));
    CHECK_EQ(w.Finish(), std::string("{}"));   // writer is empty after Finish
}

// ---- SessionRecord ----

TEST(Storage_Completed_FieldOrderAndRoundTrip) {
    CompletedSession c = MakeCompleted("0123abcd-0000-4000-8000-0123456789ab", L"line1\r\nline2 中 \"q\"");
    const std::string line = SerializeCompleted(c);
    CHECK_EQ(line.find('\n'), std::string::npos);
    const char* order[] = {"\"schema\":1", "\"id\"", "\"startUtc\"", "\"endUtc\"", "\"startLocal\"", "\"endLocal\"",
                           "\"startOffsetMinutes\":480", "\"endOffsetMinutes\":480", "\"durationSeconds\":2730",
                           "\"notes\""};
    size_t prev = 0;
    for (const char* key : order) {
        const size_t at = line.find(key);
        CHECK(at != std::string::npos);
        CHECK(at >= prev);
        prev = at;
    }
    CHECK(line.find("\"startUtc\":\"2025-11-17T01:00:00Z\"") != std::string::npos);
    CHECK(line.find("\"startLocal\":\"2025-11-17 09:00:00\"") != std::string::npos);
    CHECK(line.find("line1\\r\\nline2") != std::string::npos);

    CompletedSession back;
    CHECK(ParseCompleted(line, back));
    CHECK_EQ(back.id, c.id);
    CHECK_EQ(back.startUtc, c.startUtc);
    CHECK_EQ(back.endUtc, c.endUtc);
    CHECK_EQ(back.startOffsetMin, c.startOffsetMin);
    CHECK_EQ(back.endOffsetMin, c.endOffsetMin);
    CHECK_EQ(back.durationSeconds, c.durationSeconds);
    CHECK(back.notes == c.notes);
}

TEST(Storage_Completed_ParseValidation) {
    const std::string good = SerializeCompleted(MakeCompleted("abc"));
    CompletedSession out;
    CHECK(ParseCompleted(good, out));

    auto without = [&](const char* key) {
        Fields f;
        ParseFlatJsonObject(good, f);
        JsonObjectWriter w;
        for (auto& kv : f) {
            if (kv.first == key) continue;
            if (kv.second.kind == JsonValue::Number) w.Num(kv.first, kv.second.num);
            else if (kv.second.kind == JsonValue::String) w.Str(kv.first, kv.second.str);
            else w.Null(kv.first);
        }
        return w.Finish();
    };
    for (const char* key : {"schema", "id", "startUtc", "endUtc", "startOffsetMinutes", "endOffsetMinutes",
                            "durationSeconds", "notes"})
        CHECK(!ParseCompleted(without(key), out));
    // Local fields are display-only and not required on read.
    CHECK(ParseCompleted(without("startLocal"), out));
    CHECK(ParseCompleted(without("endLocal"), out));

    CHECK(!ParseCompleted(R"({"schema":2,"id":"a"})", out));
    CHECK(!ParseCompleted("not json", out));
    CHECK(!ParseCompleted("", out));
    std::string badType = good;
    const size_t at = badType.find("\"durationSeconds\":2730");
    badType.replace(at, 22, "\"durationSeconds\":\"2730\"");
    CHECK(!ParseCompleted(badType, out));
    std::string badTime = good;
    badTime.replace(badTime.find("2025-11-17T01:00:00Z"), 20, "2025-13-17T01:00:00Z");
    CHECK(!ParseCompleted(badTime, out));
}

TEST(Storage_Active_RoundTrip) {
    ActiveState rec = MakeActive("id-1");
    rec.notes = L"multi\r\nline é";
    const std::string text = SerializeActive(rec);
    CHECK(text.find("\"status\":\"recording\"") != std::string::npos);
    CHECK(text.find("\"pendingEndUtc\":null") != std::string::npos);
    CHECK(text.find("\"pendingEndLocal\":null") != std::string::npos);
    CHECK(text.find("\"pendingEndOffsetMinutes\":null") != std::string::npos);
    ActiveState back;
    CHECK(ParseActive(text, back));
    CHECK_EQ(back.id, std::string("id-1"));
    CHECK(back.status == ActiveStatus::Recording);
    CHECK_EQ(back.startUtc, rec.startUtc);
    CHECK_EQ(back.startOffsetMin, 480);
    CHECK(back.notes == rec.notes);
    CHECK(!back.pendingEndUtc.has_value());

    ActiveState comp = MakeActive("id-2", ActiveStatus::Completing);
    const std::string ctext = SerializeActive(comp);
    CHECK(ctext.find("\"status\":\"completing\"") != std::string::npos);
    CHECK(ParseActive(ctext, back));
    CHECK(back.status == ActiveStatus::Completing);
    CHECK(back.pendingEndUtc.has_value() && *back.pendingEndUtc == comp.startUtc + 100);
    CHECK(back.pendingEndOffsetMin.has_value() && *back.pendingEndOffsetMin == 480);
}

TEST(Storage_Active_Rejects) {
    ActiveState out;
    const std::string ok = SerializeActive(MakeActive("a"));
    CHECK(ParseActive(ok, out));
    CHECK(!ParseActive(R"({"schema":2,"id":"a","status":"recording","startUtc":"2025-11-17T01:00:00Z"})", out));
    CHECK(!ParseActive(R"({"id":"a","status":"recording","startUtc":"2025-11-17T01:00:00Z"})", out));
    CHECK(!ParseActive(R"({"schema":1,"status":"recording","startUtc":"2025-11-17T01:00:00Z"})", out));
    CHECK(!ParseActive(R"({"schema":1,"id":"","status":"recording","startUtc":"2025-11-17T01:00:00Z"})", out));
    CHECK(!ParseActive(R"({"schema":1,"id":"a","status":"recording"})", out));
    CHECK(!ParseActive(R"({"schema":1,"id":"a","status":"paused","startUtc":"2025-11-17T01:00:00Z"})", out));
    CHECK(!ParseActive(R"({"schema":1,"id":"a","status":"completing","startUtc":"2025-11-17T01:00:00Z"})", out));
    CHECK(!ParseActive(R"({"schema":1,"id":"a","status":"completing","startUtc":"2025-11-17T01:00:00Z",
        "pendingEndUtc":"2025-11-17T01:05:00Z"})", out));
    CHECK(!ParseActive(R"({"schema":1,"id":"a","status":"completing","startUtc":"2025-11-17T01:00:00Z",
        "pendingEndOffsetMinutes":60})", out));
    CHECK(!ParseActive("", out));
    CHECK(!ParseActive("garbage", out));
    CHECK(!ParseActive(R"({"schema":1,"id":"a","status":"recording","startUtc":"nope"})", out));
}

TEST(Storage_NewSessionId_Format) {
    const std::string a = NewSessionId();
    const std::string b = NewSessionId();
    CHECK(a != b);
    CHECK_EQ(a.size(), size_t(36));
    for (size_t i = 0; i < a.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            CHECK_EQ(a[i], '-');
        } else {
            const bool hex = (a[i] >= '0' && a[i] <= '9') || (a[i] >= 'a' && a[i] <= 'f');
            CHECK(hex);
        }
    }
}

// ---- JsonLinesStore ----

TEST(Storage_Jsonl_MissingFile) {
    TempDir dir;
    JsonLinesStore s(dir.File(L"TinyLog.jsonl"));
    CHECK(s.RepairTail().ok);
    bool found = true;
    CHECK(s.Contains("x", found).ok);
    CHECK(!found);
    CHECK_EQ(CountRecords(s), size_t(0));
    std::optional<CompletedSession> last;
    CHECK(s.ReadLast(last).ok);
    CHECK(!last.has_value());
    CHECK(!FileExists(s.Path()));   // reading never creates the file
}

TEST(Storage_Jsonl_AppendContainsForEachReadLast) {
    TempDir dir;
    JsonLinesStore s(dir.File(L"TinyLog.jsonl"));
    for (int i = 0; i < 3; ++i) CHECK(s.Append(MakeCompleted("id-" + std::to_string(i), L"n" + std::to_wstring(i))).ok);
    CHECK(Contains(s, "id-0"));
    CHECK(Contains(s, "id-2"));
    CHECK(!Contains(s, "id-3"));

    std::vector<std::string> ids;
    size_t skipped = 99;
    CHECK(s.ForEach([&](const CompletedSession& c) { ids.push_back(c.id); return true; }, &skipped).ok);
    CHECK_EQ(ids.size(), size_t(3));
    CHECK_EQ(ids[0], std::string("id-0"));
    CHECK_EQ(ids[2], std::string("id-2"));
    CHECK_EQ(skipped, size_t(0));

    size_t seen = 0;
    CHECK(s.ForEach([&](const CompletedSession&) { ++seen; return false; }).ok);   // early stop
    CHECK_EQ(seen, size_t(1));

    std::optional<CompletedSession> last;
    CHECK(s.ReadLast(last).ok);
    CHECK(last.has_value());
    CHECK_EQ(last->id, std::string("id-2"));
    CHECK(last->notes == L"n2");

    const std::string raw = ReadRaw(s.Path());
    CHECK_EQ(raw.back(), '\n');
}

TEST(Storage_Jsonl_TruncatedTailRepaired) {
    TempDir dir;
    JsonLinesStore s(dir.File(L"TinyLog.jsonl"));
    CHECK(s.Append(MakeCompleted("a")).ok);
    CHECK(s.Append(MakeCompleted("b")).ok);
    const std::string good = ReadRaw(s.Path());
    WriteRaw(s.Path(), good + R"({"schema":1,"id":"x)");

    CHECK(s.RepairTail().ok);
    CHECK_EQ(ReadRaw(s.Path()), good);   // prior lines intact, partial tail gone
    CHECK(Contains(s, "a"));
    CHECK(Contains(s, "b"));

    // Append after a damaged tail repairs first and writes a clean line.
    WriteRaw(s.Path(), good + R"({"schema":1,"id":"y","start)");
    CHECK(s.Append(MakeCompleted("c")).ok);
    size_t skipped = 0;
    CHECK_EQ(CountRecords(s, &skipped), size_t(3));
    CHECK_EQ(skipped, size_t(0));
    CHECK(Contains(s, "c"));
}

TEST(Storage_Jsonl_UnparsableTerminatedLastLineKept) {
    // A newline-terminated line that fails strict parsing may be a real record (future schema, hand
    // edit): RepairTail must not destroy it. ForEach/Contains skip it; Append adds after it.
    TempDir dir;
    JsonLinesStore s(dir.File(L"TinyLog.jsonl"));
    CHECK(s.Append(MakeCompleted("a")).ok);
    const std::string good = ReadRaw(s.Path());
    const std::string future = R"({"schema":2,"id":"future","startUtc":"2030-01-01T00:00:00Z"})" "\n";
    WriteRaw(s.Path(), good + future);

    CHECK(s.RepairTail().ok);
    CHECK_EQ(ReadRaw(s.Path()), good + future);   // untouched
    size_t skipped = 0;
    CHECK_EQ(CountRecords(s, &skipped), size_t(1));
    CHECK_EQ(skipped, size_t(1));
    CHECK(!Contains(s, "future"));

    CHECK(s.Append(MakeCompleted("b")).ok);
    const std::string after = ReadRaw(s.Path());
    CHECK(after.compare(0, good.size() + future.size(), good + future) == 0);   // prefix preserved byte for byte
    CHECK_EQ(CountRecords(s), size_t(2));
    CHECK(Contains(s, "b"));

    // Same for plain garbage and for a CRLF-terminated unparsable line.
    WriteRaw(s.Path(), good + "garbage line\r\n");
    CHECK(s.RepairTail().ok);
    CHECK_EQ(ReadRaw(s.Path()), good + "garbage line\r\n");
}

TEST(Storage_Jsonl_UnterminatedUnparsableSegmentStillTruncatedAfterKeptLine) {
    TempDir dir;
    JsonLinesStore s(dir.File(L"TinyLog.jsonl"));
    CHECK(s.Append(MakeCompleted("a")).ok);
    const std::string base = ReadRaw(s.Path()) + "garbage line\n";
    WriteRaw(s.Path(), base + "{\"schema\":1,\"id\":\"torn");
    CHECK(s.RepairTail().ok);
    CHECK_EQ(ReadRaw(s.Path()), base);   // only the unterminated tail goes
}

TEST(Storage_Jsonl_ValidRecordMissingNewlineKept) {
    TempDir dir;
    JsonLinesStore s(dir.File(L"TinyLog.jsonl"));
    CHECK(s.Append(MakeCompleted("a")).ok);
    const std::string line = SerializeCompleted(MakeCompleted("b"));
    const std::string good = ReadRaw(s.Path());
    WriteRaw(s.Path(), good + line);   // complete record, '\n' never landed
    CHECK(s.RepairTail().ok);
    CHECK_EQ(ReadRaw(s.Path()), good + line + "\n");
    CHECK_EQ(CountRecords(s), size_t(2));
}

TEST(Storage_Jsonl_MiddleMalformedSkippedAndCounted) {
    TempDir dir;
    JsonLinesStore s(dir.File(L"TinyLog.jsonl"));
    const std::string l1 = SerializeCompleted(MakeCompleted("a"));
    const std::string l3 = SerializeCompleted(MakeCompleted("c"));
    const std::string content = l1 + "\n{not json}\n\n" + l3 + "\n";   // blank line is neutral, not counted
    WriteRaw(s.Path(), content);
    size_t skipped = 0;
    CHECK_EQ(CountRecords(s, &skipped), size_t(2));
    CHECK_EQ(skipped, size_t(1));
    CHECK(s.RepairTail().ok);
    CHECK_EQ(ReadRaw(s.Path()), content);   // a middle bad line is never touched
    CHECK(Contains(s, "c"));
}

TEST(Storage_Jsonl_CrLfTolerated) {
    TempDir dir;
    JsonLinesStore s(dir.File(L"TinyLog.jsonl"));
    const std::string content = SerializeCompleted(MakeCompleted("a")) + "\r\n" +
                                SerializeCompleted(MakeCompleted("b")) + "\r\n";
    WriteRaw(s.Path(), content);
    size_t skipped = 0;
    CHECK_EQ(CountRecords(s, &skipped), size_t(2));
    CHECK_EQ(skipped, size_t(0));
    CHECK(s.RepairTail().ok);
    CHECK_EQ(ReadRaw(s.Path()), content);
    std::optional<CompletedSession> last;
    CHECK(s.ReadLast(last).ok);
    CHECK_EQ(last->id, std::string("b"));
}

TEST(Storage_Jsonl_LargeFileStreamingAcrossChunks) {
    TempDir dir;
    JsonLinesStore s(dir.File(L"TinyLog.jsonl"));
    std::string content;
    const std::wstring bigNotes(5000, L'x');
    const int kCount = 60;   // ~ 60 * 5.2 KB > 64 KB several times over
    for (int i = 0; i < kCount; ++i) content += SerializeCompleted(MakeCompleted("big-" + std::to_string(i), bigNotes)) + "\n";
    CHECK(content.size() > 3 * 64 * 1024);
    WriteRaw(s.Path(), content);
    CHECK_EQ(CountRecords(s), size_t(kCount));
    CHECK(Contains(s, "big-0"));
    CHECK(Contains(s, "big-59"));
    CHECK(!Contains(s, "big-60"));
    std::optional<CompletedSession> last;
    CHECK(s.ReadLast(last).ok);
    CHECK_EQ(last->id, std::string("big-59"));
    CHECK(last->notes == bigNotes);
}

TEST(Storage_Jsonl_ReadLastWithLongFinalLineAndBadTail) {
    TempDir dir;
    JsonLinesStore s(dir.File(L"TinyLog.jsonl"));
    const std::wstring huge(200000, L'y');   // one line far larger than a 64 KB chunk
    const std::string content = SerializeCompleted(MakeCompleted("first")) + "\n" +
                                SerializeCompleted(MakeCompleted("huge", huge)) + "\n";
    WriteRaw(s.Path(), content);
    std::optional<CompletedSession> last;
    CHECK(s.ReadLast(last).ok);
    CHECK_EQ(last->id, std::string("huge"));
    CHECK(Contains(s, "huge"));

    // Garbage after the last valid record: ReadLast skips it and finds the record behind it.
    WriteRaw(s.Path(), content + "garbage\n{partial");
    CHECK(s.ReadLast(last).ok);
    CHECK_EQ(last->id, std::string("huge"));
    CHECK(s.RepairTail().ok);   // removes only the final partial segment
    CHECK_EQ(ReadRaw(s.Path()), content + "garbage\n");
}

TEST(Storage_Jsonl_UnicodeMultilineNotesRoundTrip) {
    TempDir dir;
    JsonLinesStore s(dir.File(L"TinyLog.jsonl"));
    const std::wstring notes = L"line1\r\nline2\r\n\t\"quoted\" \\ back é中\U0001F600 end";
    CHECK(s.Append(MakeCompleted("u", notes)).ok);
    std::optional<CompletedSession> last;
    CHECK(s.ReadLast(last).ok);
    CHECK(last.has_value());
    CHECK(last->notes == notes);
    CHECK_EQ(CountRecords(s), size_t(1));   // newlines are escaped, so still one physical line
}

// ---- StateStore ----

TEST(Storage_State_SaveLoadRemove) {
    TempDir dir;
    StateStore st(dir.path());
    CHECK(st.StatePath() == dir.File(L"TinyLog.state"));
    CHECK(st.Load().kind == StateStore::LoadKind::None);

    CHECK(st.Save(MakeActive("s1")).ok);
    CHECK(!FileExists(dir.File(L"TinyLog.state.tmp")));
    StateStore::LoadResult r = st.Load();
    CHECK(r.kind == StateStore::LoadKind::Loaded);
    CHECK_EQ(r.state.id, std::string("s1"));

    CHECK(st.Save(MakeActive("s1", ActiveStatus::Completing)).ok);   // replace over existing
    r = st.Load();
    CHECK(r.kind == StateStore::LoadKind::Loaded);
    CHECK(r.state.status == ActiveStatus::Completing);

    CHECK(st.Remove().ok);
    CHECK(!FileExists(st.StatePath()));
    CHECK(st.Remove().ok);   // missing = Ok
    CHECK(st.Load().kind == StateStore::LoadKind::None);
}

TEST(Storage_State_StrayTmpDeletedWhenStateValid) {
    TempDir dir;
    StateStore st(dir.path());
    CHECK(st.Save(MakeActive("good")).ok);
    WriteRaw(dir.File(L"TinyLog.state.tmp"), SerializeActive(MakeActive("other")));
    const StateStore::LoadResult r = st.Load();
    CHECK(r.kind == StateStore::LoadKind::Loaded);
    CHECK_EQ(r.state.id, std::string("good"));
    CHECK(!FileExists(dir.File(L"TinyLog.state.tmp")));
}

TEST(Storage_State_TmpPromotedWhenStateMissing) {
    TempDir dir;
    StateStore st(dir.path());
    WriteRaw(dir.File(L"TinyLog.state.tmp"), SerializeActive(MakeActive("fromtmp")));
    const StateStore::LoadResult r = st.Load();
    CHECK(r.kind == StateStore::LoadKind::PromotedTmp);
    CHECK_EQ(r.state.id, std::string("fromtmp"));
    CHECK(!r.diagnostic.empty());
    CHECK(FileExists(st.StatePath()));
    CHECK(!FileExists(dir.File(L"TinyLog.state.tmp")));
    CHECK(st.Load().kind == StateStore::LoadKind::Loaded);   // now a normal state file
}

TEST(Storage_State_TmpPromotedOverInvalidStateQuarantinesIt) {
    TempDir dir;
    StateStore st(dir.path());
    WriteRaw(st.StatePath(), "{ corrupt");
    WriteRaw(dir.File(L"TinyLog.state.tmp"), SerializeActive(MakeActive("fromtmp")));
    const StateStore::LoadResult r = st.Load();
    CHECK(r.kind == StateStore::LoadKind::PromotedTmp);
    CHECK_EQ(r.state.id, std::string("fromtmp"));
    // The invalid original survives under a corrupt-* name.
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(dir.File(L"TinyLog.state.corrupt-*").c_str(), &fd);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) {
        CHECK_EQ(ReadRaw(dir.File(fd.cFileName)), std::string("{ corrupt"));
        FindClose(h);
    }
}

TEST(Storage_State_InvalidTmpLeftInPlaceWhenStateMissing) {
    TempDir dir;
    StateStore st(dir.path());
    WriteRaw(dir.File(L"TinyLog.state.tmp"), "junk");
    CHECK(st.Load().kind == StateStore::LoadKind::None);
    CHECK_EQ(ReadRaw(dir.File(L"TinyLog.state.tmp")), std::string("junk"));
}

TEST(Storage_State_InvalidStateReportedAndUntouched) {
    TempDir dir;
    StateStore st(dir.path());
    WriteRaw(st.StatePath(), "{\"schema\":1");
    const StateStore::LoadResult r = st.Load();
    CHECK(r.kind == StateStore::LoadKind::Invalid);
    CHECK(!r.diagnostic.empty());
    CHECK_EQ(ReadRaw(st.StatePath()), std::string("{\"schema\":1"));

    // Invalid state plus invalid tmp: still Invalid, both untouched.
    WriteRaw(dir.File(L"TinyLog.state.tmp"), "junk");
    CHECK(st.Load().kind == StateStore::LoadKind::Invalid);
    CHECK_EQ(ReadRaw(dir.File(L"TinyLog.state.tmp")), std::string("junk"));
}

// Opens a file with share mode 0 so any other open fails with a sharing violation.
static HANDLE HoldExclusive(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    return h;
}

TEST(Storage_State_ReadErrorIsIoErrorNotInvalid) {
    TempDir dir;
    StateStore st(dir.path());
    CHECK(st.Save(MakeActive("keep-me")).ok);
    const std::string before = ReadRaw(st.StatePath());

    HANDLE h = HoldExclusive(st.StatePath());
    const StateStore::LoadResult r = st.Load();
    CHECK(r.kind == StateStore::LoadKind::IoError);
    CHECK(!r.diagnostic.empty());
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

    // Nothing was moved or changed; once the lock is gone the state loads normally.
    CHECK_EQ(ReadRaw(st.StatePath()), before);
    CHECK(FileExists(st.StatePath()));
    const StateStore::LoadResult again = st.Load();
    CHECK(again.kind == StateStore::LoadKind::Loaded);
    CHECK_EQ(again.state.id, std::string("keep-me"));
}

TEST(Storage_State_IoErrorLeavesValidTmpAndLockedStateAlone) {
    TempDir dir;
    StateStore st(dir.path());
    CHECK(st.Save(MakeActive("state-id")).ok);
    WriteRaw(dir.File(L"TinyLog.state.tmp"), SerializeActive(MakeActive("tmp-id")));
    const std::string stateBefore = ReadRaw(st.StatePath());

    HANDLE h = HoldExclusive(st.StatePath());
    CHECK(st.Load().kind == StateStore::LoadKind::IoError);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    CHECK(FileExists(dir.File(L"TinyLog.state.tmp")));   // stale tmp not deleted, not promoted
    CHECK_EQ(ReadRaw(st.StatePath()), stateBefore);
}

TEST(Storage_State_TmpReadErrorWhileStateMissingIsIoError) {
    TempDir dir;
    StateStore st(dir.path());
    const std::wstring tmp = dir.File(L"TinyLog.state.tmp");
    WriteRaw(tmp, SerializeActive(MakeActive("only-copy")));
    const std::string before = ReadRaw(tmp);

    HANDLE h = HoldExclusive(tmp);
    CHECK(st.Load().kind == StateStore::LoadKind::IoError);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    CHECK(!FileExists(st.StatePath()));
    CHECK_EQ(ReadRaw(tmp), before);

    const StateStore::LoadResult r = st.Load();   // lock released: normal promotion
    CHECK(r.kind == StateStore::LoadKind::PromotedTmp);
    CHECK_EQ(r.state.id, std::string("only-copy"));
}

TEST(Storage_State_QuarantineKeepsFileUnderNewName) {
    TempDir dir;
    StateStore st(dir.path());
    WriteRaw(st.StatePath(), "bad-1");
    std::wstring moved1;
    CHECK(st.QuarantineInvalid(moved1).ok);
    CHECK(!FileExists(st.StatePath()));
    CHECK(FileExists(moved1));
    CHECK_EQ(ReadRaw(moved1), std::string("bad-1"));
    CHECK(moved1.find(L"TinyLog.state.corrupt-") != std::wstring::npos);
    // "TinyLog.state.corrupt-YYYYMMDD-HHMMSS" is 37 chars.
    const size_t nameAt = moved1.find_last_of(L'\\') + 1;
    CHECK(moved1.size() - nameAt >= 37);

    // A second quarantine within the same second must not overwrite the first.
    WriteRaw(st.StatePath(), "bad-2");
    std::wstring moved2;
    CHECK(st.QuarantineInvalid(moved2).ok);
    CHECK(moved1 != moved2);
    CHECK_EQ(ReadRaw(moved1), std::string("bad-1"));
    CHECK_EQ(ReadRaw(moved2), std::string("bad-2"));
}

TEST(Storage_State_SaveFailsWhenTmpBlocked) {
    TempDir dir;
    StateStore st(dir.path());
    const std::wstring tmp = dir.File(L"TinyLog.state.tmp");
    CHECK(CreateDirectoryW(tmp.c_str(), nullptr) != 0);   // a directory squatting on the .tmp name
    CHECK(!st.Save(MakeActive("x")).ok);
    CHECK(!FileExists(st.StatePath()));
}
