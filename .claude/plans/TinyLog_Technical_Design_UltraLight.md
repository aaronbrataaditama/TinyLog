# TinyLog — Ultra-Light Technical Design & Implementation Blueprint

**Document type:** Technical design specification and implementation handoff  
**Status:** Proposed architecture — ready for independent engineering review  
**Target OS:** Windows 10 (version 1703 or later) and Windows 11, x64  
**Application:** `TinyLog.exe`  
**Primary objective:** A portable, tray-only work/time logger with a very small executable, minimal idle memory use, reliable recovery, and no non-Windows runtime prerequisite.

---

## 0. Executive Summary

TinyLog is a single-executable Windows utility for recording work sessions. Its UI is a small dark floating bubble opened from the notification-area icon. The bubble contains exactly three interactive elements:

1. A multiline notes/ticket input.
2. A primary Start/Stop button.
3. An Export to Excel button.

The **Start with Windows** toggle exists only in the tray icon's right-click menu. TinyLog stores completed records and active-session recovery state beside the executable. It creates a Startup-folder shortcut only when explicitly enabled. Excel export creates a standards-based `.xlsx` file without Microsoft Excel or a third-party spreadsheet runtime.

### 0.1 Final architecture decision

Use **C++20 + native Win32 + GDI**, with a small layered-window animation implemented through `SetLayeredWindowAttributes`, `SetWindowPos`, and a UI timer. Do not use Direct2D, DirectWrite, DirectComposition, D3D11, DXGI, WebView2, Tauri, Qt, Electron, .NET, Python, or a third-party UI framework.

| Area | Decision |
|---|---|
| Language/compiler | C++20, MSVC x64 |
| Windowing | Win32 API |
| Static rendering | GDI / owner-drawn controls |
| Notes editor | Native Win32 multiline EDIT control |
| Buttons | Owner-drawn Win32 BUTTON controls |
| Bubble animation | Layered top-level window; timer-driven geometry and alpha |
| Tray | `Shell_NotifyIconW`, stable GUID |
| Startup shortcut | `SHGetKnownFolderPath`, `IShellLinkW`, `IPersistFile` |
| Persistence | UTF-8 JSON Lines + atomic active-state file |
| Excel | Small custom ZIP writer + SpreadsheetML XML |
| Runtime | Static MSVC CRT (`/MT`) where compatible |
| External runtime | None; normal Windows system DLLs are permitted |

### 0.2 Footprint targets

These are **engineering targets to validate with release builds**, not measured guarantees.

| Metric | Preferred target | Hard acceptance target |
|---|---:|---:|
| `TinyLog.exe` size | 1.5–2.0 MB | < 3,000,000 bytes |
| Idle private working set | 5–8 MB | < 15 MB |
| Idle private bytes | < 5 MB | Measure and record; no hard promise until benchmarked |
| Idle CPU | Effectively zero | No periodic polling or background work |
| Background worker threads | 0 | 0 while idle |

A sub-1 MB executable is a stretch goal, not a release promise. Static CRT linkage, compiler output, embedded resources, and the selected JSON/XML routines determine the final size. Do not weaken correctness or safety merely to save a few kilobytes.

### 0.3 Explicit interpretation of portability constraints

TinyLog itself must not create or modify application registry keys. It must not register file associations, shell extensions, services, scheduled tasks, or COM classes. All application-owned persistent data stays beside `TinyLog.exe`, except:

- the explicitly requested `TinyLog.lnk` in the user's Startup folder when Start with Windows is enabled; and
- an `.xlsx` file at the location explicitly selected by the user in the Save dialog.

A literal guarantee that Windows never consults the registry is impossible while using COM activation for Shell Link and `IFileSaveDialog`, and while using Windows notification-area infrastructure. The enforceable requirement is **no direct application-owned registry access or writes**; Windows may use its own internal registration and preference mechanisms.

---

# 1. Product Scope and UI Contract

## 1.1 Bubble UI

The popup is a compact dark card with a subtle border, rounded corners, shadow, and an optional small pointer/anchor towards the tray icon. The Recording state uses a green status indicator and a visually prominent Stop button. Idle uses a neutral status indicator and a Start button.

The bubble contains only these three **interactive** elements:

1. **Notes editor** — multiline, Unicode, editable in Idle and Recording.
2. **Start/Stop button** — one button whose label and appearance reflect state.
3. **Export to Excel button** — launches the native Save dialog.

Passive status text is allowed (for example, “Recording” and an elapsed-time display); it is not an extra interactive control. Do not add history, settings, navigation, tabs, menus, or a Start with Windows control inside the bubble.

The tray context menu contains exactly:

1. Start with Windows (checkable).
2. Separator.
3. Export to Excel.
4. Exit.

A left-click opens or toggles the bubble. A right-click opens the context menu.

## 1.2 Application states

The externally visible states are:

- `Idle`
- `Recording`

Internal transition states are:

- `Starting` — initial active state is being durably saved.
- `Completing` — Stop is being committed to the completed log.

Do not expose the internal transition states as screens or separate controls.

## 1.3 Non-goals

- No cloud sync, accounts, networking, or telemetry.
- No in-app history or editing of old sessions.
- No automatic splitting at midnight.
- No taskbar button or Alt+Tab entry.
- No installer or updater.
- No Excel automation or requirement for Microsoft Office.

---

# 2. Technology Selection and Trade-offs

## 2.1 Candidate comparison

| Criterion | Rust + Win32 | C++20 + Win32/GDI | C# Native AOT |
|---|---|---|---|
| Standalone EXE | Yes | Yes | Yes, self-contained |
| No installed language runtime | Yes | Yes | Yes when published self-contained |
| Tiny native UI footprint | Very good | Best control | Less predictable |
| Direct Win32/COM access | Good, bindings/unsafe interop | Native first-class | Interop required |
| Native controls | Possible | Direct | Interop or wrapper |
| Binary-size tuning | Good | Excellent | Runtime closure adds baseline |
| Memory predictability | Good | Excellent | Runtime/GC baseline |
| Engineering fit for this app | Good | **Best** | Acceptable but not preferred |

### Rust

Rust can produce a self-contained executable with no garbage collector and good memory safety. It is viable, but TinyLog is predominantly Win32, Shell COM, and GDI calls. That means Windows bindings and unsafe interop are still required. Rust remains a sound alternative if memory safety is prioritized over keeping the implementation entirely native to the Windows C/C++ toolchain.

### C++20

