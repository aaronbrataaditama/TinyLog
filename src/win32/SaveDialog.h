#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: SaveDialog.cpp (WP-E). Spec 7.7.
#include <windows.h>
#include <string>
#include "util/Status.h"

enum class SaveOutcome { Ok, Cancelled, Failed };

// IFileSaveDialog: title "Export TinyLog sessions", filter "Excel Workbook (*.xlsx)", default extension
// "xlsx", default name "TinyLog_YYYY-MM-DD.xlsx" (local date), FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT |
// FOS_PATHMUSTEXIST. ERROR_CANCELLED -> Cancelled. Requires COM initialized on the calling thread.
SaveOutcome ShowExportSaveDialog(HWND owner, std::wstring& path, Status& error);
