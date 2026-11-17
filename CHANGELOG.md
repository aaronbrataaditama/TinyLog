# Changelog

All notable changes to TinyLog are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/): **MAJOR** for breaking changes to the data files,
**MINOR** for new features, **PATCH** for fixes.

## [Unreleased]

## [1.0.0] - 2026-11-17

First release.

### Added
- Tray-only work logger: left-click the tray icon for a small dark bubble with a notes box,
  Start/Stop and Export to Excel; right-click for Start with Windows, Export to Excel and Exit.
- Crash-safe sessions: the running session is saved on Start and on every notes change, restored after a
  crash, reboot or exit, and an interrupted Stop is completed exactly once.
- Excel export (`.xlsx`) written without Excel installed, with real date/time cells, `[h]:mm:ss` durations
  and notes always stored as text.
- Optional Start with Windows via a Startup-folder shortcut.
- Portable single exe (about 0.5 MB) that keeps its data in `TinyLog.jsonl` and `TinyLog.state` beside it.
- Stump mascot tray icon, with a green badge while recording.
- Version information (File/Product version) in the exe's properties.
