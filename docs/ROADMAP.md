# Roadmap

## 0.1 engineering preview

- Classic context menu.
- Folder picker.
- IFileOperation copy/move.
- DPAPI encrypted append-only history.
- Source-path prioritized ranking.
- Pinned targets.
- UAC elevated helper.
- GitHub Actions Windows build.
- Native control panel UI.
- Per-user background monitor and tray icon.
- Clipboard/shell-change learning for external Explorer copy, cut, paste, and move routes.
- Per-user installer/uninstaller.

## 0.2 storage and privacy

- SQLCipher backend behind `IHistoryStore`.
- Structured schema and migrations.
- Per-directory deletion.
- Secure compaction.
- Password key slot.

## 0.3 external learning hardening

- Optional USN journal reader for recovery after monitor downtime.
- Optional CopyHook investigation for folder-specific policy scenarios.
- Operation journal viewer and per-record deletion.

## 0.4 Windows 11 modern shell

- `IExplorerCommand` extension.
- Sparse package identity.
- Top-level modern menu.

## 1.0 complete

- Signed installer.
- Enterprise policy support.
- Optional USN Journal reader.
- Recovery manifests and operation journal viewer.
