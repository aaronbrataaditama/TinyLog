// Tests for WP-C SessionManager: Start/Stop transaction, crash recovery matrix (Spec 5.4-5.6, 8.7).
#include "TestHarness.h"

#include <windows.h>

#include <string>
#include <vector>

#include "app/Clock.h"
#include "app/SessionManager.h"
#include "storage/JsonLinesStore.h"
#include "storage/SessionRecord.h"
#include "storage/StateStore.h"
#include "util/FileHelpers.h"
#include "util/TimeHelpers.h"

namespace {

struct FakeClock final : Clock {
    Timestamp now;
    Timestamp Now() override { return now; }
    void Set(int64_t utc, int32_t off) { now.utc = utc; now.offsetMinutes = off; }
};

int64_t Utc(const char* iso) {
    int64_t t = 0;
    const bool ok = ParseIsoUtc(iso, t);
    CHECK(ok);
    return t;
}

// One directory's worth of stores plus a manager, like the real app wiring.
struct Rig {
    explicit Rig(const std::wstring& dir) : state(dir), log(dir + L"\\TinyLog.jsonl"), mgr(state, log, clock) {}
    FakeClock       clock;
    StateStore      state;
    JsonLinesStore  log;
    SessionManager  mgr;
};

std::vector<CompletedSession> AllRecords(JsonLinesStore& log) {
    std::vector<CompletedSession> v;
    const Status st = log.ForEach([&](const CompletedSession& c) { v.push_back(c); return true; });
    CHECK(st.ok);
    return v;
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

ActiveState Completing(const std::string& id, int64_t start, int64_t end, const std::wstring& notes) {
    ActiveState a;
    a.id = id;
    a.status = ActiveStatus::Completing;
    a.startUtc = start;
    a.startOffsetMin = 480;
    a.notes = notes;
    a.pendingEndUtc = end;
    a.pendingEndOffsetMin = 480;
    return a;
}

// Opens a file with share mode 0 so any other open fails with a sharing violation.
HANDLE HoldExclusive(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    return h;
}

CompletedSession CompletedFrom(const ActiveState& a) {
    CompletedSession c;
    c.id = a.id;
    c.startUtc = a.startUtc;
    c.endUtc = *a.pendingEndUtc;
    c.startOffsetMin = a.startOffsetMin;
    c.endOffsetMin = *a.pendingEndOffsetMin;
    c.durationSeconds = c.endUtc - c.startUtc;
    c.notes = a.notes;
    return c;
}

}  // namespace

TEST(Session_StartStopHappyPath) {
    TempDir dir;
    Rig r(dir.path());
    r.clock.Set(Utc("2025-11-17T01:00:00Z"), 480);
    CHECK(r.mgr.State() == AppState::Idle);
    CHECK(r.mgr.Active() == nullptr);

    CHECK(r.mgr.Start(L"ticket ABC-123").ok);
    CHECK(r.mgr.State() == AppState::Recording);
    CHECK(r.mgr.Active() != nullptr);
    CHECK(FileExists(r.state.StatePath()));
    CHECK_EQ(r.mgr.Active()->id.size(), size_t(36));

    r.clock.Set(Utc("2025-11-17T01:45:30Z"), 480);
    CHECK(r.mgr.Stop().ok);
    CHECK(r.mgr.State() == AppState::Idle);
    CHECK(r.mgr.Active() == nullptr);
    CHECK(!FileExists(r.state.StatePath()));
    CHECK(r.mgr.Notes() == L"ticket ABC-123");   // notes stay after Stop

    const auto recs = AllRecords(r.log);
    CHECK_EQ(recs.size(), size_t(1));
    CHECK_EQ(recs[0].startUtc, Utc("2025-11-17T01:00:00Z"));
    CHECK_EQ(recs[0].endUtc, Utc("2025-11-17T01:45:30Z"));
    CHECK_EQ(recs[0].durationSeconds, int64_t(2730));
    CHECK_EQ(recs[0].startOffsetMin, 480);
    CHECK_EQ(recs[0].endOffsetMin, 480);
    CHECK(recs[0].notes == L"ticket ABC-123");

    const std::string raw = ReadRaw(r.log.Path());
    CHECK(raw.find("\"startLocal\":\"2025-11-17 09:00:00\"") != std::string::npos);
    CHECK(raw.find("\"endLocal\":\"2025-11-17 09:45:30\"") != std::string::npos);

    // A second session gets a different id and a second line.
    CHECK(r.mgr.Start(L"again").ok);
    r.clock.Set(Utc("2025-11-17T02:00:00Z"), 480);
    CHECK(r.mgr.Stop().ok);
    const auto two = AllRecords(r.log);
    CHECK_EQ(two.size(), size_t(2));
    CHECK(two[0].id != two[1].id);
}

TEST(Session_StopTwiceAndStartWhileRecordingRejected) {
    TempDir dir;
    Rig r(dir.path());
    r.clock.Set(Utc("2025-11-17T01:00:00Z"), 0);
    CHECK(!r.mgr.Stop().ok);   // Idle: nothing to stop
    CHECK(r.mgr.Start(L"a").ok);
    const std::string id = r.mgr.Active()->id;
    CHECK(!r.mgr.Start(L"b").ok);                    // already Recording
    CHECK(r.mgr.State() == AppState::Recording);
    CHECK_EQ(r.mgr.Active()->id, id);                // the running session is untouched
    CHECK(r.mgr.Stop().ok);
    CHECK(!r.mgr.Stop().ok);                         // second Stop rejected
    CHECK_EQ(AllRecords(r.log).size(), size_t(1));
}

TEST(Session_UpdateNotesPersistsWhileRecordingOnly) {
    TempDir dir;
    Rig r(dir.path());
    r.clock.Set(Utc("2025-11-17T01:00:00Z"), 0);
    CHECK(r.mgr.UpdateNotes(L"idle typing").ok);   // Idle: memory only
    CHECK(r.mgr.Notes() == L"idle typing");
    CHECK(!FileExists(r.state.StatePath()));

    CHECK(r.mgr.Start(L"first").ok);
    CHECK(r.mgr.UpdateNotes(L"second\r\nline").ok);
    const StateStore::LoadResult lr = r.state.Load();
    CHECK(lr.kind == StateStore::LoadKind::Loaded);
    CHECK(lr.state.notes == L"second\r\nline");
    CHECK(r.mgr.Active()->notes == L"second\r\nline");

    r.clock.Set(Utc("2025-11-17T01:10:00Z"), 0);
    CHECK(r.mgr.Stop().ok);
    CHECK(AllRecords(r.log)[0].notes == L"second\r\nline");
}

TEST(Session_RecoverRestoresRecording) {
    TempDir dir;
    {
        Rig r(dir.path());
        r.clock.Set(Utc("2025-11-17T01:00:00Z"), 480);
        CHECK(r.mgr.Start(L"before crash").ok);
        CHECK(r.mgr.UpdateNotes(L"edited before crash").ok);
    }   // "crash": nothing else happens
    Rig r2(dir.path());
    const SessionManager::RecoveryReport rep = r2.mgr.Recover();
    CHECK(rep.restoredRecording);
    CHECK(!rep.finishedPendingStop);
    CHECK(!rep.quarantined);
    CHECK(rep.error.empty());
    CHECK(r2.mgr.State() == AppState::Recording);
    CHECK(r2.mgr.Notes() == L"edited before crash");
    CHECK_EQ(r2.mgr.Active()->startUtc, Utc("2025-11-17T01:00:00Z"));
    CHECK_EQ(r2.mgr.Active()->startOffsetMin, 480);

    r2.clock.Set(Utc("2025-11-17T03:00:00Z"), 480);
    CHECK(r2.mgr.Stop().ok);
    const auto recs = AllRecords(r2.log);
    CHECK_EQ(recs.size(), size_t(1));
    CHECK_EQ(recs[0].durationSeconds, int64_t(7200));
}

TEST(Session_RecoverCompletingAbsentAppendsOnce) {
    TempDir dir;
    Rig r(dir.path());
    const ActiveState a = Completing("id-absent", Utc("2025-11-17T01:00:00Z"), Utc("2025-11-17T01:30:00Z"), L"pending notes");
    CHECK(r.state.Save(a).ok);

    const SessionManager::RecoveryReport rep = r.mgr.Recover();
    CHECK(rep.finishedPendingStop);
    CHECK(!rep.restoredRecording);
    CHECK(rep.error.empty());
    CHECK(r.mgr.State() == AppState::Idle);
    CHECK(!FileExists(r.state.StatePath()));
    const auto recs = AllRecords(r.log);
    CHECK_EQ(recs.size(), size_t(1));
    CHECK_EQ(recs[0].id, std::string("id-absent"));
    CHECK_EQ(recs[0].durationSeconds, int64_t(1800));
    CHECK(recs[0].notes == L"pending notes");
    CHECK(rep.idleNotes == L"pending notes");

    // A second Recover is a no-op.
    const SessionManager::RecoveryReport rep2 = r.mgr.Recover();
    CHECK(!rep2.finishedPendingStop);
    CHECK_EQ(AllRecords(r.log).size(), size_t(1));
}

TEST(Session_CrashAfterAppendBeforeRemoveNoDuplicate) {
    // Crash simulation: completing state on disk AND the record already appended.
    TempDir dir;
    Rig r(dir.path());
    const ActiveState a = Completing("id-present", Utc("2025-11-17T01:00:00Z"), Utc("2025-11-17T01:30:00Z"), L"n");
    CHECK(r.state.Save(a).ok);
    CHECK(r.log.Append(CompletedFrom(a)).ok);

    const SessionManager::RecoveryReport rep = r.mgr.Recover();
    CHECK(rep.finishedPendingStop);
    CHECK(rep.error.empty());
    CHECK(r.mgr.State() == AppState::Idle);
    CHECK(!FileExists(r.state.StatePath()));
    CHECK_EQ(AllRecords(r.log).size(), size_t(1));
}

TEST(Session_RecoverIdleFillsNotesFromLastRecord) {
    TempDir dir;
    Rig r(dir.path());
    CHECK(r.log.Append(CompletedFrom(Completing("a", 1000, 2000, L"older"))).ok);
    CHECK(r.log.Append(CompletedFrom(Completing("b", 3000, 4000, L"latest notes"))).ok);
    const SessionManager::RecoveryReport rep = r.mgr.Recover();
    CHECK(!rep.restoredRecording && !rep.finishedPendingStop && !rep.quarantined);
    CHECK(rep.idleNotes == L"latest notes");
    CHECK(r.mgr.State() == AppState::Idle);

    TempDir empty;
    Rig r2(empty.path());
    CHECK(r2.mgr.Recover().idleNotes.empty());
}

TEST(Session_RecoverRepairsTruncatedTailKeepsPriorLines) {
    TempDir dir;
    Rig r(dir.path());
    CHECK(r.log.Append(CompletedFrom(Completing("a", 1000, 2000, L"one"))).ok);
    CHECK(r.log.Append(CompletedFrom(Completing("b", 3000, 4000, L"two"))).ok);
    const std::string good = ReadRaw(r.log.Path());
    WriteRaw(r.log.Path(), good + R"({"schema":1,"id":"x)");

    const SessionManager::RecoveryReport rep = r.mgr.Recover();
    CHECK(rep.error.empty());
    CHECK_EQ(ReadRaw(r.log.Path()), good);
    CHECK(rep.idleNotes == L"two");
    CHECK_EQ(AllRecords(r.log).size(), size_t(2));
}

TEST(Session_RecoverCompletingAfterTruncatedTailAppendsCleanly) {
    TempDir dir;
    Rig r(dir.path());
    CHECK(r.log.Append(CompletedFrom(Completing("a", 1000, 2000, L"one"))).ok);
    const std::string good = ReadRaw(r.log.Path());
    WriteRaw(r.log.Path(), good + R"({"schema":1,"id":"half-written)");   // crash mid-append of the pending record
    CHECK(r.state.Save(Completing("half-written", 5000, 6000, L"pend")).ok);

    const SessionManager::RecoveryReport rep = r.mgr.Recover();
    CHECK(rep.finishedPendingStop);
    const auto recs = AllRecords(r.log);
    CHECK_EQ(recs.size(), size_t(2));
    CHECK_EQ(recs[1].id, std::string("half-written"));
    CHECK_EQ(recs[1].durationSeconds, int64_t(1000));
    size_t skipped = 9;
    CHECK(r.log.ForEach([](const CompletedSession&) { return true; }, &skipped).ok);
    CHECK_EQ(skipped, size_t(0));
}

TEST(Session_InvalidStateQuarantinedAndNeverOverwritten) {
    TempDir dir;
    Rig r(dir.path());
    const std::string garbage = "{\"schema\":1,\"id\":\"zz\",\"status\":\"rec";   // truncated state
    WriteRaw(r.state.StatePath(), garbage);

    const SessionManager::RecoveryReport rep = r.mgr.Recover();
    CHECK(rep.quarantined);
    CHECK(!rep.error.empty());
    CHECK(rep.quarantinePath.find(L"TinyLog.state.corrupt-") != std::wstring::npos);
    CHECK(r.mgr.State() == AppState::Idle);
    CHECK(!FileExists(r.state.StatePath()));
    CHECK(FileExists(rep.quarantinePath));                 // preserved, not deleted
    CHECK_EQ(ReadRaw(rep.quarantinePath), garbage);

    // The app can continue: a new session does not touch the quarantined copy.
    r.clock.Set(Utc("2025-11-17T01:00:00Z"), 0);
    CHECK(r.mgr.Start(L"new").ok);
    CHECK_EQ(ReadRaw(rep.quarantinePath), garbage);
}

TEST(Session_StartRefusedWhenInvalidStateCannotBeQuarantined) {
    TempDir dir;
    Rig r(dir.path());
    // A directory named TinyLog.state: not a valid state and cannot be renamed over by a file save.
    CHECK(CreateDirectoryW(r.state.StatePath().c_str(), nullptr) != 0);
    // Hold the "state" open so the quarantine rename fails deterministically.
    HANDLE h = CreateFileW((r.state.StatePath() + L"\\x").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    const SessionManager::RecoveryReport rep = r.mgr.Recover();
    CHECK(!rep.error.empty());
    CHECK(r.mgr.State() == AppState::Idle);
    if (!rep.quarantined) {
        CHECK(!r.mgr.Start(L"x").ok);   // never proceeds to overwrite the unresolved state file
        CHECK(r.mgr.State() == AppState::Idle);
    }
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

TEST(Session_RecoverPromotesValidTmp) {
    TempDir dir;
    Rig r(dir.path());
    ActiveState a;
    a.id = "tmp-session";
    a.startUtc = Utc("2025-11-17T01:00:00Z");
    a.startOffsetMin = 60;
    a.notes = L"from tmp";
    WriteRaw(dir.File(L"TinyLog.state.tmp"), SerializeActive(a));

    const SessionManager::RecoveryReport rep = r.mgr.Recover();
    CHECK(rep.restoredRecording);
    CHECK(r.mgr.State() == AppState::Recording);
    CHECK_EQ(r.mgr.Active()->id, std::string("tmp-session"));
    CHECK(r.mgr.Notes() == L"from tmp");
    CHECK(FileExists(r.state.StatePath()));
    CHECK(!FileExists(dir.File(L"TinyLog.state.tmp")));
}

TEST(Session_UnicodeMultilineNotesRoundTrip) {
    TempDir dir;
    Rig r(dir.path());
    const std::wstring notes = L"Café 中文 \U0001F600\r\nsecond \"line\" \\ tab\t end\r\n";
    r.clock.Set(Utc("2025-11-17T01:00:00Z"), 480);
    CHECK(r.mgr.Start(notes).ok);

    Rig r2(dir.path());   // restart: recording survives with exact notes
    CHECK(r2.mgr.Recover().restoredRecording);
    CHECK(r2.mgr.Notes() == notes);

    r2.clock.Set(Utc("2025-11-17T01:05:00Z"), 480);
    CHECK(r2.mgr.Stop().ok);
    const auto recs = AllRecords(r2.log);
    CHECK_EQ(recs.size(), size_t(1));
    CHECK(recs[0].notes == notes);

    Rig r3(dir.path());   // restart in Idle: notes prefilled from the last session
    CHECK(r3.mgr.Recover().idleNotes == notes);
}

TEST(Session_MidnightCrossingIsOneRecord) {
    TempDir dir;
    Rig r(dir.path());
    r.clock.Set(Utc("2025-11-17T15:50:00Z"), 480);   // 23:50 local
    CHECK(r.mgr.Start(L"late").ok);
    r.clock.Set(Utc("2025-11-17T16:10:00Z"), 480);   // 00:10 local next day
    CHECK(r.mgr.Stop().ok);
    const auto recs = AllRecords(r.log);
    CHECK_EQ(recs.size(), size_t(1));
    CHECK_EQ(recs[0].durationSeconds, int64_t(1200));
    const std::string raw = ReadRaw(r.log.Path());
    CHECK(raw.find("\"startLocal\":\"2025-11-17 23:50:00\"") != std::string::npos);
    CHECK(raw.find("\"endLocal\":\"2025-11-18 00:10:00\"") != std::string::npos);
}

TEST(Session_DstChangeDurationFromUtc) {
    TempDir dir;
    Rig r(dir.path());
    r.clock.Set(Utc("2025-03-30T00:30:00Z"), 60);    // 01:30 local, before spring-forward
    CHECK(r.mgr.Start(L"dst").ok);
    r.clock.Set(Utc("2025-03-30T01:30:00Z"), 120);   // 03:30 local; wall clock says 2h, UTC says 1h
    CHECK(r.mgr.Stop().ok);
    const auto recs = AllRecords(r.log);
    CHECK_EQ(recs.size(), size_t(1));
    CHECK_EQ(recs[0].durationSeconds, int64_t(3600));
    CHECK_EQ(recs[0].startOffsetMin, 60);
    CHECK_EQ(recs[0].endOffsetMin, 120);
    const std::string raw = ReadRaw(r.log.Path());
    CHECK(raw.find("\"startLocal\":\"2025-03-30 01:30:00\"") != std::string::npos);
    CHECK(raw.find("\"endLocal\":\"2025-03-30 03:30:00\"") != std::string::npos);
}

TEST(Session_ClockGoingBackwardsClampsDurationToZero) {
    TempDir dir;
    Rig r(dir.path());
    r.clock.Set(Utc("2025-11-17T02:00:00Z"), 0);
    CHECK(r.mgr.Start(L"").ok);
    r.clock.Set(Utc("2025-11-17T01:00:00Z"), 0);
    CHECK(r.mgr.Stop().ok);
    const auto recs = AllRecords(r.log);
    CHECK_EQ(recs.size(), size_t(1));
    CHECK_EQ(recs[0].durationSeconds, int64_t(0));
}

TEST(Session_StartFailsAndStaysIdleWhenStateNotWritable) {
    TempDir dir;
    Rig r(dir.path());
    // A read-only TinyLog.state.tmp makes the atomic write fail (CREATE_ALWAYS -> access denied)
    // without needing admin rights.
    const std::wstring tmp = dir.File(L"TinyLog.state.tmp");
    WriteRaw(tmp, "occupied");
    CHECK(SetFileAttributesW(tmp.c_str(), FILE_ATTRIBUTE_READONLY) != 0);

    r.clock.Set(Utc("2025-11-17T01:00:00Z"), 0);
    const Status st = r.mgr.Start(L"will not start");
    CHECK(!st.ok);
    CHECK(r.mgr.State() == AppState::Idle);
    CHECK(r.mgr.Active() == nullptr);
    CHECK(!FileExists(r.state.StatePath()));

    // Once the obstruction is gone, Start works.
    SetFileAttributesW(tmp.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(tmp.c_str());
    CHECK(r.mgr.Start(L"now it starts").ok);
    CHECK(r.mgr.State() == AppState::Recording);
}

TEST(Session_StopFailsToSaveCompletingStaysRecording) {
    TempDir dir;
    Rig r(dir.path());
    r.clock.Set(Utc("2025-11-17T01:00:00Z"), 0);
    CHECK(r.mgr.Start(L"keep going").ok);
    const std::string before = ReadRaw(r.state.StatePath());

    const std::wstring tmp = dir.File(L"TinyLog.state.tmp");
    WriteRaw(tmp, "occupied");
    CHECK(SetFileAttributesW(tmp.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
    r.clock.Set(Utc("2025-11-17T02:00:00Z"), 0);
    CHECK(!r.mgr.Stop().ok);
    CHECK(r.mgr.State() == AppState::Recording);
    CHECK_EQ(ReadRaw(r.state.StatePath()), before);   // recording state untouched
    CHECK_EQ(AllRecords(r.log).size(), size_t(0));

    SetFileAttributesW(tmp.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(tmp.c_str());
    CHECK(r.mgr.Stop().ok);
    CHECK_EQ(AllRecords(r.log).size(), size_t(1));
}

TEST(Session_StopAppendFailureKeepsCompletingThenRetrySucceedsOnce) {
    TempDir dir;
    Rig r(dir.path());
    // A directory squatting on TinyLog.jsonl makes every log operation fail.
    CHECK(CreateDirectoryW(r.log.Path().c_str(), nullptr) != 0);

    r.clock.Set(Utc("2025-11-17T01:00:00Z"), 0);
    CHECK(r.mgr.Start(L"notes survive").ok);
    const std::string id = r.mgr.Active()->id;
    r.clock.Set(Utc("2025-11-17T01:30:00Z"), 0);
    CHECK(!r.mgr.Stop().ok);
    CHECK(r.mgr.State() == AppState::Recording);          // not Idle: the session is not lost
    CHECK(r.mgr.Active() != nullptr);
    CHECK_EQ(r.mgr.Active()->id, id);

    const StateStore::LoadResult lr = r.state.Load();     // completing state is on disk for recovery
    CHECK(lr.kind == StateStore::LoadKind::Loaded);
    CHECK(lr.state.status == ActiveStatus::Completing);
    CHECK_EQ(lr.state.id, id);
    CHECK(lr.state.notes == L"notes survive");

    // Fix the log and retry Stop.
    RemoveDirectoryW(r.log.Path().c_str());
    r.clock.Set(Utc("2025-11-17T01:40:00Z"), 0);
    CHECK(r.mgr.Stop().ok);
    CHECK(r.mgr.State() == AppState::Idle);
    const auto recs = AllRecords(r.log);
    CHECK_EQ(recs.size(), size_t(1));
    CHECK_EQ(recs[0].id, id);
    CHECK(!FileExists(r.state.StatePath()));
}

TEST(Session_StopAppendFailureThenRestartRecoversIt) {
    TempDir dir;
    {
        Rig r(dir.path());
        CHECK(CreateDirectoryW(r.log.Path().c_str(), nullptr) != 0);
        r.clock.Set(Utc("2025-11-17T01:00:00Z"), 0);
        CHECK(r.mgr.Start(L"x").ok);
        r.clock.Set(Utc("2025-11-17T01:30:00Z"), 0);
        CHECK(!r.mgr.Stop().ok);
        RemoveDirectoryW(r.log.Path().c_str());
    }   // app closed with the completing state on disk
    Rig r2(dir.path());
    const SessionManager::RecoveryReport rep = r2.mgr.Recover();
    CHECK(rep.finishedPendingStop);
    CHECK(r2.mgr.State() == AppState::Idle);
    const auto recs = AllRecords(r2.log);
    CHECK_EQ(recs.size(), size_t(1));
    CHECK_EQ(recs[0].durationSeconds, int64_t(1800));
}

TEST(Session_FailedStopThenTypingKeepsCompletingAndRetryReusesEnd) {
    TempDir dir;
    Rig r(dir.path());
    CHECK(CreateDirectoryW(r.log.Path().c_str(), nullptr) != 0);   // every log operation fails

    r.clock.Set(Utc("2025-11-17T01:00:00Z"), 480);
    CHECK(r.mgr.Start(L"orig").ok);
    const std::string id = r.mgr.Active()->id;
    r.clock.Set(Utc("2025-11-17T01:30:00Z"), 480);
    const Status failed = r.mgr.Stop();
    CHECK(!failed.ok);
    CHECK(failed.message.find(L"09:30:00") != std::wstring::npos);        // local end time (UTC+8)
    CHECK(failed.message.find(L"Stop again") != std::wstring::npos);
    CHECK(r.mgr.State() == AppState::Recording);                          // UI can press Stop again
    CHECK(r.mgr.Active() != nullptr);
    CHECK(r.mgr.Active()->status == ActiveStatus::Completing);
    CHECK_EQ(*r.mgr.Active()->pendingEndUtc, Utc("2025-11-17T01:30:00Z"));

    // Typing after the failed Stop must not turn the on-disk completing state back into a recording.
    r.clock.Set(Utc("2025-11-17T01:45:00Z"), 480);
    CHECK(r.mgr.UpdateNotes(L"x").ok);
    const StateStore::LoadResult lr = r.state.Load();
    CHECK(lr.kind == StateStore::LoadKind::Loaded);
    CHECK(lr.state.status == ActiveStatus::Completing);
    CHECK_EQ(lr.state.id, id);
    CHECK(lr.state.notes == L"x");
    CHECK_EQ(*lr.state.pendingEndUtc, Utc("2025-11-17T01:30:00Z"));
    CHECK_EQ(*lr.state.pendingEndOffsetMin, 480);

    // Remove the obstacle; a later clock must not leak into the retried Stop.
    RemoveDirectoryW(r.log.Path().c_str());
    r.clock.Set(Utc("2025-11-17T02:00:00Z"), 480);
    const Status ok = r.mgr.Stop();
    CHECK(ok.ok);
    CHECK(ok.message.empty());
    CHECK(r.mgr.State() == AppState::Idle);
    CHECK(r.mgr.Active() == nullptr);
    CHECK(!FileExists(r.state.StatePath()));
    const auto recs = AllRecords(r.log);
    CHECK_EQ(recs.size(), size_t(1));
    CHECK_EQ(recs[0].id, id);
    CHECK_EQ(recs[0].endUtc, Utc("2025-11-17T01:30:00Z"));
    CHECK_EQ(recs[0].durationSeconds, int64_t(1800));
    CHECK(recs[0].notes == L"x");
}

TEST(Session_RetriedStopAfterAppendAlreadyLandedNoDuplicateAndWarns) {
    TempDir dir;
    Rig r(dir.path());
    CHECK(CreateDirectoryW(r.log.Path().c_str(), nullptr) != 0);
    r.clock.Set(Utc("2025-11-17T01:00:00Z"), 0);
    CHECK(r.mgr.Start(L"orig").ok);
    const std::string id = r.mgr.Active()->id;
    r.clock.Set(Utc("2025-11-17T01:30:00Z"), 0);
    CHECK(!r.mgr.Stop().ok);

    // Simulate the first Append having landed even though Stop reported failure (e.g. flush failed).
    RemoveDirectoryW(r.log.Path().c_str());
    CHECK(r.log.Append(CompletedFrom(Completing(id, Utc("2025-11-17T01:00:00Z"), Utc("2025-11-17T01:30:00Z"), L"orig"))).ok);

    CHECK(r.mgr.UpdateNotes(L"later edit").ok);
    r.clock.Set(Utc("2025-11-17T03:00:00Z"), 0);
    const Status st = r.mgr.Stop();
    CHECK(st.ok);
    CHECK(!st.message.empty());                       // warns that the later note edit was not saved
    CHECK(r.mgr.State() == AppState::Idle);
    CHECK(!FileExists(r.state.StatePath()));
    const auto recs = AllRecords(r.log);
    CHECK_EQ(recs.size(), size_t(1));                 // never a second record for the same id
    CHECK_EQ(recs[0].endUtc, Utc("2025-11-17T01:30:00Z"));
    CHECK(recs[0].notes == L"orig");
}

TEST(Session_RetriedStopAfterAppendAlreadyLandedSameNotesNoWarning) {
    TempDir dir;
    Rig r(dir.path());
    CHECK(CreateDirectoryW(r.log.Path().c_str(), nullptr) != 0);
    r.clock.Set(Utc("2025-11-17T01:00:00Z"), 0);
    CHECK(r.mgr.Start(L"same").ok);
    const std::string id = r.mgr.Active()->id;
    r.clock.Set(Utc("2025-11-17T01:30:00Z"), 0);
    CHECK(!r.mgr.Stop().ok);
    RemoveDirectoryW(r.log.Path().c_str());
    CHECK(r.log.Append(CompletedFrom(Completing(id, Utc("2025-11-17T01:00:00Z"), Utc("2025-11-17T01:30:00Z"), L"same"))).ok);

    const Status st = r.mgr.Stop();
    CHECK(st.ok);
    CHECK(st.message.empty());
    CHECK(r.mgr.State() == AppState::Idle);
    CHECK_EQ(AllRecords(r.log).size(), size_t(1));
}

TEST(Session_UnreadableStateBlocksStartAndIsNotQuarantined) {
    TempDir dir;
    ActiveState a;
    a.id = "locked-session";
    a.startUtc = Utc("2025-11-17T01:00:00Z");
    a.notes = L"precious";
    std::string before;
    {
        Rig seed(dir.path());
        CHECK(seed.state.Save(a).ok);
        before = ReadRaw(seed.state.StatePath());
    }

    Rig r(dir.path());
    HANDLE h = HoldExclusive(r.state.StatePath());
    const SessionManager::RecoveryReport rep = r.mgr.Recover();
    CHECK(!rep.error.empty());
    CHECK(rep.error.find(L"Could not read TinyLog.state") != std::wstring::npos);
    CHECK(!rep.quarantined);
    CHECK(!rep.restoredRecording);
    CHECK(r.mgr.State() == AppState::Idle);
    CHECK(!r.mgr.Start(L"must not overwrite").ok);
    CHECK(r.mgr.State() == AppState::Idle);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

    CHECK(FileExists(r.state.StatePath()));
    CHECK_EQ(ReadRaw(r.state.StatePath()), before);   // untouched, not renamed, not overwritten

    // Restarting TinyLog (a fresh Recover) once the lock is gone restores the session.
    Rig r2(dir.path());
    const SessionManager::RecoveryReport rep2 = r2.mgr.Recover();
    CHECK(rep2.restoredRecording);
    CHECK(rep2.error.empty());
    CHECK(r2.mgr.Notes() == L"precious");
}

TEST(Session_UnreadableTmpWithMissingStateBlocksStart) {
    TempDir dir;
    Rig r(dir.path());
    ActiveState a;
    a.id = "tmp-locked";
    a.startUtc = Utc("2025-11-17T01:00:00Z");
    const std::wstring tmp = dir.File(L"TinyLog.state.tmp");
    WriteRaw(tmp, SerializeActive(a));

    HANDLE h = HoldExclusive(tmp);
    const SessionManager::RecoveryReport rep = r.mgr.Recover();
    CHECK(!rep.error.empty());
    CHECK(!rep.quarantined);
    CHECK(r.mgr.State() == AppState::Idle);
    CHECK(!r.mgr.Start(L"x").ok);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    CHECK(!FileExists(r.state.StatePath()));
    CHECK(FileExists(tmp));
}
