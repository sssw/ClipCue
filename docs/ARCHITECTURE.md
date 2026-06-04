# PathCue Architecture

```text
Explorer.exe
  └─ PathCue.ShellClassic.dll
       ├─ IShellExtInit
       ├─ IContextMenu
       ├─ reads plaintext legacy menu cache
       └─ launches PathCue.Agent.exe with a temporary job file

PathCue.Agent.exe
  ├─ folder picker
  ├─ job execution
  ├─ UAC fallback through PathCue.Elevated.exe
  ├─ encrypted history write
  └─ menu cache generation

PathCue.Worker.exe
  └─ direct job execution for scripts/tests

PathCue.Elevated.exe
  └─ requireAdministrator manifest; executes one job and returns result

PathCue.Settings.exe
  ├─ add pinned target
  ├─ show store state
  ├─ cleanup expired history
  └─ rebuild menu cache

PathCue.UI.exe
  ├─ native Win32 control panel
  ├─ add/remove pinned targets
  ├─ show recent operations
  ├─ rebuild menu cache
  ├─ cleanup history
  └─ register/unregister the classic menu

PathCue.Installer.exe
  ├─ per-user install/repair
  ├─ copies build artifacts to %LOCALAPPDATA%\Programs\PathCue
  ├─ registers PathCue.ShellClassic.dll under HKCU
  ├─ creates Start Menu shortcuts and uninstall registry entry
  └─ uninstalls binaries while leaving user data intact
```

## Implemented design decisions

1. The Explorer DLL does not open the encrypted history store.
2. The Explorer DLL does not perform file copy/move work in-process.
3. Successful PathCue operations are written as encrypted history records.
4. Menu ranking prioritizes exact source-parent matches, pinned targets, recent successful routes, and existing targets.
5. Permission failures can be retried by `PathCue.Elevated.exe` through UAC.
6. The installer is per-user by default and does not require administrator rights.
7. The UI is a native Win32 control panel with no external GUI framework dependency.

## Extension points

The preview intentionally keeps external dependencies out of the default build. The intended production-grade extensions are:

- `StorageProviderSqlCipher`: SQLite/SQLCipher backend.
- `ClipboardMonitor`: `CF_HDROP` + preferred drop effect collection.
- `FileSystemEventCollector`: `ReadDirectoryChangesW` correlation.
- `UsnJournalReader`: optional NTFS journal enhancement.
- `ShellModern`: Windows 11 `IExplorerCommand` implementation using sparse package identity.
