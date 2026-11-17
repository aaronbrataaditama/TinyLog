// Session state machine (WP-C). Spec 5.4-5.6, 8.1, 8.3, 8.7. UI-thread only, fully synchronous.
#include "app/SessionManager.h"

#include <algorithm>
#include <utility>

#include "storage/SessionRecord.h"
#include "util/TimeHelpers.h"

namespace {

// Builds the completed record from a validated completing state (Spec 5.5 step 3).
CompletedSession BuildCompleted(const ActiveState& a) {
    CompletedSession c;
    c.id = a.id;
    c.startUtc = a.startUtc;
    c.endUtc = a.pendingEndUtc.value_or(a.startUtc);
    c.startOffsetMin = a.startOffsetMin;
    c.endOffsetMin = a.pendingEndOffsetMin.value_or(a.startOffsetMin);
    c.durationSeconds = std::max<int64_t>(0, c.endUtc - c.startUtc);
    c.notes = a.notes;
    return c;
}

// Spec 5.5 steps 4-6: append only if the id is not already in the log. Ok means the record is durable.
Status CommitCompleted(JsonLinesStore& log, const CompletedSession& c) {
    bool found = false;
    Status st = log.Contains(c.id, found);
    if (!st) return st;
    if (found) return Status::Ok();
    return log.Append(c);
}

}  // namespace

SessionManager::SessionManager(StateStore& state, JsonLinesStore& log, Clock& clock)
    : stateStore_(state), log_(log), clock_(clock) {}

SessionManager::RecoveryReport SessionManager::Recover() {
    RecoveryReport rep;
    state_ = AppState::Idle;
    active_.reset();
    startBlockedReason_.clear();

    const Status repair = log_.RepairTail();
    if (!repair) rep.error = L"Could not repair the session log: " + repair.message;

    auto addError = [&rep](const std::wstring& msg) {
        if (!rep.error.empty()) rep.error += L"\n";
        rep.error += msg;
    };
    auto loadIdleNotes = [&]() {
        std::optional<CompletedSession> last;
        const Status st = log_.ReadLast(last);
        if (!st) {
            addError(L"Could not read the session log: " + st.message);
        } else if (last) {
            rep.idleNotes = last->notes;
        }
        notes_ = rep.idleNotes;
    };

    StateStore::LoadResult lr = stateStore_.Load();
    switch (lr.kind) {
    case StateStore::LoadKind::None:
        loadIdleNotes();
        break;

    case StateStore::LoadKind::Loaded:
    case StateStore::LoadKind::PromotedTmp: {
        if (lr.kind == StateStore::LoadKind::PromotedTmp && !lr.diagnostic.empty()) addError(lr.diagnostic);

        if (lr.state.status == ActiveStatus::Recording) {
            notes_ = lr.state.notes;
            active_ = std::move(lr.state);
            state_ = AppState::Recording;
            rep.restoredRecording = true;
            break;
        }

        // Completing: finish the idempotent transaction using the stored end timestamp.
        const CompletedSession c = BuildCompleted(lr.state);
        const Status commit = CommitCompleted(log_, c);
        if (!commit) {
            addError(L"A finished session could not be saved to the log and is kept for the next attempt: " +
                     commit.message);
            startBlockedReason_ = L"A pending session is still waiting to be saved to the log.";
            loadIdleNotes();
            break;
        }
        rep.finishedPendingStop = true;
        const Status rm = stateStore_.Remove();
        if (!rm) addError(L"The session was saved, but the state file could not be removed: " + rm.message);
        loadIdleNotes();
        break;
    }

    case StateStore::LoadKind::IoError:
        // Unreadable is not invalid: nothing is quarantined, promoted or overwritten.
        addError(L"Could not read TinyLog.state: " + lr.diagnostic +
                 L" TinyLog will not start a new session until this is resolved; restart TinyLog to retry.");
        startBlockedReason_ = L"The saved session state could not be read.";
        loadIdleNotes();
        break;

    case StateStore::LoadKind::Invalid: {
        std::wstring movedTo;
        const Status q = stateStore_.QuarantineInvalid(movedTo);
        if (q) {
            rep.quarantined = true;
            rep.quarantinePath = movedTo;
            addError(L"The saved session state was invalid and was preserved as " + movedTo + L". " + lr.diagnostic);
        } else {
            addError(L"The saved session state is invalid and could not be moved aside (" + q.message + L"). " +
                     lr.diagnostic);
            startBlockedReason_ = L"An invalid session state file is still present.";
        }
        loadIdleNotes();
        break;
    }
    }
    return rep;
}

