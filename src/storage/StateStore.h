#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: StateStore.cpp (WP-C).
// Files: <dir>\TinyLog.state, <dir>\TinyLog.state.tmp (Spec 5.3).
#include <string>
#include "storage/SessionRecord.h"
#include "util/Status.h"

class StateStore {
public:
    explicit StateStore(std::wstring dir);

    // IoError: a state file exists but could not be read (sharing violation, access denied, ...) even after
    // retries. Nothing was moved, promoted or deleted; the caller must not start a new session.
    enum class LoadKind { None, Loaded, PromotedTmp, Invalid, IoError };
    struct LoadResult {
        LoadKind     kind = LoadKind::None;
        ActiveState  state;        // valid for Loaded / PromotedTmp
        std::wstring diagnostic;   // human-readable reason for Invalid / PromotedTmp / IoError (IoError: the read failure itself)
    };

    // - state valid                          -> Loaded (a stray .tmp is deleted)
    // - state missing/invalid, .tmp valid    -> .tmp promoted to state (invalid state first quarantined) -> PromotedTmp
    // - state missing, no valid .tmp         -> None (an invalid .tmp is left in place)
    // - state invalid, no valid .tmp         -> Invalid (file untouched)
    // - state (or, with state missing, .tmp) unreadable for I/O reasons after 3 attempts -> IoError (untouched)
    LoadResult Load();

    Status Save(const ActiveState& s);   // WriteFileAtomic(SerializeActive(s))
    Status Remove();                     // delete state file; missing = Ok
    // Rename TinyLog.state -> TinyLog.state.corrupt-YYYYMMDD-HHMMSS (local time). Never deletes.
    Status QuarantineInvalid(std::wstring& movedTo);

    const std::wstring& StatePath() const { return statePath_; }

private:
    std::wstring dir_;
    std::wstring statePath_;
    std::wstring tmpPath_;
};
