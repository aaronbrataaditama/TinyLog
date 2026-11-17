// Spec 7.7. Native IFileSaveDialog.
#include "win32/SaveDialog.h"

#include <shobjidl.h>
#include <wrl/client.h>

#include "util/ComHelpers.h"

using Microsoft::WRL::ComPtr;

SaveOutcome ShowExportSaveDialog(HWND owner, std::wstring& path, Status& error) {
    path.clear();
    error = Status::Ok();

    ComPtr<IFileSaveDialog> dlg;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (FAILED(hr)) { error = Status::FromHr(hr, L"Create save dialog"); return SaveOutcome::Failed; }

    DWORD options = 0;
    hr = dlg->GetOptions(&options);
    if (FAILED(hr)) { error = Status::FromHr(hr, L"Save dialog GetOptions"); return SaveOutcome::Failed; }
    hr = dlg->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST);
    if (FAILED(hr)) { error = Status::FromHr(hr, L"Save dialog SetOptions"); return SaveOutcome::Failed; }

    hr = dlg->SetTitle(L"Export TinyLog sessions");
    if (FAILED(hr)) { error = Status::FromHr(hr, L"Save dialog SetTitle"); return SaveOutcome::Failed; }

    const COMDLG_FILTERSPEC filter[] = {{L"Excel Workbook (*.xlsx)", L"*.xlsx"}};
    hr = dlg->SetFileTypes(1, filter);
    if (FAILED(hr)) { error = Status::FromHr(hr, L"Save dialog SetFileTypes"); return SaveOutcome::Failed; }
    hr = dlg->SetFileTypeIndex(1);
    if (FAILED(hr)) { error = Status::FromHr(hr, L"Save dialog SetFileTypeIndex"); return SaveOutcome::Failed; }
    hr = dlg->SetDefaultExtension(L"xlsx");
    if (FAILED(hr)) { error = Status::FromHr(hr, L"Save dialog SetDefaultExtension"); return SaveOutcome::Failed; }

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t name[64];
    swprintf_s(name, L"TinyLog_%04u-%02u-%02u.xlsx", static_cast<unsigned>(st.wYear),
               static_cast<unsigned>(st.wMonth), static_cast<unsigned>(st.wDay));
    hr = dlg->SetFileName(name);
    if (FAILED(hr)) { error = Status::FromHr(hr, L"Save dialog SetFileName"); return SaveOutcome::Failed; }

    hr = dlg->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return SaveOutcome::Cancelled;
    if (FAILED(hr)) { error = Status::FromHr(hr, L"Show save dialog"); return SaveOutcome::Failed; }

    ComPtr<IShellItem> item;
    hr = dlg->GetResult(&item);
    if (FAILED(hr)) { error = Status::FromHr(hr, L"Save dialog GetResult"); return SaveOutcome::Failed; }

    CoTaskMemString display;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, display.put());
    if (FAILED(hr) || !display.get()) {
        error = Status::FromHr(FAILED(hr) ? hr : E_FAIL, L"Save dialog GetDisplayName");
        return SaveOutcome::Failed;
    }

    path = display.get();
    return SaveOutcome::Ok;
}
