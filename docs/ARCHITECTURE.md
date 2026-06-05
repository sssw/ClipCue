# PathCue Architecture

```text
Explorer.exe
  └─ PathCue.ShellClassic.dll
       ├─ IShellExtInit
       ├─ IContextMenu
       ├─ reads plaintext legacy menu cache for Move/Copy quick-target submenus
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

PathCue.Monitor.exe
  - per-user background monitor
  - tree tray icon in the notification area
  - status, auto-start toggle, control panel launch
  - observes external file clipboard data (`CF_HDROP` + preferred drop effect)
  - subscribes to Shell/file-system change notifications on available drive roots
  - correlates external paste/move results into inferred encrypted history records
  - cache rebuild and history cleanup shortcuts

PathCue.UI.exe
  ├─ native Win32 control panel
  ├─ add/remove pinned targets
  ├─ show recent operations
  ├─ rebuild menu cache
  ├─ cleanup history
  └─ register/unregister the classic menu

PathCue.Installer.exe
  - verifies, enables, and starts PathCue.Monitor.exe for the current user
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
4. Menu ranking prioritizes exact source-parent matches, pinned targets, detected target folders, recent successful routes, and existing targets.
5. Permission failures can be retried by `PathCue.Elevated.exe` through UAC.
6. The installer is per-user by default and does not require administrator rights.
7. The UI is a native Win32 control panel with no external GUI framework dependency.
8. The monitor is a per-user tray process, not a machine-wide Windows service.
9. External Explorer operations are recorded only after the monitor can correlate file clipboard data or Shell rename/create notifications with an actual file-system result.
10. The menu cache includes global recent targets so folders detected from user copy/move/clipboard activity can appear in new source directories as quick suggestions.

## Extension points

The preview intentionally keeps external dependencies out of the default build. The intended production-grade extensions are:

- `StorageProviderSqlCipher`: SQLite/SQLCipher backend.
- `UsnJournalReader`: optional NTFS journal enhancement.
- `CopyHookFolderPolicy`: optional folder copy/move policy hook investigation.
- `ShellModern`: Windows 11 `IExplorerCommand` implementation using sparse package identity.
