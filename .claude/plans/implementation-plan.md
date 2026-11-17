# TinyLog — Parallel Implementation Plan

## Context

The repo started empty (no commits, no remote). We are building **TinyLog.exe**, a portable, tray-only Windows work logger, from the spec `.claude/plans/TinyLog_Technical_Design_UltraLight.md` (the "Spec", cited as §n) and the dark-bubble mockup (status dot + "TinyLog" title, green timer pill `00:12:34`, multiline notes box, green **Stop** button, grey **Export to Excel** button, pointer toward a green clock tray icon).

Stack is fixed by the Spec: **C++20 + Win32 + GDI, MSVC x64, /MT, no third-party libraries.** The goal of this plan is to let cheaper models (Sonnet, Haiku) implement work packages (WPs) **in parallel**, so the plan freezes every cross-module interface up front (Wave 0) and gives each WP exclusive file ownership.

Decided by the user: **notes stay in the box after Stop** (not cleared). Consequence: at startup in Idle, prefill the notes box with the notes of the last completed session; edits typed while Idle live in memory only and become durable when Start is pressed.

Toolchain verified on this machine: VS 18 Enterprise (`C:\Program Files\Microsoft Visual Studio\18\Enterprise`), MSVC 14.51.36231, bundled CMake + Ninja, Windows SDK 10.0.26100.0. Nothing is on PATH until `vcvars64.bat` runs.

---

## Step 0 — Project bootstrap (first action after approval, before Wave 0)

1. `git checkout -b feat/tinylog-v1` (unborn repo → integration branch; never commit to main/master).
2. Create `.claude/` in the repo root with:
   - `CLAUDE.md` (project root `CLAUDE.md` too, short, pointing to `.claude/`): project purpose, fixed stack (C++20/Win32/GDI, /MT, no third-party libs), build/test commands (`cmd //c 'toolsuild.cmd debug'|release`), toolchain paths, git rules (work on `feat/tinylog-v1` / `wp/*` branches, no push, no remote), WP-agent rules (file ownership, frozen contracts, no registry APIs/threads/exceptions), pointers to the Spec, `.claude/plans/implementation-plan.md` and `.claude/PROGRESS.md`. Keep it under ~60 lines.
   - `plans/implementation-plan.md` — this plan, verbatim.
   - `PROGRESS.md` — tracker: one table row per WP (W0, A, B, E, F2, F1, C, D, H, R) with columns Wave · Model · Status (todo/in-progress/review/merged) · Branch · Commit · Tests · Notes; a "Contract changes" log; a "Decisions" log (seeded with: notes kept after Stop; TrayHost folded into App; invalid state quarantined; no sort on export); a "Spec §12 acceptance" checklist copied from the Spec. Orchestrator updates it after every merge.
3. `.gitignore` per W0 (keep `.claude/settings.local.json` ignored; commit the rest of `.claude/`).
4. Commit `chore: bootstrap project docs and plan` on `feat/tinylog-v1`.

---

## Execution model

| Wave | WPs (parallel within a wave) | Model | Gate to next wave |
|---|---|---|---|
| 0 | W0 Contracts & skeleton | **Opus** (orchestrator) | Debug+Release build green with stubs; committed |
| 1 | A Utils · B Resources/tools · E Shell leaf · F2 Placement+animation math · F1 Bubble window | Haiku · Haiku · Sonnet · Haiku · Sonnet | Each WP: own tests pass; merged into integration branch; full build green |
| 2 | C Storage+Session · D Zip/XLSX/Export | Sonnet · Sonnet | Same |
| 3 | H App integration | Sonnet | App runs end-to-end |
| 4 | R Review & hardening | **Opus** review → Sonnet fixes | Spec §12 acceptance checklist |

**Git** (per global CLAUDE.md — never commit to main):
- W0 creates integration branch `feat/tinylog-v1` from the unborn repo (`git checkout -b feat/tinylog-v1`), commits the skeleton there.
- Each WP agent runs with `isolation: "worktree"` (branched from `feat/tinylog-v1`), commits only its owned files on its own branch `wp/<id>-<slug>`. The orchestrator merges WP branches into `feat/tinylog-v1` (`--no-ff`) after verifying. No remote exists; nobody pushes.

**Rules every WP agent receives verbatim:**
1. Read only this plan's "Contracts" section + your WP section + the Spec sections cited in your WP.
2. Edit **only** files listed as yours. Never edit headers under "Frozen contracts", `CMakeLists.txt`, `CMakePresets.json`, or `tests/TestHarness.h`. If a contract is wrong or insufficient, stop and report the exact proposed change — do not work around it. **Exception:** you may add/alter `private:` members and private helper declarations in the header of a class whose `.cpp` you own (public API, free-function signatures and constants stay frozen). File-local helpers go in an anonymous namespace in your `.cpp`.
3. No new dependencies, no exceptions thrown by our code, no RTTI use, no registry APIs (`Reg*`, `SHReg*`), no background threads.
4. Build/test with `cmd //c tools\\build.cmd debug` (Bash) — must end green, including the full test exe.
5. Commit on your branch with a message `WP-<id>: <summary>` + the Co-Authored-By trailer. Report: files changed, tests added/passing, any contract concerns.

---

## Wave 0 — Contracts & skeleton (Opus, sequential)

Creates everything below so every later WP compiles against stubs from day one.

