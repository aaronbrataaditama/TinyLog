#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: SessionManager.cpp (WP-C).
// UI-thread only, fully synchronous (Spec 5.4-5.6, 8.1, 8.7).
#include <optional>
#include <string>
#include "app/AppState.h"
#include "app/Clock.h"
#include "storage/JsonLinesStore.h"
#include "storage/StateStore.h"
#include "util/Status.h"

class SessionManager {
public:
    SessionManager(StateStore& state, JsonLinesStore& log, Clock& clock);

    struct RecoveryReport {
        bool         restoredRecording = false;   // valid recording state restored
        bool         finishedPendingStop = false; // completing state resumed (appended or already present)
        bool         quarantined = false;         // invalid state renamed aside
        std::wstring quarantinePath;
        std::wstring error;                       // non-empty -> surface to the user
        std::wstring idleNotes;                   // when ending Idle: notes of the last completed session
    };
    // RepairTail, then Spec 5.6 recovery matrix. Never overwrites an invalid state file.
    RecoveryReport Recover();

    // Idle only (else Fail). Idle -> Starting -> Save(recording) -> Recording. On failure: back to Idle.
    Status Start(const std::wstring& notes);

    // Recording: store + Save immediately (Fail if save fails; in-memory notes still updated).
    // Idle: memory only, returns Ok. Starting/Completing: memory only, returns Ok.
    Status UpdateNotes(const std::wstring& notes);

    // Recording only. Spec 5.5 steps 1-8. If anything fails after the completing state is saved, the
    // completing state stays on disk, State() returns to Recording (Stop can be pressed again), Active()
    // is that completing state (status Completing, pendingEnd* set) and Fail is returned ("ended at
    // HH:MM:SS ... press Stop again"). UpdateNotes then re-saves it as completing; a retried Stop reuses
    // the persisted end time and never appends a second record for the same id. A successful Stop whose
    // record was already in the log but with different notes returns Ok with a warning in Status.message.
    // Notes are retained after a successful Stop (user decision).
    Status Stop();

    AppState            State() const { return state_; }
    const ActiveState*  Active() const { return active_ ? &*active_ : nullptr; }   // nullptr when Idle
    const std::wstring& Notes() const { return notes_; }

private:
    StateStore&                stateStore_;
    JsonLinesStore&            log_;
    Clock&                     clock_;
    AppState                   state_ = AppState::Idle;
    std::optional<ActiveState> active_;
    std::wstring               notes_;
    // Set by Recover() when an invalid/pending state file is still on disk and could not be moved aside
    // or finished: Start() then refuses, so that file is never overwritten.
    std::wstring               startBlockedReason_;
};
