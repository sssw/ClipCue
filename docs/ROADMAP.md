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
- Per-user installer/uninstaller.

## 0.2 storage and privacy

- SQLCipher backend behind `IHistoryStore`.
- Structured schema and migrations.
- Per-directory deletion.
- Secure compaction.
- Password key slot.

## 0.3 external learning

- Clipboard monitor: `CF_HDROP` and preferred drop effect.
- File system event collector: `ReadDirectoryChangesW`.
- Correlation engine and confidence scoring.

## 0.4 Windows 11 modern shell

- `IExplorerCommand` extension.
- Sparse package identity.
- Top-level modern menu.

## 1.0 complete

- Signed installer.
- Enterprise policy support.
- Optional USN Journal reader.
- Recovery manifests and operation journal viewer.