### Repo layout (Spec §10.1, with adjustments)
```
.gitignore  (build/, out/, *.user, TinyLog.jsonl, TinyLog.state*)
CMakeLists.txt  CMakePresets.json  README.md
tools/build.cmd           # call vcvars64.bat, cmake --preset %1, cmake --build --preset %1, ctest --preset %1
resources/  app.manifest  TinyLog.rc  resource.h  idle.ico  recording.ico (placeholders; WP-B replaces)
src/main.cpp
src/app/      App.h/.cpp  AppState.h  SessionManager.h/.cpp  Clock.h
src/util/     Status.h  Handles.h  StringHelpers.h/.cpp  TimeHelpers.h/.cpp  Crc32.h/.cpp  ComHelpers.h  FileHelpers.h/.cpp
src/storage/  SessionRecord.h/.cpp  Json.h/.cpp  StateStore.h/.cpp  JsonLinesStore.h/.cpp
src/export/   ZipWriter.h/.cpp  SpreadsheetWriter.h/.cpp  ExportController.h/.cpp
src/win32/    SingleInstance.h/.cpp  TrayIcon.h/.cpp  TrayMenu.h/.cpp  StartupShortcut.h/.cpp  SaveDialog.h/.cpp
              BubbleWindow.h/.cpp  GdiRenderer.h/.cpp  NativeControls.h/.cpp  BubblePlacement.h/.cpp  LayeredAnimation.h/.cpp  Theme.h
tests/        TestHarness.h  TestMain.cpp  Util_Tests.cpp  Storage_Tests.cpp  Session_Tests.cpp  Zip_Tests.cpp
              Spreadsheet_Tests.cpp  Startup_Tests.cpp  Placement_Tests.cpp  Animation_Tests.cpp
demo/         BubbleDemo.cpp   # test-only exe to view the bubble without the full app (not shipped)
```
`TrayHost` from Spec §10.1 is folded into `App` (owner window + wndproc) to remove one contract.

