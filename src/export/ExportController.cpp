// Export flow (Spec 7.8): temp file next to the destination, then atomic replace.
#include "export/ExportController.h"

#include "export/SpreadsheetWriter.h"
#include "export/ZipWriter.h"
#include "util/Handles.h"

namespace {

Status BuildWorkbook(JsonLinesStore& log, HANDLE file, size_t& rows) {
    ZipWriter zip;
    Status st = zip.Open(file);
    if (!st) return st;
    st = WriteStaticParts(zip);
    if (!st) return st;

    SheetStream sheet(zip);
    st = sheet.Begin();
    if (!st) return st;

    Status writeError = Status::Ok();
    const Status scan = log.ForEach([&](const CompletedSession& s) {
        writeError = sheet.AddSession(s);
        if (!writeError) return false;
        ++rows;
        return true;
    });
    if (!writeError) return writeError;
    if (!scan) return scan;

    st = sheet.End();
    if (!st) return st;
    st = zip.Finish();
    if (!st) return st;

    if (!FlushFileBuffers(file)) return Status::FromLastError(L"Flush export file");
    return Status::Ok();
}

}  // namespace

Status ExportWorkbook(JsonLinesStore& log, const std::wstring& destPath, size_t& rowsWritten) {
    rowsWritten = 0;
    if (destPath.empty()) return Status::Fail(L"Export: empty destination path");

    const std::wstring tmpPath = destPath + L".tmp-" + std::to_wstring(GetCurrentProcessId());

    size_t rows = 0;
    Status result = Status::Ok();
    {
        UniqueHandle file(CreateFileW(tmpPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file.valid()) return Status::FromLastError(L"Create export file");

        result = BuildWorkbook(log, file.get(), rows);
        if (result) {
            // Close before the rename; report close errors via the handle being valid until here.
            if (!CloseHandle(file.get())) {
                result = Status::FromLastError(L"Close export file");
            }
            file.release();
        }
    }

    if (!result) {
        DeleteFileW(tmpPath.c_str());
        return result;
    }

    if (!MoveFileExW(tmpPath.c_str(), destPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        result = Status::FromLastError(L"Save export file");
        DeleteFileW(tmpPath.c_str());
        return result;
    }

    rowsWritten = rows;
    return Status::Ok();
}