C++20 provides direct access to HWNDs, message handling, GDI, layered windows, Shell notification APIs, and COM. It offers the most predictable control over the binary and runtime footprint. The trade-off is manual ownership of handles, GDI objects, buffers, and COM interfaces. Use RAII wrappers even in a small codebase.

### C# Native AOT

Native AOT can create self-contained native executables, but it still has a runtime-related code footprint and Native AOT has restrictions around built-in COM support. It is not the best choice for the strictest executable-size target and the required Shell COM interfaces.

## 2.2 Why GDI instead of DirectComposition

The visual surface is simple: one rounded rectangle, text, a notes editor, and two buttons. Direct2D, DirectWrite, DirectComposition, D3D11, and DXGI would introduce additional graphics initialization and resource lifetimes for effects that are not essential to the product.

GDI is sufficient for:

- solid dark fills and borders;
- text and icons;
- owner-drawn buttons;
- off-screen bitmap rendering;
- small UI snapshots for the expansion/collapse transition.

The bubble should look polished through spacing, typography, contrast, and restrained colour—not through expensive blur or acrylic effects.

## 2.3 Build configuration

Recommended starting point:

| Setting | Value |
|---|---|
| Architecture | x64 |
| Language standard | C++20 |
| Runtime library | `/MT` for Release |
| Optimization | Start with `/O1`; measure `/O2` |
| Function-level linking | `/Gy` |
| Data-level linking | `/Gw` |
| Link-time code generation | `/GL` + `/LTCG` |
| Dead-code elimination | `/OPT:REF` |
| Identical COMDAT folding | `/OPT:ICF` |
| Incremental linking | Off for Release |
| RTTI | Off if no code requires it |
| Exceptions | Avoid exception-heavy dependencies; do not suppress required error handling |
| Debug symbols | External PDB; do not ship PDB |
| Subsystem | Windows GUI (`/SUBSYSTEM:WINDOWS`) |

Use the Windows SDK libraries only. Do not include MFC, ATL, Boost, Qt, wxWidgets, a general-purpose JSON framework, a full ZIP library, or a spreadsheet SDK unless later measurements show a compelling reason.

`/MT` removes the dependency on the separately installed MSVC runtime DLLs, but it increases the amount of runtime code embedded in the executable. Benchmark `/MT` and an allowed alternative only if the portability requirement changes; the release must not require the Visual C++ Redistributable.

The build must check the actual file size and fail if it exceeds 3,000,000 bytes. Track both compressed distribution size only if packaging is later introduced, and the actual uncompressed PE file size; the acceptance criterion is the latter.

---

# 3. Windowing, Tray, and Bubble UI

## 3.1 Window architecture

Use two top-level windows:

### `TrayOwnerWindow`

A hidden top-level owner window with:

- `WS_POPUP`
- `WS_EX_TOOLWINDOW`
- no visible taskbar or Alt+Tab presence

Responsibilities:

- receive tray callbacks;
- own and display the tray context menu;
- process `TaskbarCreated`;
- receive the single-instance activation message;
- own the bubble window;
- coordinate application messages and transitions.

Prefer a hidden top-level window rather than relying solely on an `HWND_MESSAGE` message-only window, because the app needs a normal owner for popup/menu interactions and predictable Shell notification handling.

### `BubbleWindow`

A top-level popup owned by `TrayOwnerWindow`:

- style: `WS_POPUP`;
- extended styles: `WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED`;
- no caption, resize frame, or system menu;
- no taskbar/Alt+Tab presence;
- activation allowed, because the notes editor needs keyboard focus.

Do not use `WS_EX_NOACTIVATE`; the notes editor must accept keyboard focus. Do not use `WS_EX_APPWINDOW`.

The popup contains one multiline EDIT control and two buttons. Buttons can be owner-drawn to match the dark theme. They remain real native child controls, not pictures.

## 3.2 Rounded corners and shadow

Use `CreateRoundRectRgn` + `SetWindowRgn` for the bubble's rounded outline on the compatibility path. `SetWindowRgn` takes ownership of the region after a successful call; do not delete a region after ownership has transferred. If `SetWindowRgn` fails, delete the region yourself.

A GDI region produces a pixel-aligned edge and may look less antialiased than a GPU-rendered surface. This is an intentional trade-off for the ultra-light profile. Keep the corner radius modest (approximately 12–16 logical pixels) so the edge looks clean at common DPI values.

Use the standard DWM/window shadow where Windows supplies it. If shadow appearance is inconsistent on a supported configuration, draw a restrained shadow into the GDI animation snapshot rather than adding a third-party rendering engine. Do not promise identical shadow rendering on every Windows theme or remote desktop session.

## 3.3 Dark theme palette

Suggested starting palette (tune during implementation):

| Element | Colour |
|---|---|
| Bubble background | `#171B26` |
| Inner/editor background | `#111621` |
| Primary text | `#F3F5FA` |
| Secondary text | `#9AA5B5` |
| Border | `#343D50` |
| Recording green | `#32D16D` |
| Stop button | `#15964A` / darker hover state |
| Export button | `#252D3D` |
| Focus/accent | `#3987FF` |

The bubble should not imitate the full Windows acrylic material. Use an opaque or nearly opaque charcoal surface with a simple border. This keeps the rendering path predictable and avoids costly blur/backdrop dependencies.

## 3.4 Native controls

### Notes EDIT control

Create a child EDIT control with:

- `ES_MULTILINE`
- `ES_AUTOVSCROLL`
- `ES_WANTRETURN`
- `WS_VSCROLL` only if the final design requires it
- `WS_TABSTOP`
- Unicode window class `L"EDIT"`

Set the font explicitly using a small system UI font (for example Segoe UI, with a system fallback). Handle `EN_CHANGE` to persist notes immediately. Handle `WM_CTLCOLOREDIT` and `WM_CTLCOLORSTATIC` for dark backgrounds, while ensuring text/caret selection contrast remains usable.

A standard EDIT control may not perfectly match every custom dark-theme detail. The goal is reliable text editing, clipboard behaviour, keyboard navigation, selection, and IME support. Do not replace it with a custom editor merely for visual perfection.

### Buttons

Use native BUTTON child windows with owner drawing (`BS_OWNERDRAW`) to match the design. Maintain accessible keyboard focus indication and support Space/Enter activation. There are exactly two buttons.

## 3.5 Bubble expansion and collapse

### Recommended ultra-light approach

Use a single `WS_EX_LAYERED` `BubbleWindow` and animate its top-level geometry and constant opacity:

