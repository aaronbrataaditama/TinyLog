// Active-session state file (WP-C). Spec 5.3, 5.6. Invalid files are quarantined, never deleted.
#include "storage/StateStore.h"

#include <windows.h>

#include <cstdio>
#include <utility>

#include "util/FileHelpers.h"
#include "util/StringHelpers.h"

namespace {

// IoError: the file exists (or may exist) but could not be read; says nothing about its content.
enum class FileKind { Missing, Valid, Invalid, IoError };

constexpr int   kReadAttempts = 3;
constexpr DWORD kReadRetryDelayMs = 100;

// Reads and validates a state file. why receives a human-readable reason when Invalid / IoError.
// A read failure is retried briefly (e.g. a sharing violation from a scanner or sync client) and is never
// treated as invalid content.
FileKind Inspect(const std::wstring& path, ActiveState& out, std::wstring& why) {
    std::string bytes;
    Status st = ReadWholeFile(path, bytes);
    for (int attempt = 1; !st && attempt < kReadAttempts; ++attempt) {
        if (st.code == ERROR_FILE_NOT_FOUND || st.code == ERROR_PATH_NOT_FOUND) break;
        Sleep(kReadRetryDelayMs);
        st = ReadWholeFile(path, bytes);
    }
    if (!st) {
        if (st.code == ERROR_FILE_NOT_FOUND || st.code == ERROR_PATH_NOT_FOUND) return FileKind::Missing;
        why = st.message;
        return FileKind::IoError;
    }
    // Tolerate a UTF-8 BOM (e.g. after a manual edit).
    std::string_view text = bytes;
    if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    if (!ParseActive(text, out)) {
        why = L"content is not a valid TinyLog session state";
        return FileKind::Invalid;
    }
    return FileKind::Valid;
}

}  // namespace

StateStore::StateStore(std::wstring dir)
    : dir_(std::move(dir)), statePath_(dir_ + L"\\TinyLog.state"), tmpPath_(dir_ + L"\\TinyLog.state.tmp") {}

StateStore::LoadResult StateStore::Load() {
    LoadResult r;

    ActiveState stateVal;
    std::wstring stateWhy;
    const FileKind state = Inspect(statePath_, stateVal, stateWhy);

    if (state == FileKind::IoError) {
        // Unreadable, not invalid: touch nothing (no promote, no quarantine, no stale-tmp cleanup).
        r.kind = LoadKind::IoError;
        r.diagnostic = stateWhy;   // "open <path>: <system message>"
        return r;
    }

    if (state == FileKind::Valid) {
        // A stray .tmp next to a valid state is stale; remove it (best effort).
        if (FileExists(tmpPath_)) DeleteFileW(tmpPath_.c_str());
        r.kind = LoadKind::Loaded;
        r.state = std::move(stateVal);
        return r;
    }

    ActiveState tmpVal;
    std::wstring tmpWhy;
    const FileKind tmp = Inspect(tmpPath_, tmpVal, tmpWhy);

    if (tmp == FileKind::IoError) {
        // The .tmp may hold the only copy of the session: do not guess, do not quarantine the state file.
        r.kind = LoadKind::IoError;
        r.diagnostic = tmpWhy;   // names TinyLog.state.tmp
        return r;
    }

    if (tmp == FileKind::Valid) {
        std::wstring note = L"recovered the session from TinyLog.state.tmp";
        if (state == FileKind::Invalid) {
            std::wstring movedTo;
            const Status q = QuarantineInvalid(movedTo);
            if (!q) {
                // Promoting would overwrite the invalid file; leave everything untouched.
                r.kind = LoadKind::Invalid;
                r.diagnostic = L"TinyLog.state is invalid (" + stateWhy +
                               L") and could not be moved aside: " + q.message;
                return r;
            }
            note += L"; the invalid TinyLog.state was preserved as " + movedTo;
        }
        if (!MoveFileExW(tmpPath_.c_str(), statePath_.c_str(), MOVEFILE_WRITE_THROUGH)) {
            note += L"; could not promote it to TinyLog.state: " + Status::FromLastError(L"MoveFileExW").message;
        }
        r.kind = LoadKind::PromotedTmp;
        r.state = std::move(tmpVal);
        r.diagnostic = std::move(note);
        return r;
    }

    if (state == FileKind::Invalid) {
        r.kind = LoadKind::Invalid;
        r.diagnostic = L"TinyLog.state is invalid: " + stateWhy;
        return r;
    }
    r.kind = LoadKind::None;   // state missing; an invalid .tmp is left in place
    return r;
}

Status StateStore::Save(const ActiveState& s) { return WriteFileAtomic(statePath_, SerializeActive(s)); }

Status StateStore::Remove() {
    if (DeleteFileW(statePath_.c_str())) return Status::Ok();
    const DWORD e = GetLastError();
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return Status::Ok();
    SetLastError(e);
    return Status::FromLastError(L"delete " + statePath_);
}

Status StateStore::QuarantineInvalid(std::wstring& movedTo) {
    movedTo.clear();
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t stamp[32];
    swprintf_s(stamp, L".corrupt-%04u%02u%02u-%02u%02u%02u", static_cast<unsigned>(t.wYear),
               static_cast<unsigned>(t.wMonth), static_cast<unsigned>(t.wDay), static_cast<unsigned>(t.wHour),
               static_cast<unsigned>(t.wMinute), static_cast<unsigned>(t.wSecond));
    const std::wstring baseName = statePath_ + stamp;

    for (unsigned n = 1; n <= 1000; ++n) {
        std::wstring candidate = baseName;
        if (n > 1) candidate += L"-" + std::to_wstring(n);
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        // No MOVEFILE_REPLACE_EXISTING: never overwrite anything.
        if (MoveFileExW(statePath_.c_str(), candidate.c_str(), MOVEFILE_WRITE_THROUGH)) {
            movedTo = candidate;
            return Status::Ok();
        }
        const DWORD e = GetLastError();
        if (e != ERROR_ALREADY_EXISTS && e != ERROR_FILE_EXISTS) {
            SetLastError(e);
            return Status::FromLastError(L"quarantine " + statePath_);
        }
    }
    return Status::Fail(L"quarantine " + statePath_ + L": no free name");
}
