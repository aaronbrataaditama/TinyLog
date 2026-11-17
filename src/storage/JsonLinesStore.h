#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: JsonLinesStore.cpp (WP-C).
// File: <dir>\TinyLog.jsonl, one SerializeCompleted line + '\n' per session (Spec 5.2, 5.5).
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include "storage/SessionRecord.h"
#include "util/Status.h"

class JsonLinesStore {
public:
    explicit JsonLinesStore(std::wstring path);

    // If the file does not end with '\n', or its last line does not parse, truncate the file to the end
    // of the last valid '\n'-terminated line. Earlier lines are never touched. Missing file = Ok.
    Status RepairTail();

    // Streaming scan (64 KB chunks) for a record with this id. Missing file -> found=false, Ok.
    Status Contains(const std::string& id, bool& found);

    // RepairTail, then open with FILE_APPEND_DATA (OPEN_ALWAYS), write line + '\n', FlushFileBuffers.
    Status Append(const CompletedSession& s);

    // Streams records in file order. Unparsable lines are skipped and counted. cb returns false to stop.
    // Missing file = Ok with zero callbacks.
    Status ForEach(const std::function<bool(const CompletedSession&)>& cb, size_t* skippedMalformed = nullptr);

    // Last valid record (scans backwards from the end in chunks). Empty/missing -> nullopt, Ok.
    Status ReadLast(std::optional<CompletedSession>& last);

    const std::wstring& Path() const { return path_; }

private:
    std::wstring path_;
};
