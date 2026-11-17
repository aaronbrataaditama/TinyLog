#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: ExportController.cpp (WP-D). Spec 7.8.
#include <cstddef>
#include <string>
#include "storage/JsonLinesStore.h"
#include "util/Status.h"

// Writes "<destPath>.tmp-<pid>" in the destination directory: static parts + streamed sheet (records in
// log order, malformed lines skipped), FlushFileBuffers, close, MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH).
// On any failure the temp file is deleted and destPath is untouched. Never modifies the log.
Status ExportWorkbook(JsonLinesStore& log, const std::wstring& destPath, size_t& rowsWritten);