- `SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA)` sets global opacity;
- `SetWindowPos` changes the popup position and size;
- `SetTimer` runs a short-lived UI-thread animation timer;
- GDI redraws the bubble's background and status surface for each animation step;
- child controls are hidden during the transform and shown after the final geometry is reached.

This deliberately avoids DirectComposition and avoids a second permanently allocated graphics pipeline.

**Important API rule:** Do not mix `SetLayeredWindowAttributes` and `UpdateLayeredWindow` on the same HWND during normal operation. Microsoft documents that after `SetLayeredWindowAttributes` is called, subsequent `UpdateLayeredWindow` calls fail until the `WS_EX_LAYERED` style bit is cleared and set again. TinyLog uses the `SetLayeredWindowAttributes` path only.

### Animation rendering behaviour

The bubble should visually scale from the tray anchor. During animation:

1. Capture or draw a static GDI snapshot of the bubble's visible content (background, status, notes text, button labels and visual states) into a compatible memory bitmap.
2. Hide native child controls so their HWNDs do not distort while the parent changes size.
3. Resize/reposition the parent window from a small rectangle at the tray icon towards its final rectangle.
4. Stretch the snapshot into the current client rectangle using `StretchBlt` (or redraw the static surface at the current size).
5. Update global alpha from transparent to opaque.
6. On completion, set the exact final rectangle, redraw at full resolution, show the native child controls, and focus the notes EDIT control.

For collapse, capture the current note contents and button state into a fresh snapshot, hide controls, reverse the geometry/opacity animation towards the tray icon, then hide the popup.

The snapshot must never be the source of truth for notes; always read the notes from the EDIT control before hiding it. The actual controls are interactive only at the fully expanded state.

### Suggested animation parameters

| Parameter | Suggested value |
|---|---:|
| Duration | 180–220 ms |
| Initial scale | 0.10 |
| Final scale | 1.00 |
| Initial opacity | 0 |
| Final opacity | 255 |
| Timer interval | 15–16 ms (target ~60 FPS) |
| Easing | Cubic-out |

For normalized progress `t` in `[0,1]`, use:

`easeOutCubic(t) = 1 - (1 - t)^3`

Then:

`scale(t) = 0.10 + 0.90 * easeOutCubic(t)`

`alpha(t) = round(255 * easeOutCubic(t))`

Use elapsed time from `QueryPerformanceCounter`, not “add 16 ms per tick”, because timer delivery can be delayed. The UI timer only exists during the short animation and is killed immediately after completion. A 60 FPS target is not a hard guarantee under load, remote desktop, or power-saving configurations.

### Anchor/pivot geometry

Get the true icon rectangle using `Shell_NotifyIconGetRect`. For a bubble above the icon:

`anchorX = (iconRect.left + iconRect.right) / 2`

`anchorY = iconRect.top`

For each animation frame, interpolate the window rectangle so the bubble's lower-center anchor remains attached to `(anchorX, anchorY)`. The small initial shape should be centred around the same anchor.

## 3.6 DPI awareness

Declare Per-Monitor V2 awareness in the manifest. Target Windows 10 version 1703+ and Windows 11.

Handle:

- `WM_DPICHANGED` — use the suggested rectangle, update DPI-scaled dimensions, recreate DPI-specific fonts/regions/bitmaps, reposition child controls;
- `WM_DISPLAYCHANGE` — refresh display configuration if needed;
- `WM_SETTINGCHANGE` — re-evaluate relevant shell/taskbar settings where appropriate.

Use `GetDpiForWindow` and scale logical dimensions by `dpi / 96.0`. Do not assume all monitors share one scale factor. Re-query the tray icon rectangle every time the bubble opens.

## 3.7 Tray icon registration and menu

Register the tray icon with `Shell_NotifyIconW` using:

- `NIM_ADD` to register;
- `NIM_MODIFY` to update tooltip/icon;
- `NIM_DELETE` on orderly exit;
- `NIF_GUID` with a permanent application GUID;
- `NIF_ICON`, `NIF_TIP`, and callback flags;
- `NOTIFYICON_VERSION_4`.

Use two embedded icons:

| State | Tooltip |
|---|---|
| Idle | `TinyLog — Idle` |
| Recording | `TinyLog — Recording` |

Use `RegisterWindowMessage(L"TaskbarCreated")`. When Explorer restarts and broadcasts this message, re-register the icon. Windows controls whether a notification icon is shown in the main notification area or hidden overflow; an app cannot force a user-pinned visibility preference. Provide a short first-run note explaining how to pin TinyLog in Taskbar Settings if necessary.

The tray context menu must contain exactly the four requested entries, including the separator. Before displaying the menu, call the Startup shortcut verification routine and update the menu checkmark from the resolved target path, not just file existence.

Use `SetForegroundWindow`/`TrackPopupMenuEx` patterns carefully so the menu dismisses correctly. Handle the relevant tray callback event codes for left click, right click, and double click. Debounce repeated activation events and avoid opening two bubbles.

---

# 4. Bubble Positioning and Monitor Behaviour

## 4.1 Position from the actual icon rectangle

Call `Shell_NotifyIconGetRect` with a `NOTIFYICONIDENTIFIER` containing the stable TinyLog GUID. Do not guess the taskbar location or use fixed screen coordinates.

Find the monitor with `MonitorFromRect` or `MonitorFromPoint`, then retrieve its work area using `GetMonitorInfo` (`rcWork`). Calculate the final position around the icon centre and clamp it inside the monitor work area.

For a default bottom taskbar:

`x = iconCenterX - bubbleWidth / 2`

`y = iconRect.top - gap - bubbleHeight`

If there is insufficient room above, place the bubble below the icon where possible. For side/top taskbars, use the same principle: choose the side with the most usable work area, then clamp the full rectangle. Keep the visual anchor near the icon when practical.

## 4.2 Auto-hide and overflow

The taskbar may be auto-hidden, moved to another edge, or the icon may be inside the notification overflow flyout. Query the icon rectangle on every opening. If the API fails, use the cursor point from the notification callback as a best-effort fallback. If neither is available, do not open at an arbitrary hard-coded location; report or use a conservative work-area corner fallback only as a last resort.

## 4.3 Multi-monitor handling

All positioning calculations use physical screen coordinates supplied by the DPI-aware process. Keep the bubble on the monitor hosting the actual tray icon. Recalculate dimensions for that monitor's DPI. Do not use primary-monitor metrics for a secondary-monitor tray.

---

# 5. Storage and Crash Recovery

## 5.1 Portable directory layout

At runtime, the expected directory is:

```text
TinyLog/
├── TinyLog.exe
├── TinyLog.jsonl          # completed sessions
├── TinyLog.state          # active or completing session
└── TinyLog.state.tmp      # transient atomic-write file; normally absent
```

Resolve the executable directory with `GetModuleFileNameW` using a dynamically resized buffer. Derive all application-owned paths from that directory. Do not silently fall back to AppData, LocalAppData, Documents, or another location if the directory is not writable. Report a clear error instead.

The export destination selected by the user is an explicit exception: an `.xlsx` file may be written anywhere the user selects.

## 5.2 Completed log: JSON Lines

`TinyLog.jsonl` contains one UTF-8 JSON object per line. Suggested schema:

```json
{
  "schema": 1,
  "id": "session-guid",
  "startUtc": "2026-11-17T01:00:00Z",
  "endUtc": "2026-11-17T01:45:30Z",
  "startLocal": "2026-11-17 09:00:00",
  "endLocal": "2026-11-17 09:45:30",
  "startOffsetMinutes": 480,
  "endOffsetMinutes": 480,
  "durationSeconds": 2730,
  "notes": "Implementing ticket ABC-123"
}
```

The example values are illustrative only. Use a unique GUID per session. Store UTC timestamps for elapsed-time calculation and retain local values/offsets for reporting. Preserve Unicode and newlines in notes through valid JSON escaping.

Recommended fields:

| Field | Meaning |
|---|---|
| `schema` | Record schema version |
| `id` | Unique session ID; used for idempotency |
| `startUtc`, `endUtc` | UTC timestamps |
| `startLocal`, `endLocal` | Captured local display timestamps |
| `startOffsetMinutes`, `endOffsetMinutes` | Local UTC offsets captured at the respective endpoints |
| `durationSeconds` | Persisted elapsed duration derived from UTC |
| `notes` | User-entered description/ticket |

## 5.3 Active state file

`TinyLog.state` stores the current recording or an in-progress completion transaction. Example:

```json
{
  "schema": 1,
  "id": "session-guid",
  "status": "recording",
  "startUtc": "2026-11-17T01:00:00Z",
  "startLocal": "2026-11-17 09:00:00",
  "startOffsetMinutes": 480,
  "notes": "Implementing ticket ABC-123",
  "pendingEndUtc": null,
  "pendingEndLocal": null,
  "pendingEndOffsetMinutes": null
}
```

Persist the state immediately when Start is clicked and on every notes change (`EN_CHANGE`). The state is intentionally small, so synchronous persistence is preferred over debounced persistence for the initial version.

### Atomic replacement procedure

1. Serialize to UTF-8.
2. Open/create `TinyLog.state.tmp` in the same directory.
3. Write all bytes and check short writes/errors.
4. Call `FlushFileBuffers` on the temporary file handle.
5. Close the temporary file.
6. Atomically replace/move it over `TinyLog.state` using a same-volume operation (`ReplaceFileW` when a destination exists; an appropriate `MoveFileExW` path for first creation).
7. Use write-through flags where supported and check every return code.

Keep `.tmp` in the same directory so replacement stays on the same volume. Recovery may inspect a leftover temporary file only if the authoritative state is missing or invalid; validate its schema and content before promotion. Preserve damaged files for diagnosis rather than silently destroying the only copy.

## 5.4 Start transaction

On Start:

1. Reject the command if a transition is already in progress.
2. Capture UTC time with `GetSystemTimePreciseAsFileTime` (with a supported fallback if necessary).
3. Capture corresponding local display time and current UTC offset.
4. Generate a GUID.
5. Read the notes editor contents.
6. Atomically persist `status = recording`.
7. Only after persistence succeeds, transition to Recording, update tray icon/tooltip, and change the primary button to Stop.

If persistence fails, remain Idle and display a clear error. Never claim Recording if the active state was not saved.

## 5.5 Stop transaction and idempotency

Do not simply append a log line and then delete the state file; a crash between the two can duplicate an entry on next startup.

Use this protocol:

1. Capture the end UTC/local timestamps and end offset.
2. Change active state to `status = completing` and persist `pendingEnd*` values atomically.
3. Construct the completed session record using the persisted start and pending end timestamps.
4. Check whether the same session ID is already present in the completed log (or maintain a bounded/indexed lookup if the log becomes very large).
5. If absent, append one complete JSONL line and call `FlushFileBuffers`.
6. If present, do not append again.
7. Delete `TinyLog.state` only after the completed record is known to be durable.
8. Reset UI and tray to Idle.

On recovery, a `completing` state is resumed using its stored end timestamp. The same session ID is used, so recovery cannot create a second logical record.

If the final JSONL line is incomplete or malformed because of a crash during append, validate line boundaries and truncate only the incomplete tail before appending. Do not discard earlier valid records.

## 5.6 Recovery matrix

| Condition at launch | Behaviour |
|---|---|
| No state file | Start Idle |
| Valid `recording` state | Restore notes and Recording state; elapsed display derives from stored start |
| Valid `completing` state, ID not in log | Append pending record, flush, delete state, start Idle |
| Valid `completing` state, ID already in log | Delete stale state, start Idle |
| Invalid state file | Preserve file, report recovery error; never silently start a new session and overwrite it |
| Leftover valid `.tmp`, no valid state | Validate and recover/promote according to deterministic rules |
| Incomplete final JSONL line | Preserve valid prefix and remove only malformed trailing bytes |

Exiting while Recording does not stop the session. Remove the tray icon and exit while leaving `TinyLog.state` intact. On next launch, restore the session. If Start with Windows is enabled, the shortcut will launch TinyLog at sign-in and it will restore the active session.

## 5.7 Time, midnight, daylight saving, and clock changes

Use UTC timestamps to calculate duration:

`durationSeconds = endUtc - startUtc`

Local timestamps are for display/export. Do not split sessions at midnight. The export Date is the local date on which the session started.

A session crossing midnight remains one row. A session crossing a daylight-saving boundary uses UTC to calculate elapsed duration and retains the start/end local offsets separately.

Across reboot, a monotonic performance counter cannot be trusted as a continuous clock. The persisted UTC wall-clock values are therefore the recovery source. Manual clock changes can affect a wall-clock-derived duration; without an external trusted clock, TinyLog cannot guarantee perfect elapsed time across arbitrary system-clock manipulation or power loss. Document this limitation.

---

# 6. Start with Windows — Startup Folder Shortcut

## 6.1 Location

Resolve the current user's Startup directory with:

`SHGetKnownFolderPath(FOLDERID_Startup, KF_FLAG_DEFAULT, nullptr, &startupPath)`

Release the returned path with `CoTaskMemFree`. Initialize COM on the calling thread before Shell COM work and balance initialization appropriately.

The shortcut path is:

`<Startup folder>\TinyLog.lnk`

Do not write a Run key or create a scheduled task.

## 6.2 Create or overwrite shortcut

Use native Shell COM interfaces:

1. `CoCreateInstance(CLSID_ShellLink, ..., IID_IShellLinkW)`.
2. `IShellLinkW::SetPath(currentExePath)`.
3. `IShellLinkW::SetWorkingDirectory(exeDirectory)`.
4. Query `IPersistFile`.
5. `IPersistFile::Save(shortcutPath, TRUE)`.
6. Release all interfaces and balance COM initialization.

Check every HRESULT. Do not leave a partial or malformed shortcut and report permission failures.

## 6.3 Verify state every time the tray menu opens

The menu checkmark must represent whether `TinyLog.lnk` resolves to the **currently running executable**, not merely whether a file with that name exists.

Algorithm:

1. Resolve Startup folder.
2. Check whether `TinyLog.lnk` exists.
3. Load it through `IPersistFile::Load`.
4. Resolve its target using `IShellLinkW::GetPath`.
5. Obtain the current executable path using `GetModuleFileNameW`.
6. Normalize absolute paths and compare case-insensitively. Where both targets exist, consider final-path resolution through file handles to account for aliases.
7. If paths match, mark Start with Windows checked.
8. If shortcut is stale or missing, mark unchecked. A stale TinyLog shortcut may be deleted as a cleanup action, with errors handled safely.

If the user moves the portable EXE, the old shortcut points to the previous path and must not be shown as enabled. The user can re-enable the option to create a shortcut pointing to the new location.

The Shell Link `GetPath` interface has path-length considerations; test paths near and beyond traditional `MAX_PATH` limits. Use extended-length path handling where supported and clearly document any Shell Link API limitation found during implementation.

## 6.4 Toggle behaviour

| Action | Result |
|---|---|
| Toggle ON | Create/overwrite `TinyLog.lnk` for current EXE |
| Toggle OFF | Delete `TinyLog.lnk` |
| Menu opened | Re-verify target and refresh checkmark |
| EXE moved | Old target mismatches; show unchecked and allow recreation |
| Shortcut deleted externally | Show unchecked |
| Startup folder unavailable or not writable | Keep app running; report operation failure |

---

# 7. Excel Export — Minimal OOXML Without Excel

## 7.1 Approach

An `.xlsx` workbook is an Open XML Spreadsheet package stored as a ZIP archive. TinyLog needs one worksheet and six columns, so a minimal custom writer is appropriate.

Do not use:

- Excel automation/COM automation;
- Microsoft Excel installation;
- Open XML SDK;
- EPPlus or ClosedXML;
- a general-purpose spreadsheet framework;
- an external ZIP DLL.

Implement a small ZIP writer that initially uses ZIP **Store** (no compression). XML files are small; avoiding DEFLATE reduces implementation and binary cost. The generated `.xlsx` remains a valid ZIP-based OOXML package.

## 7.2 Required columns

The worksheet must contain exactly these user-facing columns:

1. `Date`
2. `Start Time`
3. `End Time`
4. `Duration (Minutes)`
5. `Duration (HH:MM:SS)`
6. `Notes / Ticket`

Suggested example:

| Date | Start Time | End Time | Duration (Minutes) | Duration (HH:MM:SS) | Notes / Ticket |
|---|---|---|---:|---|---|
| 2026-11-17 | 09:00:00 | 09:45:30 | 45.50 | 00:45:30 | ABC-123 |
| 2026-11-17 | 10:00:00 | 11:15:00 | 75.00 | 01:15:00 | Code review |

Export completed sessions only. Do not export an active unfinished recording.

## 7.3 Excel cell values and formats

Use native Excel serial values rather than formatting every cell as text:

| Column | Value type | Number format |
|---|---|---|
| Date | Excel date serial | `yyyy-mm-dd` |
| Start Time | Fraction of day | `hh:mm:ss` |
| End Time | Fraction of day | `hh:mm:ss` |
| Duration (Minutes) | Numeric decimal | `0.00` |
| Duration (HH:MM:SS) | Fractional days | `[h]:mm:ss` |
| Notes / Ticket | Inline string | General |

The `[h]:mm:ss` format is essential because it allows durations longer than 24 hours to display without wrapping to zero. Use one documented Excel date epoch convention consistently (1900 date system is the common Windows default) and test leap-year/serial conversions.

For sessions crossing midnight, `Date` is the start local date. Start and end time are local clock times captured with their respective offsets. If the local timezone changes during a session, the two displayed local times can reflect different offsets; duration remains derived from UTC.

## 7.4 Minimal package structure

Required package parts for the chosen minimal workbook:

```text
[Content_Types].xml
_rels/.rels
xl/workbook.xml
xl/_rels/workbook.xml.rels
xl/styles.xml
xl/worksheets/sheet1.xml
```

A shared-string table is not required when cells use inline strings. Build a single worksheet named `TinyLog` or `Sessions`.

## 7.5 ZIP writer requirements

Implement:

- local file headers;
- file data;
- CRC32;
- central directory file headers;
- end-of-central-directory record.

Use UTF-8 XML. Set ZIP flags/method consistently. Store file entries without compression (method 0). Validate all lengths and offsets against classic ZIP limits; if a future export exceeds classic ZIP bounds, return a clear error rather than writing a corrupt archive. TinyLog's expected workbook should remain far below those limits.

## 7.6 XML and data safety

- Escape `&`, `<`, `>`, quotes where required by XML attributes, and apostrophes where appropriate.
- Remove or replace XML 1.0 forbidden control characters.
- Preserve newline content in notes using valid SpreadsheetML string handling.
- Always write notes as string cells, never formula cells.
- Do not interpret user text beginning with `=`, `+`, `-`, or `@` as a formula.
- Use inline strings with `xml:space="preserve"` where leading/trailing whitespace must survive.
- Use a deterministic UTF-8 XML declaration.

## 7.7 Native Save dialog

Use `IFileSaveDialog`, configured with:

- title such as `Export TinyLog sessions`;
- file type `Excel Workbook (*.xlsx)`;
- default extension `xlsx`;
- default filename `TinyLog_YYYY-MM-DD.xlsx`;
- filesystem path requirement;
- overwrite confirmation.