### CMake targets
- `tinylog_core` (static lib): util/*, storage/*, export/*, app/SessionManager, win32/BubblePlacement, win32/LayeredAnimation, win32/StartupShortcut, win32/SaveDialog (everything testable / non-window).
- `tinylog_ui` (static lib): remaining win32/* + Theme.
- `TinyLog` (WIN32 exe): main.cpp, app/App.cpp, resources; links core+ui + `user32 gdi32 shell32 ole32 uuid comctl32 uxtheme`.
- `TinyLogTests` (console exe): tests/*.cpp + core; registered with CTest. `TinyLogTests.exe [substring-filter]`.
- `BubbleDemo` (WIN32 exe): demo/ + ui + core. Not part of release folder.
- Release flags (Spec §2.3): `/O1 /Gy /Gw /GL /GR- /EHsc /MT /permissive- /W4 /utf-8`, link `/LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /SUBSYSTEM:WINDOWS`, PDB external. Debug: `/MTd`. `UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00`.
- Post-build on Release `TinyLog`: `tools/check-size.ps1` fails the build if > 3,000,000 bytes (stub in W0, WP-B finalizes).
- Presets: `debug`, `release` (Ninja, `build/<preset>`), matching build/test presets.

### Test harness (`tests/TestHarness.h`)
Header-only: `TEST(Name) { ... }` self-registers; `CHECK(expr)`, `CHECK_EQ(a,b)`, `CHECK_STR(a,b)`; `TempDir` RAII helper creating a unique dir under `%TEMP%\TinyLogTests\` and deleting it recursively. `TestMain.cpp` runs all (or filtered) tests, prints failures with file:line, returns non-zero on failure. Each `*_Tests.cpp` starts empty.

### Frozen contracts (W0 writes these headers exactly; stubs return `Status::Fail(L"not implemented")`/defaults)

> **After W0, the headers in `src/` are the source of truth** — they refine the sketch below (e.g. private members, `UniqueIcon`, `CoTaskMemString`, `kTrayFallbackUid`, `ZipWriter::Write(std::string_view)`, `StatePath()`/`Path()` accessors). Read the real header before implementing.

```cpp
// util/Status.h
struct Status { bool ok = true; DWORD code = 0; std::wstring message;
  static Status Ok(); static Status Fail(std::wstring msg, DWORD code = 0);
  static Status FromLastError(std::wstring ctx);   // ctx + ": " + FormatMessage(GetLastError())
  static Status FromHr(HRESULT hr, std::wstring ctx);
  explicit operator bool() const { return ok; } };

// util/Handles.h  — RAII, move-only
class UniqueHandle;      // CloseHandle; treats nullptr and INVALID_HANDLE_VALUE as empty; get(), release(), reset()
template<class T> class UniqueGdi;   // DeleteObject (HFONT/HBRUSH/HPEN/HRGN/HBITMAP)
class UniqueDC;          // DeleteDC (memory DCs only)
class SelectGuard;       // SelectObject + restore previous on destruction
// COM: use Microsoft::WRL::ComPtr (<wrl/client.h>, header-only SDK)
// util/ComHelpers.h: class ComInit { ComInit(); ~ComInit(); HRESULT hr; }  // CoInitializeEx(APARTMENTTHREADED), balanced

// util/StringHelpers.h
std::string  Utf8FromWide(std::wstring_view);
bool         WideFromUtf8(std::string_view, std::wstring& out);   // false on invalid UTF-8
std::string  JsonEscape(std::string_view utf8);   // " \\ \b \f \n \r \t, other <0x20 as \u00XX; non-ASCII passes through
std::string  XmlEscape(std::string_view utf8);    // & < > " ' ; also strips XML-1.0-forbidden chars (<0x20 except \t \n \r, U+FFFE/U+FFFF)
std::wstring NormalizeNewlinesToLf(std::wstring_view);  // \r\n and lone \r -> \n
bool         StartsWithFormulaChar(std::wstring_view);  // = + - @ (informational; export always writes strings)

// util/TimeHelpers.h
struct Timestamp { int64_t utc = 0; int32_t offsetMinutes = 0; };   // utc = Unix seconds
int64_t      UnixFromFileTime(const FILETIME&);
Timestamp    CaptureNow();                       // GetSystemTimePreciseAsFileTime; offset = local - utc for that instant (SystemTimeToTzSpecificLocalTimeEx w/ dynamic TZ)
std::string  FormatIsoUtc(int64_t utc);          // "2026-11-17T01:00:00Z"
bool         ParseIsoUtc(std::string_view, int64_t& utc);   // exactly that format
std::string  FormatLocal(int64_t utc, int32_t offsetMin);    // "2026-11-17 09:00:00" of utc+offset
void         CivilFromDays(int64_t days, int& y, int& m, int& d);   // Howard Hinnant algorithms, days since 1970-01-01
int64_t      DaysFromCivil(int y, int m, int d);
int64_t      ExcelSerialFromCivil(int y, int m, int d);  // 1900 system: DaysFromCivil(y,m,d) - DaysFromCivil(1899,12,30); valid for >= 1900-03-01
std::wstring FormatElapsed(int64_t seconds);     // "HH:MM:SS", hours may exceed 99 ("123:04:05"); negative -> "00:00:00"

// util/Crc32.h
uint32_t Crc32Update(uint32_t crc, const void* data, size_t len);   // start with 0; standard reflected 0xEDB88320, table built at first use or constexpr

// util/FileHelpers.h
Status   GetExeDirectory(std::wstring& dir);      // GetModuleFileNameW with growing buffer; dir w/o trailing slash
Status   GetExePath(std::wstring& path);
Status   WriteFileAtomic(const std::wstring& target, std::string_view bytes);  // Spec §5.3 steps 1-7: <target>.tmp, WriteFile loop, FlushFileBuffers, close, ReplaceFileW if exists else MoveFileExW(MOVEFILE_WRITE_THROUGH); used by StateStore and ExportController
Status   ReadWholeFile(const std::wstring& path, std::string& bytes);  // ERROR_FILE_NOT_FOUND -> Status with code set
bool     FileExists(const std::wstring&);

// storage/SessionRecord.h
struct CompletedSession { std::string id; int64_t startUtc=0, endUtc=0; int32_t startOffsetMin=0, endOffsetMin=0;
                          int64_t durationSeconds=0; std::wstring notes; };
enum class ActiveStatus { Recording, Completing };
struct ActiveState { std::string id; ActiveStatus status = ActiveStatus::Recording; int64_t startUtc=0; int32_t startOffsetMin=0;
                     std::wstring notes; std::optional<int64_t> pendingEndUtc; std::optional<int32_t> pendingEndOffsetMin; };
std::string SerializeCompleted(const CompletedSession&);      // one line, no '\n'; exact field set of Spec §5.2 incl. schema:1, *Local strings
bool        ParseCompleted(std::string_view line, CompletedSession&);
std::string SerializeActive(const ActiveState&);              // Spec §5.3 fields; status "recording"/"completing"; nulls when absent
bool        ParseActive(std::string_view, ActiveState&);      // rejects schema != 1, missing id/startUtc, completing w/o pendingEnd*
std::string NewSessionId();                                    // CoCreateGuid, lowercase "xxxxxxxx-xxxx-..." no braces

// storage/Json.h  — minimal flat-object codec, only what the two schemas need
struct JsonValue { enum Kind { Null, Number, String } kind = Null; int64_t num = 0; std::string str; };
bool ParseFlatJsonObject(std::string_view, std::vector<std::pair<std::string, JsonValue>>& out);  // integers + strings + null; full \uXXXX incl. surrogate pairs; rejects nesting/trailing garbage
class JsonObjectWriter { public: void Str(std::string_view k, std::string_view utf8v); void Num(std::string_view k, int64_t v);
                         void Null(std::string_view k); std::string Finish(); };

// storage/StateStore.h   (files: <dir>\TinyLog.state, TinyLog.state.tmp)
class StateStore { public:
  explicit StateStore(std::wstring dir);
  enum class LoadKind { None, Loaded, PromotedTmp, Invalid };
  struct LoadResult { LoadKind kind = LoadKind::None; ActiveState state; std::wstring diagnostic; };
  LoadResult Load();                 // state valid -> Loaded (delete stray tmp). state missing/invalid & tmp valid -> promote tmp -> PromotedTmp. state invalid & no valid tmp -> Invalid (file untouched)
  Status Save(const ActiveState&);   // WriteFileAtomic
  Status Remove();                   // delete; missing file = ok
  Status QuarantineInvalid(std::wstring& movedTo);  // rename to TinyLog.state.corrupt-YYYYMMDD-HHMMSS (never delete)
};

// storage/JsonLinesStore.h  (file: <dir>\TinyLog.jsonl)
class JsonLinesStore { public:
  explicit JsonLinesStore(std::wstring path);
  Status RepairTail();               // if last byte != '\n' or last line unparsable: truncate to end of last valid '\n'-terminated line; earlier lines untouched
  Status Contains(const std::string& id, bool& found);   // streaming scan (64 KB chunks)
  Status Append(const CompletedSession&);                 // RepairTail first, OPEN_ALWAYS + FILE_APPEND_DATA, line+'\n', FlushFileBuffers
  Status ForEach(const std::function<bool(const CompletedSession&)>& cb, size_t* skippedMalformed = nullptr);  // streaming, file order; cb returns false to stop
  Status ReadLast(std::optional<CompletedSession>& last); // last valid record (scan backwards from end in chunks)
};

// app/Clock.h
struct Clock { virtual ~Clock() = default; virtual Timestamp Now() = 0; };
struct SystemClock final : Clock { Timestamp Now() override { return CaptureNow(); } };

// app/AppState.h
enum class AppState { Idle, Starting, Recording, Completing };

// app/SessionManager.h  — UI-thread only, synchronous (Spec §5.4-5.6, §8.1)
class SessionManager { public:
  SessionManager(StateStore&, JsonLinesStore&, Clock&);
  struct RecoveryReport { bool restoredRecording=false; bool finishedPendingStop=false; bool quarantined=false;
                          std::wstring quarantinePath; std::wstring error; std::wstring idleNotes; };
  RecoveryReport Recover();                       // full recovery matrix §5.6; idleNotes = ReadLast().notes when ending Idle
  Status Start(const std::wstring& notes);        // Idle only; Starting -> Save -> Recording; on failure back to Idle
  Status UpdateNotes(const std::wstring& notes);  // Recording: Save immediately; Idle: memory only (returns Ok)
  Status Stop();                                  // Recording only; §5.5 steps 1-8; on append failure stay Completing-on-disk, in-memory back to Recording; notes retained (user decision)
  AppState State() const;  const ActiveState* Active() const;   // nullptr when Idle
  const std::wstring& Notes() const;                             // current notes in any state
};

// export/ZipWriter.h — Store-only, streaming, seek-back patching (no data descriptors)
class ZipWriter { public:
  Status Open(HANDLE file);          // not owned; must be seekable
  Status BeginEntry(std::string_view utf8Name);   // writes local header w/ placeholder CRC/sizes, flag bit 11 (UTF-8), method 0, fixed DOS time 1980-01-01 00:00
  Status Write(const void* data, size_t len);     // updates CRC/size; error if entry > 0xFFFFFFFE
  Status EndEntry();                               // SetFilePointerEx back, patch CRC + sizes, return to end
  Status Finish();                                 // central directory + EOCD; error if >65535 entries or offsets >= 4 GiB
};

// export/SpreadsheetWriter.h
// Streams sheet1.xml rows into an open ZipWriter entry.
class SheetStream { public:
  explicit SheetStream(ZipWriter&);
  Status Begin();                               // xml decl, <worksheet>, <sheetViews> frozen header, <cols>, <sheetData>, header row (style 6)
  Status AddSession(const CompletedSession&);   // one <row>; see WP-D for exact cells
  Status End();                                 // </sheetData></worksheet>
};
Status WriteStaticParts(ZipWriter&);   // [Content_Types].xml, _rels/.rels, xl/workbook.xml (sheet "Sessions"), xl/_rels/workbook.xml.rels, xl/styles.xml

// export/ExportController.h
Status ExportWorkbook(JsonLinesStore& log, const std::wstring& destPath, size_t& rowsWritten);
       // dest dir temp "<dest>.tmp-<pid>", ZipWriter, static parts + streamed sheet, FlushFileBuffers, MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH); delete temp on failure

// win32/SingleInstance.h
constexpr wchar_t kMutexName[] = L"Local\\TinyLog.Singleton.v1";
constexpr wchar_t kOwnerClass[] = L"TinyLog.TrayOwner.v1";
constexpr wchar_t kActivateMsgName[] = L"TinyLog.Activate.v1";
class SingleInstance { public: bool Acquire();   // false => another instance owns the mutex
                               static void ActivateExisting(); };  // FindWindowW(kOwnerClass) + PostMessage(RegisterWindowMessage(kActivateMsgName))

// win32/TrayIcon.h
extern const GUID kTrayGuid;            // W0 generates once and freezes
constexpr UINT kTrayCallbackMsg = WM_APP + 1;
class TrayIcon { public:
  bool Add(HWND owner, bool recording);     // NIF_GUID|NIF_ICON|NIF_TIP|NIF_MESSAGE|NIF_SHOWTIP, NOTIFYICON_VERSION_4; on GUID failure: NIM_DELETE(guid)+retry, then fall back to hWnd+uID=1 (GUID is bound to EXE path; portable EXE may move)
  void SetRecording(bool recording);        // icon + tooltip "TinyLog — Idle"/"TinyLog — Recording"
  void Remove();
  bool GetIconRect(RECT& rc) const;         // Shell_NotifyIconGetRect with whichever identity Add used
  void ShowBalloon(const wchar_t* title, const wchar_t* text, bool error);  // NIF_INFO; the only notification surface
  void OnDpiOrThemeChanged();               // reload icons via LoadIconMetric(LIM_SMALL)
};

// win32/TrayMenu.h
enum : UINT { kCmdStartup = 100, kCmdExport = 101, kCmdExit = 102 };
UINT ShowTrayMenu(HWND owner, POINT screenPt, bool startupChecked);  // exactly: Start with Windows(check), separator, Export to Excel, Exit; SetForegroundWindow + TrackPopupMenuEx(TPM_RETURNCMD|TPM_RIGHTBUTTON) + PostMessage(WM_NULL); returns 0 if dismissed

// win32/StartupShortcut.h
namespace startup {
  Status IsEnabled(bool& enabled);        // Spec §6.3; deletes stale TinyLog.lnk that points elsewhere (ignore delete errors)
  Status Enable();                         // create/overwrite <Startup>\TinyLog.lnk -> current exe, workdir = exe dir
  Status Disable();                        // delete; missing = ok
  // testable core
  Status CreateShortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& workDir);
  Status ReadShortcutTarget(const std::wstring& lnk, std::wstring& target);   // IPersistFile::Load + GetPath(SLGP_RAWPATH) w/ 32K buffer
  bool   PathsEqual(const std::wstring& a, const std::wstring& b);            // GetFullPathNameW, case-insensitive (CompareStringOrdinal TRUE); if both exist, compare GetFinalPathNameByHandleW
}

// win32/SaveDialog.h
enum class SaveOutcome { Ok, Cancelled, Failed };
SaveOutcome ShowExportSaveDialog(HWND owner, std::wstring& path, Status& error);   // Spec §7.7

// win32/BubblePlacement.h  — pure math, physical pixels
enum class BubbleSide { Above, Below, Left, Right };   // side of the icon the bubble sits on
struct Placement { RECT rect; POINT anchor; BubbleSide side; };   // anchor = point on icon edge the pointer touches
Placement ComputePlacement(const RECT& iconRect, const RECT& workArea, SIZE bubbleSize, int gap);
       // prefer Above if it fits, else the side with most room (Below/Left/Right); center on icon along the cross axis; clamp fully inside workArea
Placement FallbackPlacement(POINT cursor, const RECT& workArea, SIZE bubbleSize, int gap);  // treat cursor as 1x1 icon rect

// win32/LayeredAnimation.h
float EaseOutCubic(float t);                                       // 1-(1-t)^3, t clamped [0,1]
RECT  ScaleRectAboutAnchor(const RECT& finalRect, POINT anchor, float scale);  // anchor fixed, rect shrinks toward it; min 1x1
BYTE  AlphaForProgress(float eased);                               // round(255*eased)
class LayeredAnimation { public:
  enum class Dir { Open, Close };
  struct Frame { RECT rect; BYTE alpha; bool done; };
  static constexpr UINT_PTR kTimerId = 0xA11;
  void  Start(HWND hwnd, Dir dir, RECT finalRect, POINT anchor, UINT durationMs = 200);  // records QPC start, SetTimer(hwnd,kTimerId,15)
  Frame Tick();               // progress from QPC elapsed (never "+16ms"); Close runs reversed; on done KillTimer
  void  Cancel();             // KillTimer
  bool  Running() const;
  Dir   Direction() const;
};

// win32/Theme.h — constexpr COLORREFs from Spec §3.3 + logical layout constants (see WP-F1); Scale(int logical, UINT dpi) = MulDiv(logical, dpi, 96)

// win32/BubbleWindow.h
struct BubbleModel { bool recording = false; bool busy = false; int64_t startUtc = 0; };
struct IBubbleHost { virtual void OnStartStopClicked() = 0; virtual void OnExportClicked() = 0;
                     virtual void OnNotesChanged(const std::wstring&) = 0; virtual void OnBubbleHidden(bool byDeactivation) = 0; };
class BubbleWindow { public:
  BubbleWindow(HINSTANCE, HWND owner, IBubbleHost&);
  ~BubbleWindow();                                   // destroys HWND + all GDI
  void Show(const RECT* iconRect, POINT fallbackPt); // lazy-creates HWND/fonts/controls on first call; places + animates open; focuses EDIT at end
  void Hide(bool animate);                           // reads EDIT first; animates close; ShowWindow(SW_HIDE); OnBubbleHidden
  bool IsVisible() const;                            // true while open or opening
  void SetModel(const BubbleModel&);                 // relabels button, status dot, timer; starts/stops 1 s display timer (visible && recording only)
  void SetNotes(const std::wstring&);                // suppresses OnNotesChanged while setting
  std::wstring GetNotes() const;
  void SetModalGuard(bool active);                   // while true, WM_ACTIVATE(WA_INACTIVE) does not collapse
  bool PreTranslate(MSG&);                           // IsDialogMessage for Tab/Space/Enter-on-button; Esc -> Hide(true)
  HWND Hwnd() const;
};
```

Also in W0: `main.cpp` (wWinMain → `App app; return app.Run(hInst);`), `App.h` with `class App : IBubbleHost { public: int Run(HINSTANCE); ... }`, `resource.h` IDs (`IDI_IDLE 101`, `IDI_RECORDING 102`), `TinyLog.rc` referencing them + manifest, placeholder icons (any valid 16/32 px .ico), `app.manifest` (PerMonitorV2 + `dpiAware true/pm` fallback, Common Controls v6, supportedOS Win10, `asInvoker`), `tools/build.cmd`, README with build instructions.

**W0 exit:** `tools\build.cmd debug` and `release` green; `TinyLogTests` runs 0 tests and passes; commit on `feat/tinylog-v1`.

---

## Wave 1

### WP-A · Pure utilities — **Haiku**
Owns: `src/util/StringHelpers.cpp, TimeHelpers.cpp, Crc32.cpp, FileHelpers.cpp`, `tests/Util_Tests.cpp`. Spec §5.2, §5.3, §5.7, §7.6.
Required tests (exact vectors):
- `Crc32Update(0,"123456789",9) == 0xCBF43926`; empty input → 0; split update equals one-shot.
- `ExcelSerialFromCivil(2026,11,17)==46343`; `(2025,1,1)==45658`; `(2000,2,29)==36585`; `(1900,3,1)==61`. `DaysFromCivil(1970,1,1)==0`; round-trip CivilFromDays over 1900–2100 every day.
- `FormatIsoUtc(0)=="1970-01-01T00:00:00Z"`; Parse round-trip; Parse rejects `"2026-11-17 01:00:00"`, `"…Z "`, month 13.
- `FormatLocal(1795222800, 480)` → compute expected = FormatIsoUtc of utc+480*60 reformatted; `FormatElapsed(45296)=="12:34:56"`, `(360000)=="100:00:00"`, `(-5)=="00:00:00"`.
- UTF-8: round-trip of `L"Fix login 😀 ü\r\n"`; `WideFromUtf8("\xC3\x28")` false.
- `JsonEscape("a\"b\\c\n\x01")=="a\\\"b\\\\c\\n\\u0001"`; `XmlEscape("<a&'\">\x01\t")=="&lt;a&amp;&apos;&quot;&gt;\t"`.
- `WriteFileAtomic` in TempDir: create, overwrite, content verified, no `.tmp` left behind. `GetExeDirectory` non-empty, no trailing `\`.

### WP-B · Resources & build tooling — **Haiku**
Owns: `resources/idle.ico, recording.ico, app.manifest, TinyLog.rc` (content only; IDs frozen), `tools/make-icons.ps1, check-size.ps1, audit.ps1`, `README.md`.
- `make-icons.ps1` (System.Drawing, run once, commit outputs): multi-size .ico (16,20,24,32,48 px) — rounded clock: Idle = grey `#9AA5B5` ring + hands; Recording = green `#32D16D` ring + hands with small filled dot (matches mockup tray icon). Transparent background, antialiased.
- `check-size.ps1 -Path <exe> -Max 3000000` → exit 1 with message if larger; prints size.
- `audit.ps1 -Path <exe>`: runs `dumpbin /dependents` and `/imports`; fails if any DLL outside allowlist {KERNEL32, USER32, GDI32, SHELL32, OLE32, OLEAUT32, COMCTL32, UxTheme, ADVAPI32*, COMDLG32} or if imports contain `RegCreateKey|RegSetValue|RegOpenKey|RegDeleteKey|SHReg` (*ADVAPI32 allowed only for non-Reg imports e.g. CRT pulls). Prints a report.
- README: what it is, build (`tools\build.cmd release`), portable usage, data files, pin-to-tray hint, known limits (Spec §5.7 clock changes, §13.4).
Verify: Release build embeds icons (`TinyLog.exe` shows clock in Explorer); both scripts run against the W0 exe.

### WP-E · Shell leaf modules — **Sonnet**
Owns: `src/win32/SingleInstance.cpp, TrayIcon.cpp, TrayMenu.cpp, StartupShortcut.cpp, SaveDialog.cpp`, `tests/Startup_Tests.cpp`. Spec §3.7, §6, §7.7, §8.2.
- TrayIcon: implement the GUID→uID fallback described in the contract; keep `NOTIFYICONDATAW` builder in one helper; `LoadIconMetric` for DPI-correct icons; tooltip uses em dash.
- TrayMenu: `CreatePopupMenu` each time, destroy after; `MF_CHECKED` for startup.
- StartupShortcut: `SHGetKnownFolderPath(FOLDERID_Startup)` + `CoTaskMemFree`; every HRESULT checked; on Save failure delete partial file. Callers already hold a `ComInit`.
- SaveDialog: `IFileSaveDialog` title `Export TinyLog sessions`, filter `Excel Workbook (*.xlsx)`, `SetDefaultExtension(L"xlsx")`, filename `TinyLog_YYYY-MM-DD.xlsx` (local date), `FOS_FORCEFILESYSTEM|FOS_OVERWRITEPROMPT|FOS_PATHMUSTEXIST`; `HRESULT_FROM_WIN32(ERROR_CANCELLED)` → Cancelled.
- Tests (Startup_Tests): CreateShortcut in TempDir → ReadShortcutTarget equals target; PathsEqual: case diff true, `a\..\b` normalization true, different files false; overwrite existing lnk. (No tests touch the real Startup folder or tray.)

### WP-F2 · Placement & animation math — **Haiku**
Owns: `src/win32/BubblePlacement.cpp, LayeredAnimation.cpp`, `tests/Placement_Tests.cpp, Animation_Tests.cpp`. Spec §3.5, §4.
- ComputePlacement: bottom taskbar (icon near bottom of work area) → Above, `rect.bottom = icon.top - gap`, centered on icon X; icon at right edge → clamped so `rect.right <= work.right`; top taskbar → Below; left/right taskbars → Right/Left; anchor = midpoint of the icon edge facing the bubble.
- LayeredAnimation: QPC frequency cached; Open: scale 0.10→1.00, alpha 0→255 by eased t; Close: reversed; `done` frame returns exact finalRect/255 (open) or alpha 0 (close).
- Tests: EaseOutCubic(0)=0, (1)=1, (0.5)=0.875; ScaleRectAboutAnchor(scale 1)=finalRect, (scale 0.1) contains anchor and width ≈10%; Alpha(1)=255; placement cases for all four taskbar edges + clamping + fallback; multi-monitor case with negative work-area coordinates (e.g. work `{-1920,0,0,1040}`).

### WP-F1 · Bubble window & rendering — **Sonnet**
Owns: `src/win32/BubbleWindow.cpp, GdiRenderer.h/.cpp, NativeControls.h/.cpp, Theme.h` (values only), `demo/BubbleDemo.cpp`. Spec §3.1–3.6, §8.5–8.6, §8.8, §9.1–9.2. Uses F2 functions via headers (stubs until merge; final verify after F2 merges).
Layout in logical px at 96 DPI (scale with `GetDpiForWindow`), matching the mockup:
- Card 360 wide; padding 16; corner radius 14; pointer triangle 16 wide × 9 tall on the `BubbleSide` edge at the anchor (region = `CreateRoundRectRgn` ∪ triangle polygon via `CombineRgn`; on `SetWindowRgn` failure delete region).
- Header row y=14, h=32: status dot Ø10 (green `#32D16D` recording / `#9AA5B5` idle) at x=16; "TinyLog" Segoe UI Semibold 15pt `#F3F5FA`; 1px divider `#343D50`; right-aligned timer pill 128×32, radius 16, border green (idle: border), clock glyph (GDI ellipse + 2 lines) + `FormatElapsed` in Segoe UI Semibold 14pt green (idle shows `00:00:00` secondary color).
- Notes box y=58, h=84: parent draws rounded rect bg `#111621` border `#343D50` (focus: `#3987FF`); EDIT inset 10 px (`ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_TABSTOP`, Segoe UI 11pt, `WM_CTLCOLOREDIT` bg/text, `SetWindowTheme(edit, L"DarkMode_Explorer", nullptr)` best-effort, `EM_SETCUEBANNER`-free).
- Buttons y=154, h=44, two equal columns gap 12: Start/Stop (`BS_OWNERDRAW|WS_TABSTOP`) bg `#15964A` (hover/pressed darker, disabled when `busy` greyed), white square glyph for Stop / triangle for Start, label "Stop"/"Start" Semibold 13pt; Export bg `#252D3D` with drawn Excel glyph (green `#1D6F42` rounded square + white "X") + "Export to Excel". Focus ring 2px `#3987FF` when `ODS_FOCUS`.
- Height 154+44+16 = 214 (+pointer).
- Window: `WS_POPUP`, `WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_LAYERED`, owner = owner HWND; `SetLayeredWindowAttributes` only (never `UpdateLayeredWindow`). Paint via memory-DC double buffer.
- Animation: per Spec §3.5 — build snapshot bitmap of full-size bubble (with current notes text drawn by renderer), hide children, drive `LayeredAnimation` on WM_TIMER, `SetWindowPos` + `StretchBlt` snapshot + alpha; at end set exact rect, delete snapshot, show children, focus EDIT. Cancel animation on `WM_DPICHANGED`/`WM_DISPLAYCHANGE` and snap to final.
- `WM_DPICHANGED`: recreate fonts/region, relayout children. Lazy create everything on first `Show`; keep HWND hidden afterwards (measure vs destroy in WP-R).
- `WM_ACTIVATE(WA_INACTIVE)` → `Hide(true)` + `OnBubbleHidden(true)` unless modal guard; Esc → Hide. `EN_CHANGE` → `OnNotesChanged(GetNotes())`. Button clicks → host callbacks.
- `BubbleDemo.cpp`: creates a hidden owner, a fake `IBubbleHost` toggling recording, opens the bubble anchored at cursor on a hotkey/at start — for visual verification and screenshots.
Verify: BubbleDemo shows mockup-like bubble at 100% and 150% DPI; Tab cycles EDIT→Start→Export; Esc and click-outside close; GDI count stable over 50 open/close (Task Manager GDI objects column).

---

## Wave 2

### WP-C · Storage & session state machine — **Sonnet**
Owns: `src/storage/*.cpp`, `src/app/SessionManager.cpp`, `tests/Storage_Tests.cpp, Session_Tests.cpp`. Spec §5, §8.1, §8.3, §8.7.
- Implement contracts exactly; `SerializeCompleted` field order as Spec §5.2; notes stored as UTF-8 of the EDIT text (CRLF preserved).
- SessionManager.Stop: (1) ts = clock.Now(); (2) Save completing with pendingEnd*; (3) build CompletedSession, duration = endUtc − startUtc clamped ≥ 0; (4) Contains(id); (5) Append if absent; (7) Remove state; (8) Idle, notes kept. Any failure after step 2 leaves the completing state file on disk.
- Recover(): RepairTail first, then the §5.6 matrix; Invalid → QuarantineInvalid and report (never overwrite); Idle → idleNotes from ReadLast.
- Tests with a `FakeClock` and TempDir: start/stop happy path (one line, durations); stop twice rejected; Start while Recording rejected; recovery of recording; completing+absent → appended once; completing+present → no duplicate, state removed; crash simulation = write completing state + already-appended line → Recover gives exactly one record; truncated tail `{"schema":1,"id":"x` repaired and prior lines intact; middle malformed line skipped and counted by ForEach; invalid state quarantined and file still exists under new name; tmp promotion; Unicode/multiline notes round-trip; midnight-crossing and DST (start offset 60, end offset 120) → duration from UTC; read-only dir (`FILE_ATTRIBUTE_READONLY` on state file) → Start fails and state stays Idle.

### WP-D · ZIP, XLSX, export — **Sonnet**
Owns: `src/export/*.cpp`, `tests/Zip_Tests.cpp, Spreadsheet_Tests.cpp`. Spec §7.
- Sheet row cells (r = row index from 2):
  - A Date: `<c r="A{r}" s="1"><v>{ExcelSerial(start local date)}</v></c>`
  - B Start: `s="2"` value = local seconds-of-day/86400; C End: `s="2"` end local seconds-of-day/86400
  - D Minutes: `s="3"` durationSeconds/60.0 ; E HH:MM:SS: `s="4"` durationSeconds/86400.0
  - F Notes: `<c r="F{r}" t="inlineStr" s="5"><is><t xml:space="preserve">{XmlEscape(Utf8(NormalizeNewlinesToLf(notes)))}</t></is></c>`
  - Numbers via `snprintf("%.15g")` (C locale; never call `setlocale`).
- styles.xml: numFmts 164 `yyyy-mm-dd`, 165 `hh:mm:ss`, 166 `[h]:mm:ss`; cellXfs index 0 default, 1 date(164), 2 time(165), 3 `0.00` (builtin 2), 4 duration(166), 5 wrapText, 6 bold header font. Header row: `Date, Start Time, End Time, Duration (Minutes), Duration (HH:MM:SS), Notes / Ticket` as inline strings style 6. `<cols>` widths 12,11,11,18,19,60. Freeze row 1.
- Rows in log order (log is append-ordered by Stop; single instance ⇒ chronological — no sort, documented).
- Tests: ZIP of two entries read back by a tiny in-test central-directory parser (signatures, CRC matches `Crc32Update`, sizes, offsets). Additionally verify once by hand that `powershell Expand-Archive` extracts a generated workbook. Formula-like note `=1+1` emitted as inlineStr; empty log produces header-only valid workbook; 25 h session yields E value 1.0416666…; control char stripped; a 10,000-row export completes and peak private bytes stay < 15 MB (assert via `GetProcessMemoryInfo` delta, generous bound).
- Manual verification: open generated sample in Excel (and LibreOffice if available): formats display, `[h]:mm:ss` shows `25:00:00`, notes multiline.

---

## Wave 3

### WP-H · App integration — **Sonnet**
Owns: `src/main.cpp, src/app/App.h, App.cpp` (may extend App.h privately). Spec §3.1, §3.7, §4.2, §8.2–8.7, §9.1.
- `Run`: SingleInstance (else ActivateExisting, return 0) → `GetExeDirectory` → write-probe (create/delete `TinyLog.probe`; failure → MessageBox "TinyLog cannot write next to TinyLog.exe" and exit 1; never relocate) → register `kOwnerClass`, create hidden `WS_POPUP`/`WS_EX_TOOLWINDOW` owner → `ComInit` for the UI thread → stores + SessionManager → `Recover()` (quarantine/error → balloon) → TrayIcon.Add(recording?) → first-run balloon (no jsonl and no state existed) with pin-to-tray hint → message loop with `bubble.PreTranslate`.
- Owner wndproc: `kTrayCallbackMsg` with `LOWORD(lParam)`: `NIN_SELECT`/`NIN_KEYSELECT` → toggle bubble; `WM_CONTEXTMENU` → `startup::IsEnabled` → `ShowTrayMenu` → dispatch; ignore toggles within 300 ms of a deactivation-collapse (the click that deactivated the bubble must not reopen it) and while animating. `TaskbarCreated` → `TrayIcon.Add` again. Activate message → open bubble. `WM_QUERYENDSESSION` → TRUE; `WM_ENDSESSION`/Exit → `TrayIcon.Remove`, destroy, `PostQuitMessage` (state file untouched while Recording). `WM_SETTINGCHANGE`/`WM_DPICHANGED` on owner → `TrayIcon.OnDpiOrThemeChanged`.
- Bubble open: `GetIconRect` (fallback = callback point from `GET_X/Y_LPARAM(wParam)`, then cursor) → `bubble.Show`; `SetNotes(session.Notes())`, `SetModel`.
- IBubbleHost: StartStop → disable via `busy` model → Start/Stop → update tray icon/tooltip + model; on failure balloon error. NotesChanged → `UpdateNotes` (balloon once per failure streak). Export (bubble or menu) → `SetModalGuard(true)` → SaveDialog → `ExportWorkbook` → balloon "Exported N sessions" / error → guard false. Startup toggle → Enable/Disable, balloon on failure.
- Verify by running Release `TinyLog.exe` from a copy in `%TEMP%\TinyLogRun\` (never pollute the repo dir): full manual checklist from Spec §12.1–12.3.

---

## Wave 4 — WP-R · Review & hardening (Opus review, Sonnet fixes)

1. Opus code review of the integrated branch against Spec §5 (durability ordering), §8 (edge cases), GDI ownership (§8.8), and the contracts — produce a findings list; Sonnet agents fix per file-disjoint batch.
2. Footprint (Spec §9.3, §12.6): Release size via `check-size.ps1`; `audit.ps1` dependencies/registry imports; a `tools/measure.ps1` (WP-R adds) sampling `Get-Process TinyLog` working set/private bytes/handles + GDI/USER via `GetGuiResources` at the six measurement points; record results in `docs/footprint.md`.
3. 100 open/close cycles → GDI/USER counts flat; idle CPU 0 (no timers while hidden — confirm with Process Explorer or `Get-Process` CPU over 60 s).
4. Display matrix (Spec §12.4) as a manual checklist in `docs/test-matrix.md` for the user to tick; Opus runs what is possible locally (100%/150%, taskbar edges, Explorer restart via `Stop-Process explorer`).
5. `superpowers:finishing-a-development-branch` → hand back to user (no push; no remote).

---

## File ownership summary

| WP | Model | Wave | Files (exclusive) |
|---|---|---|---|
| W0 | Opus | 0 | all headers, CMake*, build.cmd, TestHarness/TestMain, stubs, main.cpp, App.h skeleton, resource.h |
| A | Haiku | 1 | util/*.cpp, tests/Util_Tests.cpp |
| B | Haiku | 1 | resources/* (except resource.h), tools/make-icons, check-size, audit, README |
| E | Sonnet | 1 | win32/SingleInstance, TrayIcon, TrayMenu, StartupShortcut, SaveDialog .cpp, tests/Startup_Tests.cpp |
| F2 | Haiku | 1 | win32/BubblePlacement.cpp, LayeredAnimation.cpp, tests/Placement_, Animation_Tests.cpp |
| F1 | Sonnet | 1 | win32/BubbleWindow.cpp, GdiRenderer.*, NativeControls.*, Theme.h, demo/BubbleDemo.cpp |
| C | Sonnet | 2 | storage/*.cpp, app/SessionManager.cpp, tests/Storage_, Session_Tests.cpp |
| D | Sonnet | 2 | export/*.cpp, tests/Zip_, Spreadsheet_Tests.cpp |
| H | Sonnet | 3 | main.cpp, app/App.h, App.cpp |
| R | Opus→Sonnet | 4 | fixes by batch; tools/measure.ps1, docs/* |

Agent dispatch: one `Agent` call per WP in a single message per wave, `isolation: "worktree"`, `model` per table, prompt = the "Rules every WP agent receives" + Contracts section + that WP section + Spec path. Haiku WPs get `effort: "lower"`. Orchestrator (Opus) merges, runs full build+tests after each merge, and resolves any reported contract change before the next wave.

---

## Verification (end-to-end)

1. `cmd //c tools\build.cmd release` → build green, all `TinyLogTests` pass, size check passes, `audit.ps1` clean.
2. Copy `build\release\TinyLog.exe` to `%TEMP%\TinyLogRun\`, run it: tray clock icon appears (grey), tooltip `TinyLog — Idle`; second launch opens the existing bubble instead of a second icon.
3. Left-click → bubble animates up from the icon, focus in notes; type note; Start → icon green, tooltip Recording, timer pill counts; click outside → collapses, still recording.
4. Kill the process (`taskkill /f`) mid-recording → relaunch → Recording restored with notes and correct elapsed time.
5. Stop → one line in `TinyLog.jsonl`, `TinyLog.state` gone, notes still in box. Hand-craft a `completing` state with an ID already in the log → relaunch → no duplicate.
6. Right-click → menu has exactly the 4 entries; toggle Start with Windows → `TinyLog.lnk` in `shell:startup` targets the exe; move the exe → checkmark cleared.
7. Export → Save dialog with `TinyLog_<date>.xlsx`; open in Excel: 6 columns, numeric date/time cells, `[h]:mm:ss`, Unicode/multiline/`=1+1` notes as text. Cancel → nothing written.
8. Restart Explorer → icon re-registers. 150% DPI monitor → bubble crisp and correctly placed.