Status SessionManager::Start(const std::wstring& notes) {
    if (state_ != AppState::Idle) return Status::Fail(L"A session is already in progress.");
    notes_ = notes;
    if (!startBlockedReason_.empty())
        return Status::Fail(startBlockedReason_ + L" Resolve it and restart TinyLog.");

    state_ = AppState::Starting;
    const Timestamp ts = clock_.Now();

    ActiveState a;
    a.id = NewSessionId();
    a.status = ActiveStatus::Recording;
    a.startUtc = ts.utc;
    a.startOffsetMin = ts.offsetMinutes;
    a.notes = notes;

    const Status st = stateStore_.Save(a);
    if (!st) {
        state_ = AppState::Idle;
        active_.reset();
        return st;
    }
    active_ = std::move(a);
    state_ = AppState::Recording;
    return Status::Ok();
}

Status SessionManager::UpdateNotes(const std::wstring& notes) {
    notes_ = notes;
    if (state_ != AppState::Recording || !active_) return Status::Ok();
    active_->notes = notes;
    // After a failed Stop, active_ is the completing state that is on disk (pendingEnd* kept): saving it
    // with the new notes never turns it back into a recording.
    return stateStore_.Save(*active_);
}

Status SessionManager::Stop() {
    if (state_ != AppState::Recording || !active_) return Status::Fail(L"No session is recording.");
    state_ = AppState::Completing;

    // 1-2. Capture the end and persist it as a completing transaction. A retried Stop (active_ is already
    // the persisted completing state after a failure) reuses the persisted end time: the session ended
    // when the user first pressed Stop.
    const bool retry = active_->status == ActiveStatus::Completing && active_->pendingEndUtc &&
                       active_->pendingEndOffsetMin;
    ActiveState completing = *active_;
    completing.notes = notes_;
    if (!retry) {
        const Timestamp ts = clock_.Now();
        completing.status = ActiveStatus::Completing;
        completing.pendingEndUtc = ts.utc;
        completing.pendingEndOffsetMin = ts.offsetMinutes;
    }

    // After any failure from here on, the completing state is what is (or may be) on disk. Keep it in
    // memory so later typing re-saves it as completing, but let the UI press Stop again.
    auto failEnded = [&](const Status& why) {
        active_ = completing;
        state_ = AppState::Recording;
        const std::string local = FormatLocal(*completing.pendingEndUtc, *completing.pendingEndOffsetMin);
        std::wstring hms;
        if (local.size() >= 19) hms.assign(local.begin() + 11, local.begin() + 19);
        return Status::Fail(L"The session ended at " + hms + L" but could not be saved to the log: " +
                                why.message + L" Press Stop again to retry saving it.",
                            why.code);
    };

    const Status saved = stateStore_.Save(completing);
    if (!saved) {
        if (retry) return failEnded(saved);
        state_ = AppState::Recording;   // nothing changed on disk; the recording continues
        return saved;
    }

    // 3-6. Build the record; append unless already present (never a second record for the same id).
    const CompletedSession c = BuildCompleted(completing);
    bool found = false;
    CompletedSession existing;
    const Status scan = log_.ForEach([&](const CompletedSession& r) {
        if (r.id != c.id) return true;
        found = true;
        existing = r;
        return false;
    });
    if (!scan) return failEnded(scan);
    if (!found) {
        const Status appended = log_.Append(c);
        if (!appended) return failEnded(appended);   // completing state stays on disk for recovery / retry
    }

    // 7. The record is durable. A failed cleanup is finished by the next Recover (completing + present).
    const Status removed = stateStore_.Remove();

    // 8. Idle; notes stay in the box.
    active_.reset();
    state_ = AppState::Idle;

    Status result = Status::Ok();
    if (found && existing.notes != c.notes) {
        result.message = L"The session was already saved by the earlier Stop attempt; note edits made after that "
                         L"attempt could not be added to the saved record.";
    }
    if (!removed) {
        if (!result.message.empty()) result.message += L"\n";
        result.message += L"The session was saved, but the state file could not be removed: " + removed.message;
    }
    return result;
}