Call `Show`, then `GetResult`, retrieve the filesystem path, and write the workbook there. Treat user cancellation (`HRESULT_FROM_WIN32(ERROR_CANCELLED)`) as a normal outcome, not an error.

The Save dialog is a standard Windows Shell dialog. Excel is not started or required.

## 7.8 Export flow

1. User clicks Export to Excel (from bubble or tray menu).
2. Open `IFileSaveDialog`.
3. If cancelled, stop without writing.
4. Read/validate completed JSONL records, preferably streaming for large histories.
5. Sort chronologically if the log is not guaranteed to be append-ordered.
6. Generate XML parts and ZIP package.
7. Write to the chosen path using a temporary file in the destination directory, flush, then replace/rename into place so an interrupted export does not leave a seemingly complete corrupt `.xlsx`.
8. Report success/failure through a concise native notification or message surface that does not add permanent bubble controls.

---

# 8. State Machine, Reliability, and Edge Cases

## 8.1 State transitions

```text
Idle --Start--> Starting --state persisted--> Recording
Recording --Stop--> Completing --log flushed/state removed--> Idle
```

Reject Start/Stop commands while `Starting` or `Completing`. Keep all state transitions on the UI thread in the first implementation. Avoid background threads until measurements or a real long-running operation justify them.

## 8.2 Duplicate instance handling

Create a named mutex early in startup, for example:

`Local\\TinyLog.Singleton.v1`

If `CreateMutexW` indicates `ERROR_ALREADY_EXISTS`, send a registered activation message to the existing instance where possible, then exit without registering a second tray icon. Keep a handle to the mutex for the process lifetime.

A fixed Local-session mutex prevents duplicates in the same logon session, including copies launched from different portable directories. If cross-session single-instance semantics are ever required, revisit the namespace and security descriptor deliberately.

## 8.3 Crash/reboot behaviour

- Crash while Idle: next launch starts Idle.
- Crash while Recording: active state remains; next launch restores Recording.
- Crash while Completing: resume the idempotent completion transaction.
- User chooses Exit while Recording: leave active state intact; do not stop automatically.
- Windows restarts and startup shortcut exists: launch and restore active state.

## 8.4 Midnight and DST

Do not split a session at midnight. Keep a single record and use the start-local date in the Date column. Compute elapsed time from UTC timestamps. Preserve start/end offsets for display and forensic clarity.

## 8.5 Double-clicks and re-entrancy

Tray callbacks may contain single/double-click event patterns. Track current popup/animation state and ignore duplicate activation within a small bounded interval (initial suggestion: 250 ms). Do not depend solely on timing; state checks must make opening/closing idempotent.

For Start/Stop, disable the button during `Starting`/`Completing`. A short debounce (around 250–300 ms) may help accidental rapid repeat input, but state-machine locking is the real correctness guarantee.

## 8.6 Outside click and Escape

On `WM_ACTIVATE` deactivation, collapse the bubble unless the newly activated window is an owned/expected modal dialog such as `IFileSaveDialog`. Escape collapses the bubble but does not stop a running session. Clicking the tray icon while open collapses it. Collapsing never changes the timer state.

Do not inadvertently collapse while the Save dialog is active. Keep a modal-dialog/owned-window guard in the activation handler.

## 8.7 File and permissions failures

| Failure | Required behaviour |
|---|---|
| Cannot create state file | Do not enter Recording |
| Cannot update notes state | Keep session visible as Recording but surface persistence failure; prevent Stop from discarding recoverable data |
| Cannot append completed log | Keep `completing` state for recovery; do not clear UI/state as though successful |
| Cannot delete state after append | On next launch detect matching session ID and finish cleanup idempotently |
| EXE directory read-only | Report that portable data cannot be written; do not silently relocate data |
| Startup folder denied | Keep app operational; leave toggle unchecked and explain failure |
| Save destination inaccessible | Report export error; preserve original logs |
| Removable drive disconnected | Never silently delete or relocate the active state |

## 8.8 GDI resource ownership

Every GDI handle created by TinyLog must have one clear owner and release path:

- `HFONT` -> `DeleteObject`;
- `HBRUSH` -> `DeleteObject`;
- `HPEN` -> `DeleteObject`;
- `HRGN` -> `DeleteObject` unless ownership transferred to `SetWindowRgn`;
- compatible `HBITMAP` -> `DeleteObject`;
- compatible memory `HDC` -> `DeleteDC`;
- restore the previous selected object before deleting a selected GDI object.

Use small RAII wrappers for GDI handles, file handles, mutex handles, and COM interfaces. Avoid global unowned raw handles.

---

# 9. Performance and Memory Plan

## 9.1 Lazy initialization

At startup, initialize only:

- single-instance mutex;
- COM apartment only if startup recovery/shortcut work needs it (balance calls);
- hidden owner window;
- tray icon;
- local state recovery;
- message loop.

Do not initialize animation bitmaps, fonts, or bubble child controls until the bubble is first opened. If the bubble is destroyed when closed, release its GDI resources. Alternatively, keep the small window alive but hidden if benchmarks show this reduces churn without violating the idle budget; measure both strategies.

## 9.2 No idle polling

Do not run a one-second timer while idle. Do not poll the file system or registry. The application is event-driven.

A short animation timer is created only while opening/closing. A display timer for the elapsed-time text is only enabled while the bubble is visible and Recording; it can be one-second resolution because it is visual feedback, not the time source. Calculate displayed elapsed time from stored start UTC whenever the timer fires, rather than incrementing an integer.

## 9.3 Memory measurement

Record at least:

- `WorkingSetSize`;
- `PrivateUsage` / private bytes;
- peak working set;
- GDI object count;
- USER object count;
- thread count.

Use `GetProcessMemoryInfo` with `PROCESS_MEMORY_COUNTERS_EX`. Working set includes resident pages and can change due to Windows memory management; private usage is a different measure. Do not force a misleading small working set by repeatedly trimming it with `SetProcessWorkingSetSize`.

Measure at consistent points:

1. right after startup and recovery;
2. tray idle after 60 seconds;
3. bubble open;
4. after 100 open/close cycles;
5. after 1,000 note-change events;
6. after export of a large sample log.

## 9.4 Binary audit

Inspect release output using `dumpbin /headers` and `dumpbin /dependents`. The executable may depend on Windows system DLLs such as `KERNEL32`, `USER32`, `GDI32`, `SHELL32`, `OLE32`, `COMDLG32`, and `COMCTL32`; these are OS components, not bundled runtimes.

