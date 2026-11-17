# TinyLog — project instructions

Portable, tray-only Windows work/time logger (`TinyLog.exe`). Dark bubble popup with exactly three
interactive controls: notes box, Start/Stop, Export to Excel. Data lives beside the EXE.

## Key documents
- Spec: `.claude/plans/TinyLog_Technical_Design_UltraLight.md` (cited as §n)
- Plan: `.claude/plans/implementation-plan.md` — waves, work packages (WPs), frozen contracts
- Progress: `.claude/PROGRESS.md` — update after every WP merge, contract change, or decision

## Fixed stack (do not change without a measured reason)
- C++20, MSVC x64, Win32 + GDI, `/MT` (Release) / `/MTd` (Debug), CMake + Ninja
- Windows SDK libs only. No third-party libraries, no MFC/ATL/Boost/Qt, no D2D/DWrite/DComp/D3D.
- COM via `Microsoft::WRL::ComPtr` (header-only SDK).

## Build & test
- `cmd //c 'tools\build.cmd debug'` (from Bash — keep the single quotes) or `tools\build.cmd release` (cmd) — runs vcvars64,
  configures, builds, runs `TinyLogTests` via CTest.
- Toolchain: `C:\Program Files\Microsoft Visual Studio\18\Enterprise` (MSVC 14.51, bundled CMake/Ninja),
  Windows SDK 10.0.26100.0. Nothing is on PATH until `vcvars64.bat` runs.
- Release EXE must be < 3,000,000 bytes (`tools/check-size.ps1`); `tools/audit.ps1` must pass.
- Run the app from a copy in `%TEMP%\TinyLogRun\`, never from the repo (it writes data beside the EXE).

## Versioning
- Semantic Versioning. Single source of truth: `project(TinyLog VERSION x.y.z)` in `CMakeLists.txt`; CMake generates
  `version.h` and the manifest from `resources/version.h.in` / `resources/app.manifest.in`. Never hard-code a version.
- Release = bump `VERSION`, add a `CHANGELOG.md` entry, commit, tag `vX.Y.Z`.

## Git
- Public repo: `origin` = github.com/aaronbrataaditama/TinyLog, branch `main`. The owner pushes; never push unless asked.
- Commits meant for the public repo must use the GitHub noreply identity
  (`aaronbrataaditama@users.noreply.github.com`), never a work email or real name.

## Code rules (all contributors, incl. WP agents)
- Edit only files your WP owns (see plan's ownership table). Never edit frozen contract headers,
  `CMakeLists.txt`, `CMakePresets.json`, or `tests/TestHarness.h` — report proposed changes instead.
  Exception: `private:` members of a class whose `.cpp` you own may be changed.
- Code compiles with `/W4 /WX /permissive- /GR-`; fix warnings rather than suppressing them.
- No registry APIs (`Reg*`, `SHReg*`), no background threads, no idle timers/polling, no exceptions
  thrown by our code, no RTTI use.
- Return `Status` (util/Status.h); check every Win32 return code and HRESULT.
- RAII for every handle: `UniqueHandle`, `UniqueGdi<T>`, `UniqueDC`, `SelectGuard`, `ComPtr`.
- Durability first: never lose or duplicate a session (Spec §5). Animation polish comes last.

## Product decisions
- Notes stay in the box after Stop; at startup in Idle, prefill with the last completed session's notes.
  Notes typed while Idle are in memory only until Start.
- Invalid state file is quarantined (renamed `TinyLog.state.corrupt-*`), never overwritten or deleted.
