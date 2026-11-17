# TinyLog — Implementation Progress

Plan: [plans/implementation-plan.md](plans/implementation-plan.md) · Integration branch: `feat/tinylog-v1`

Status values: `todo` · `in-progress` · `review` · `merged` · `blocked`

## Work packages

| WP | Wave | Model | Scope | Status | Branch | Commit | Tests | Notes |
|---|---|---|---|---|---|---|---|---|
| S0 | 0 | Opus | Bootstrap: CLAUDE.md, plan, PROGRESS.md | merged | feat/tinylog-v1 | | — | |
| W0 | 0 | Opus | Contracts, CMake, harness, stubs | merged | feat/tinylog-v1 | (W0 commit) | 0 tests; debug+release green | Release stub EXE 104,448 B |
| A | 1 | Haiku | Pure utilities (string/time/crc/file) | merged | worktree-agent-ad98e8adf910c6355 | 688b662 | 23 Util_ pass | GetExeDirectory keeps `C:\` at drive root |
| B | 1 | Haiku | Icons, manifest, rc, size/audit scripts, README | merged | worktree-agent-a054786758dc3e170 | eb01318 | release build OK; audit PASS | 5-size PNG icons; EXE 110,592 B |
| E | 1 | Sonnet | SingleInstance, TrayIcon, TrayMenu, StartupShortcut, SaveDialog | merged | worktree-agent-aa211654bb8e57a89 | c53c643 | 6 Startup_ pass | Tray/menu/dialog/Enable verified only in WP-H; lnk saved via tmp+move |
| F2 | 1 | Haiku | BubblePlacement + LayeredAnimation math | merged | worktree-agent-ad10b56f24c5e976a | 7f9a7e5 (+905d72c) | 18 Placement_/Anim_ pass | Orchestrator fix: taskbar edge detected from icon vs work area |
| F1 | 1 | Sonnet | BubbleWindow, GdiRenderer, NativeControls, Theme, BubbleDemo | merged | worktree-agent-af0b4d681f9c74bf6 | ace93f7 | build OK; demo checked at ~125% DPI by agent | Not yet verified: 100/150% DPI, click-outside, GDI leak loop, non-Above sides |
| C | 2 | Sonnet | Storage + SessionManager | merged | worktree-agent-a6b98c52844fc8d84 | a77b618 | 27 Storage_ + 20 Session_ pass | Valid-but-unterminated last line kept (newline added); Start blocked while unrecoverable state file present |
| D | 2 | Sonnet | ZipWriter, SpreadsheetWriter, ExportController | merged | worktree-agent-aadc66ce52e45937f | eb3d805 | 14 Zip_/Sheet_ pass (1 skipped until C) | Sample: %TEMP%\TinyLogSample.xlsx — open in Excel/LibreOffice still pending |
| H | 3 | Sonnet | App integration (main, App) | merged | worktree-agent-a9d2ef8f5da103f3e | 29e0f3e | smoke test: single instance + recording/completing recovery pass | UI needs manual test; orchestrator fixed ShowBalloon truncation |
| R | 4 | Opus→Sonnet | Review, footprint, hardening, test matrix | in-progress | wp/r-hardening | | | |

## Contract changes

| Date | Header | Change | Requested by | Approved |
|---|---|---|---|---|
| | | | | |

## Decisions

| Date | Decision | Reason |
|---|---|---|
| 2026-11-17 | Notes stay in the box after Stop; Idle startup prefills last session's notes | User choice |
| 2026-11-17 | `TrayHost` (Spec §10.1) folded into `App` | One fewer contract |
| 2026-11-17 | Invalid state file is quarantined (`TinyLog.state.corrupt-*`), app continues Idle | Spec §5.6: preserve, never overwrite |
| 2026-11-17 | Export rows in log order, no sort | Single instance ⇒ append order is chronological |
| 2026-11-17 | Tray GUID with fallback to hWnd+uID | Windows binds NIF_GUID to EXE path; portable EXE may move |
| 2026-11-17 | ZIP Store with seek-back header patching (no data descriptors) | Streaming export without loading all rows |
| 2026-11-17 | In-house header-only test harness | No third-party dependencies |
| 2026-11-17 | App/tray icon = stump mascot (`resources/tinylog.png`); recording = same with green badge | User request |
| 2026-11-17 | Do NOT auto-promote the tray icon out of overflow (would need HKCU NotifyIconSettings write) | User: keep the no-registry rule; user pins it in Settings |

## Footprint measurements (Spec §9.3 / §12.6)

| Point | Working set | Private bytes | Peak WS | GDI | USER | Threads |
|---|---|---|---|---|---|---|
| After startup | 15,868 KB (private WS 2,180 KB) | 2,832 KB | 15,868 KB | 10 | 6 | 4 (startup threads, settle to 1) |
| Idle 60 s | 15,948 KB | 2,740 KB | 16,000 KB | 10 | 6 | 1 — idle CPU 0 ms over 10 s |
| Bubble open | 19,056 KB | 3,268 KB | 19,056 KB | 24 | 17 | 1 |
| After 100 open/close | 21,528 KB (private WS 2,732 KB) | 3,556 KB | 21,528 KB | 24 (flat) | 19 | 3 |
| After 1,000 note changes | | | | | | |
| After large export | | | | | | |

EXE size (Release): ~297 KB (limit 3,000,000). Measured 2026-11-17 with `tools/measure.ps1`. WS includes shared system-DLL pages; the Spec's *private* working set is 2.2–2.7 MB. Not yet measured: 1,000 note changes, large export.

## Spec §12 acceptance

### Functional (§12.1)
- [ ] Start captures exact start instant and durably writes active state before showing Recording
- [ ] Notes persist on every text change
- [ ] Stop stores end instant, writes exactly one completed entry, flushes, removes active state
- [x] Reboot/crash recovery restores an active recording (smoke)
- [x] Interrupted Stop completes idempotently without duplicate records (unit + smoke)
- [ ] Exit during recording preserves the session
- [x] Midnight-crossing sessions remain one row with correct duration (unit)
- [x] DST offset changes do not corrupt elapsed duration (unit)
- [x] Invalid/corrupt state does not silently overwrite user data (unit)

### Tray & startup (§12.2)
- [ ] Exactly one tray icon registered
- [ ] Idle/Recording tooltips and icons update correctly
- [ ] Explorer restart re-registers the icon
- [ ] Menu: Start with Windows, separator, Export to Excel, Exit — exactly
- [ ] Startup checkmark resolves `.lnk` target vs current EXE
- [ ] Enabling creates/overwrites `TinyLog.lnk` with correct target and working dir
- [ ] Disabling deletes `TinyLog.lnk`
- [ ] Moving the EXE causes stale shortcut detection
- [ ] No Run key, service, scheduled task, or app-owned registry entry

### UI (§12.3)
- [ ] No taskbar button or Alt+Tab entry
- [ ] Bubble has exactly three interactive controls
- [ ] Start with Windows not shown in bubble
- [ ] Bubble opens above/near the actual tray icon
- [ ] Popup stays within the correct monitor work area
- [ ] Focus goes to notes after expansion
- [ ] Outside-click and Escape collapse without stopping recording
- [ ] Save dialog does not trigger outside-click dismissal
- [ ] No GDI/USER leak across repeated open/close

### Display matrix (§12.4)
- [ ] 100% DPI · [ ] 150% DPI · [ ] 2 monitors 100%/150% · [ ] 3 monitors mixed
- [ ] Taskbar bottom · [ ] top · [ ] left · [ ] right · [ ] auto-hide on/off
- [ ] Icon in overflow flyout · [ ] Explorer restart · [ ] DPI change while bubble open

### Export (§12.5)
- [ ] Valid ZIP/OOXML package
- [ ] Six columns in correct order
- [ ] Dates/times numeric with correct formats
- [ ] `[h]:mm:ss` shows durations > 24 h
- [ ] Unicode and multiline notes preserved
- [ ] Formula-like notes stay strings
- [ ] Empty history → valid workbook with headers
- [ ] Large logs streamed, not fully loaded
- [ ] Opens in Excel and LibreOffice
- [ ] Excel not required to export

### Footprint (§12.6)
- [x] Release EXE < 3,000,000 bytes
- [x] No non-Windows runtime prerequisite (audit.ps1 PASS)
- [x] Idle private working set < 15 MB (2.2 MB)
- [x] 5–8 MB preferred target — achieved (private WS 2.2 MB; total WS 15.9 MB)
- [ ] All six memory/handle metrics recorded
- [x] No idle polling (0 ms CPU while idle)
- [x] No background worker thread while idle (1 thread)
- [x] 100 open/close cycles: GDI flat at 24; private bytes +0.3 MB, USER +2 — recheck in manual pass

## Log

- 2026-11-17 — S0: repo bootstrapped on `feat/tinylog-v1` (CLAUDE.md, plan, PROGRESS.md).
- 2026-11-17 — W0: contracts, CMake/presets, build.cmd, test harness, stubs, placeholder icons/manifest. Debug + Release build green; ctest passes. Rule added: implementers may change private members of their classes.
- 2026-11-17 — Wave 1: B, F2, A, E merged (47 tests green). Placement side choice changed to taskbar-edge detection. Wave 2 (C, D) started while F1 still running.
- 2026-11-17 — F1 merged; Wave 1 complete (all 5). Full debug build + 47 tests green.
- 2026-11-17 — D merged (61 tests green). Waiting on C.
- 2026-11-17 — C merged; Wave 2 complete. 108 tests green. Wave 3 (H) started.
- 2026-11-17 — H merged; Wave 3 complete. Release build green. TrayIcon::ShowBalloon now truncates (was a crash risk on long messages).
- 2026-11-17 — Opus review: 8 findings (no high). Fix batches running in parallel:
  - Storage/session (Sonnet): #1 failed Stop lets typing overwrite the completing state; #4 RepairTail truncates a terminated-but-unparsable last line; #6 transient read error quarantines a valid state (adds LoadKind::IoError — approved contract change).
  - UI seam (Sonnet): #2 second launch can't take foreground (AllowSetForegroundWindow); #3 tray menu/Exit usable during Save dialog; #5 double-click opens then closes; #7 font swap leaves EDIT with deleted HFONT; #8 recovery balloon lost if tray add fails.
- 2026-11-17 — Review fixes merged (117 tests). Footprint measured (tools/measure.ps1). Remaining: manual UI/display/Excel checks by user.
- 2026-11-17 — New mascot icons committed (9acf113). Release rebuild pending: buildelease\TinyLog.exe was locked by the user's running instance.