Do not add a dependency simply because it is convenient. Every library addition must be justified by size, maintenance, security, and portability measurements.

---

# 10. Project Structure and Build Blueprint

## 10.1 Suggested project layout

```text
TinyLog/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── resources/
│   ├── app.manifest
│   ├── TinyLog.rc
│   └── tinylog.ico
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── App.h
│   │   ├── App.cpp
│   │   ├── AppState.h
│   │   ├── SessionManager.h
│   │   └── SessionManager.cpp
│   ├── win32/
│   │   ├── TrayHost.h
│   │   ├── TrayHost.cpp
│   │   ├── TrayIcon.h
│   │   ├── TrayIcon.cpp
│   │   ├── TrayMenu.h
│   │   ├── TrayMenu.cpp
│   │   ├── BubbleWindow.h
│   │   ├── BubbleWindow.cpp
│   │   ├── GdiRenderer.h
│   │   ├── GdiRenderer.cpp
│   │   ├── LayeredAnimation.h
│   │   ├── LayeredAnimation.cpp
│   │   ├── NativeControls.h
│   │   ├── NativeControls.cpp
│   │   ├── StartupShortcut.h
│   │   ├── StartupShortcut.cpp
│   │   ├── SaveDialog.h
│   │   ├── SaveDialog.cpp
│   │   ├── SingleInstance.h
│   │   └── SingleInstance.cpp
│   ├── storage/
│   │   ├── SessionRecord.h
│   │   ├── StateStore.h
│   │   ├── StateStore.cpp
│   │   ├── JsonLinesStore.h
│   │   └── JsonLinesStore.cpp
│   ├── export/
│   │   ├── ExportController.h
│   │   ├── ExportController.cpp
│   │   ├── SpreadsheetWriter.h
│   │   ├── SpreadsheetWriter.cpp
│   │   ├── ZipWriter.h
│   │   ├── ZipWriter.cpp
│   │   ├── Crc32.h
│   │   └── Crc32.cpp
│   └── util/
│       ├── Win32Helpers.h
│       ├── FileHelpers.h
│       ├── TimeHelpers.h
│       ├── StringHelpers.h
│       ├── ComHelpers.h
│       └── ComHelpers.cpp
└── tests/
    ├── StateMachineTests.cpp
    ├── RecoveryTests.cpp
    ├── JsonLinesTests.cpp
    ├── SpreadsheetWriterTests.cpp
    ├── ZipWriterTests.cpp
    └── StartupShortcutTests.cpp
```

Keep the modules small. Do not add dependency injection, a service container, an event bus, or a plugin architecture.

## 10.2 Windows libraries

Link only the libraries actually used. Likely dependencies:

```text
user32.lib
gdi32.lib
shell32.lib
ole32.lib
uuid.lib
comdlg32.lib
comctl32.lib
shlwapi.lib        # only if a chosen path helper needs it
psapi.lib          # test/diagnostic memory reporting; may be excluded from shipping build
```

The precise linker list should be validated by the implementation. Do not link graphics libraries such as `d2d1`, `dwrite`, `dcomp`, `dxgi`, or `d3d11` in this revised architecture.

## 10.3 Build pipeline

1. Configure Release x64 with the Windows SDK and MSVC toolchain.
2. Compile the GUI subsystem executable.
3. Run unit tests separately; do not ship test binaries.
4. Run executable-size assertion.
5. Run dependency inspection.
6. Run registry-import/API audit.
7. Run memory/GDI handle smoke tests.
8. Produce the portable release folder containing `TinyLog.exe` only; user data appears beside it at runtime.

---

# 11. Implementation Phases

Recommended sequencing:

| Phase | Deliverable | Exit criteria |
|---|---|---|
| 1 | Build skeleton, hidden Win32 owner, mutex | Single-instance GUI process runs without a console |
| 2 | Tray icon and context menu | Stable GUID, correct callbacks, Explorer restart handling |
| 3 | Session model and durable storage | Start, note update, Stop, and recovery tests pass |
| 4 | Basic bubble and native controls | Exactly three interactive elements; keyboard editing works |
| 5 | Startup shortcut | Create, delete, verify, moved-EXE handling pass |
| 6 | Native Save dialog | Correct `.xlsx` filename/filter and cancellation behaviour |
| 7 | Minimal ZIP/OOXML exporter | Opens in Excel and LibreOffice; correct types and formats |
| 8 | Layered-window animation | Smooth open/close; no focus or child-control regressions |
| 9 | DPI, multi-monitor, taskbar edge cases | Test matrix passes |
| 10 | Footprint and reliability hardening | Size/memory/handle acceptance tests pass |

Implement and validate storage before spending time on animation polish.

---

# 12. Testing and Release Acceptance

## 12.1 Functional acceptance

- [ ] Start captures the exact start instant and durably writes active state before showing Recording.
- [ ] Notes persist on every text change.
- [ ] Stop stores its end instant, writes exactly one completed entry, flushes, and removes active state.
- [ ] Reboot/crash recovery restores an active recording.
- [ ] Interrupted Stop completes idempotently without duplicate records.
- [ ] Exit during recording preserves the session.
- [ ] Midnight-crossing sessions remain one row with correct duration.
- [ ] DST offset changes do not corrupt elapsed-duration calculations.
- [ ] Invalid/corrupt state does not silently overwrite user data.

## 12.2 Tray and startup acceptance

- [ ] Exactly one tray icon is registered.
- [ ] Idle and Recording tooltips/icons update correctly.
- [ ] Explorer restart causes icon re-registration.
- [ ] Menu contains exactly Start with Windows, separator, Export to Excel, Exit.
- [ ] Startup checkmark resolves the `.lnk` target and compares it to the current EXE.
- [ ] Enabling creates/overwrites `TinyLog.lnk` with target and working directory set correctly.
- [ ] Disabling deletes `TinyLog.lnk`.
- [ ] Moving the EXE causes stale shortcut detection.
- [ ] No Run key, service, scheduled task, or app-owned registry entry is created.

## 12.3 UI acceptance

- [ ] No taskbar button or Alt+Tab entry.
- [ ] Bubble contains exactly three interactive controls.
- [ ] Start with Windows is not shown in the bubble.
- [ ] Bubble opens above/near the actual tray icon.
- [ ] Popup remains within the correct monitor work area.
- [ ] Focus goes to notes input after expansion.
- [ ] Outside-click and Escape collapse without stopping a recording.
- [ ] Save dialog does not accidentally trigger outside-click dismissal.
- [ ] No GDI/USER object leak across repeated open/close cycles.

