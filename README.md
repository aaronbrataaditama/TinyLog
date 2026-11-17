<p align="center">
  <img src="resources/tinylog.png" alt="TinyLog mascot: a friendly tree stump holding a pencil and a work-task checklist" width="200">
</p>

<h1 align="center">TinyLog</h1>

<p align="center">
  <b>A tiny stump of a time tracker that lives in your system tray.</b><br>
  Jot what you're working on, press Start, press Stop. Export to Excel when you need it.
</p>

<p align="center">
  Windows 10 / 11 &nbsp;·&nbsp; one portable <code>.exe</code> (~0.5 MB) &nbsp;·&nbsp; no installer &nbsp;·&nbsp; no account &nbsp;·&nbsp; no network
</p>

---

## Why TinyLog?

You don't need a time-tracking *platform*. You need to answer "what did I do today, and for how long?" without
opening a browser, signing in, or fighting a form.

TinyLog is one click away in the tray:

```
 ┌──────────────────────────────────────────┐
 │ ●  TinyLog  │               ⏱ 00:12:34   │
 │ ┌──────────────────────────────────────┐ │
 │ │ Fix login issue on customer portal   │ │
 │ │ (ticket #48271)                      │ │
 │ └──────────────────────────────────────┘ │
 │  [ ■ Stop ]        [ X Export to Excel ] │
 └─────────────────────┬────────────────────┘
                       ▼  (tray icon)
```

A small dark bubble, exactly three controls: a **notes box**, a **Start/Stop** button and **Export to Excel**.
That's the whole app.

## Features

- **One click, no ceremony.** Left-click the stump, type a note or ticket number, press **Start**. Click away and
  it keeps timing quietly in the background. The tray icon gets a green badge while you're recording.
- **Your work survives surprises.** The running session is saved to disk on Start and on every edit to your notes.
  Crash, power cut, forced restart, or just closing TinyLog while recording: next launch picks up right where you
  left off. A session that was mid-Stop is finished once, never duplicated.
- **Excel export without Excel.** Writes a real `.xlsx` itself, so Excel doesn't even have to be installed. Real
  date and time cells, durations in both minutes and `HH:MM:SS` (long sessions over 24 h display correctly), and
  your notes always stay as plain text, so a note like `=1+1` can't turn into a formula.
- **Genuinely portable.** Copy `TinyLog.exe` anywhere and run it. Your data lives next to it in two small
  text files you can read, back up or delete.
- **Featherweight.** About 0.5 MB on disk and roughly 2 MB of private memory when idle, with 0% CPU. No background
  threads, no polling, no telemetry.
- **Private by design.** No network connections at all. No registry writes. No account.
- **Optional autostart.** Right-click the icon → **Start with Windows** drops a shortcut in your Startup folder.
  Untick it and it's gone.

## Quick start

There's no prebuilt download yet, so you build it once:

1. Install **Visual Studio** with the *Desktop development with C++* workload (MSVC x64 + Windows SDK).
2. From the repo folder run:

   ```
   tools\build.cmd release
   ```

3. Copy `build\release\TinyLog.exe` to any folder you can write to, and double-click it.
4. Look for the stump in the tray. Windows 11 tucks new icons under the `^` arrow, so drag it onto the taskbar, or
   go to **Settings → Personalization → Taskbar → Other system tray icons** and turn on **TinyLog**.

## Using it

| You want to… | Do this |
|---|---|
| Open the bubble | Left-click the tray icon |
| Start a session | Type your note, press **Start** |
| Edit the note mid-session | Just type; it's saved as you go |
| Stop | Press **Stop**. Your note stays in the box, ready for the next round |
| Close the bubble | Click elsewhere, or press **Esc** (a running session keeps running) |
| Export | **Export to Excel** in the bubble, or right-click the icon → **Export to Excel** |
| Autostart / quit | Right-click the icon → **Start with Windows** / **Exit** |

Exiting while recording does **not** stop the session. It's still running when you start TinyLog again.

### What the spreadsheet looks like

| Date | Start Time | End Time | Duration (Minutes) | Duration (HH:MM:SS) | Notes / Ticket |
|---|---|---|---:|---|---|
| 2026-11-17 | 09:00:00 | 09:45:30 | 45.50 | 00:45:30 | ABC-123 |
| 2026-11-17 | 10:00:00 | 11:15:00 | 75.00 | 01:15:00 | Code review |

Sessions that cross midnight stay one row, dated by when they started.

## Your data

Two plain-text files, written beside `TinyLog.exe`:

- `TinyLog.jsonl`: one JSON line per finished session. It's your log, so back it up like any file.
- `TinyLog.state`: the session in progress. It only exists while recording or while a Stop is being finalised.
  If it ever can't be read, TinyLog renames it to `TinyLog.state.corrupt-*` instead of deleting it.

Run TinyLog from a folder you can write to; if it can't write next to itself it tells you rather than quietly
saving somewhere else. If you move the exe, take the two files with it. A second launch just opens the existing
bubble instead of starting another copy.

## Under the hood

Built to prove a useful desktop tool doesn't need a framework:

- **C++20 + raw Win32 + GDI.** No Qt, no Electron, no .NET, no WebView, no third-party libraries at all.
- The `.xlsx` writer, ZIP packer, JSON log and crash-safe storage are about 1,400 lines of hand-written code.
- Crash-safe storage: atomic file replacement, flushed writes, and an idempotent Stop that can be safely retried.
- 100+ unit tests covering recovery, the session state machine, ZIP/XLSX output and window placement.
- A release build must stay under 3 MB and import only standard Windows DLLs. `tools\audit.ps1` checks both, and
  also fails the build if any registry API is imported.

Curious how it was designed? See [`.claude/plans/TinyLog_Technical_Design_UltraLight.md`](.claude/plans/TinyLog_Technical_Design_UltraLight.md)
for the full spec and [`.claude/plans/implementation-plan.md`](.claude/plans/implementation-plan.md) for how it was built.

## Known limitations

- **Clock changes:** durations come from the wall clock. If the system time jumps during a session (manual change,
  time sync, daylight-saving edge), the recorded duration reflects it.
- **Hardware limits:** TinyLog flushes to disk at every step, but no software can guarantee survival of a storage
  failure or sudden hardware loss. Keep a backup of `TinyLog.jsonl`.
- **No history view:** there's deliberately no in-app list or editor for old sessions. Use the Excel export.
- **Windows only:** 64-bit Windows 10 (version 1703 or later) and Windows 11.
- **Unsigned exe:** Windows SmartScreen may warn the first time you run a copy that came from a download or
  network share. Choose *More info → Run anyway*.

## Developing

```
tools\build.cmd debug      # build + run all tests
tools\build.cmd release    # size check + tests
powershell -NoProfile -ExecutionPolicy Bypass -File tools\audit.ps1 -Path build\release\TinyLog.exe
```

Tray and window artwork is generated from `resources\tinylog.png` by `tools\make-icons.ps1`. Project rules and the
progress log live in [`.claude/`](.claude/). `BubbleDemo.exe` (built alongside) opens just the popup for
visual testing; it is not part of the release.