## 12.4 Display test matrix

Test at minimum:

- Single monitor at 100% DPI.
- Single monitor at 150% DPI.
- Two monitors at 100% and 150%.
- Three monitors with different resolutions/scales.
- Taskbar at bottom, top, left, and right.
- Taskbar auto-hide on/off.
- Tray icon in overflow flyout.
- Explorer restart while TinyLog is running.
- DPI change while the bubble is open.

## 12.5 Export acceptance

- [ ] `.xlsx` is a valid ZIP/OOXML package.
- [ ] All six required columns exist in the correct order.
- [ ] Dates and times are numeric Excel values with correct formats.
- [ ] `[h]:mm:ss` displays durations above 24 hours correctly.
- [ ] Unicode and multiline notes are preserved.
- [ ] Formula-like notes remain string data, never formulas.
- [ ] Empty history exports a valid workbook with headers.
- [ ] Large logs do not require loading all records into memory unnecessarily.
- [ ] Workbook opens in Microsoft Excel and LibreOffice.
- [ ] Excel is not required on the target computer.

## 12.6 Footprint acceptance

- [ ] Release EXE is below 3,000,000 bytes.
- [ ] No non-Windows runtime prerequisite exists.
- [ ] Idle private working set is measured below 15 MB on the reference test machine.
- [ ] Preferred idle working-set objective of 5–8 MB is recorded as achieved or not achieved.
- [ ] Private bytes, working set, peak working set, GDI count, USER count, and thread count are recorded.
- [ ] No idle polling occurs.
- [ ] No background worker thread remains while idle.
- [ ] Opening/closing 100 times does not steadily grow memory or GDI handle counts.

---

# 13. Risks, Trade-offs, and Engineering Notes

## 13.1 Visual fidelity vs footprint

GDI and window regions do not provide the same antialiased curves, blur, or compositor animation as Direct2D/DirectComposition. Accept a slightly simpler visual treatment. Use a restrained charcoal card, clear text hierarchy, high contrast, green recording state, rounded region, and a modest shadow. The product should feel intentional rather than attempting to replicate acrylic effects.

## 13.2 Animation reliability

The timer-driven animation is not compositor-native. Use `QueryPerformanceCounter` to calculate normalized progress and avoid drift. If a frame is late, jump to the correct interpolated frame rather than extending the animation. If the window loses its display or receives a DPI change mid-animation, cancel or restart from a valid final rectangle; never leave the popup partially transparent or off-screen.

## 13.3 Layered-window API choice

The design uses `SetLayeredWindowAttributes` for global alpha and GDI redraw. Do not call `UpdateLayeredWindow` on that same HWND unless the implementation deliberately resets the layered style and reinitializes the chosen path. The first release should not combine both approaches.

If later testing demonstrates that per-pixel alpha and a fully custom shadow are essential, evaluate a separate temporary animation-only layered HWND using `UpdateLayeredWindow` and a 32-bit premultiplied-alpha DIB. Keep native child controls in the normal interactive bubble HWND and swap between the snapshot window and interactive window at animation boundaries. This is a possible enhancement, not the baseline design.

## 13.4 Storage durability limits

`FlushFileBuffers` and same-directory replacement greatly improve durability, but no user-space file protocol can promise survival against every storage-device firmware failure, filesystem corruption, or sudden hardware failure. Test on local NTFS and removable media. Make errors explicit.

## 13.5 Timekeeping limits

UTC wall-clock timestamps allow reboot recovery but can be affected by manual clock changes. A monotonic counter is excellent during one process lifetime but cannot be assumed continuous across reboot. Do not claim perfect wall-clock-independent timing across power loss without an external time source.

## 13.6 System dependencies are not bundled runtime dependencies

The PE imports Windows system components. That is compatible with a no-installer Windows desktop application. It does not mean the executable is literally self-contained at the OS DLL level; no normal Win32 application is. State the supported Windows versions and list required Windows system DLL imports in the release audit.

---

# 14. Reference Documentation

The following Microsoft documentation is the primary API reference for implementation. Re-check requirements and API notes against the minimum supported Windows version during development.

## Layered windows and GDI

- [Window Features — Layered Windows](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features)
- [UpdateLayeredWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-updatelayeredwindow)
- [SetLayeredWindowAttributes](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setlayeredwindowattributes)
- [CreateRoundRectRgn](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createroundrectrgn)
- [SetWindowRgn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowrgn)

## DPI and memory

- [WM_DPICHANGED](https://learn.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged)
- [High DPI Reference](https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-reference)
- [Process Memory Usage Information](https://learn.microsoft.com/en-us/windows/win32/psapi/process-memory-usage-information)
- [PROCESS_MEMORY_COUNTERS_EX](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex)

## Shell, Startup, and dialogs

- [SHGetKnownFolderPath](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetknownfolderpath)
- [Known Folder IDs](https://learn.microsoft.com/en-us/windows/win32/shell/knownfolderid)
- [IShellLinkW](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ishelllinkw)
- [IPersistFile](https://learn.microsoft.com/en-us/windows/win32/api/objidl/nn-objidl-ipersistfile)
- [IFileSaveDialog](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ifilesavedialog)
- [Shell_NotifyIconW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw)
- [Shell_NotifyIconGetRect](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyicongetrect)

## Spreadsheet format

- [Open XML Spreadsheet reference](https://learn.microsoft.com/en-us/office/open-xml/spreadsheet/overview)
- [Structure of a SpreadsheetML document](https://learn.microsoft.com/en-us/office/open-xml/spreadsheet/structure-of-a-spreadsheetml-document)

---

# 15. Final Recommendation

Freeze the first implementation around:

**C++20 + Win32 + GDI + `SetLayeredWindowAttributes` + native controls + JSONL/state recovery + Startup Shell Link + custom ZIP/OOXML export.**

Do not reintroduce Direct2D, DirectWrite, DirectComposition, D3D11, or DXGI unless a measured requirement demonstrates that GDI cannot meet the product's visual needs.

Build in this order:

1. Single-instance tray process.
2. Session state machine and crash-safe storage.
3. Native bubble and three controls.
4. Startup shortcut management.
5. Native Save dialog and valid XLSX writer.
6. Layered-window animation and DPI polish.
7. Footprint/reliability testing.

The most important quality attribute is not animation smoothness or a sub-megabyte executable. It is that a user's active work session is never silently lost or duplicated. The ultra-light UI is valuable only if the storage and recovery behaviour is trustworthy.
