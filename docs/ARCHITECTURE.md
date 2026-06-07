# ClipCue Architecture

```text
Explorer.exe
  -> ClipCue.ShellClassic.dll
     -> IShellExtInit
     -> IContextMenu
     -> reads plaintext menu cache for Move/Copy quick-target submenus
     -> shows ClipCue Clipboard Queue on folder/background targets
     -> offers an Open Path Clip Queue entry that launches ClipCue.UI.exe queue
     -> launches ClipCue.Agent.exe with a temporary job file or queue command

ClipCue.Agent.exe
  -> folder picker
  -> queue preview before applying recorded clipboard file actions
  -> job execution
  -> UAC fallback through ClipCue.Elevated.exe
  -> encrypted history write
  -> menu cache generation

ClipCue.Worker.exe
  -> direct job execution for scripts/tests

ClipCue.Elevated.exe
  -> requireAdministrator manifest; executes one job and returns result

ClipCue.Settings.exe
  -> add pinned target
  -> show store state
  -> list clipboard history
  -> clear selected file clipboard queue
  -> cleanup expired history
  -> rebuild menu cache

ClipCue.Monitor.exe
  -> per-user background monitor
  -> tree tray icon in the notification area
  -> status, auto-start toggle, control panel and Path Clip Queue launch
  -> observes file clipboard data (`CF_HDROP` + preferred drop effect)
  -> observes text clipboard data (`CF_UNICODETEXT`)
  -> records continuous file copy/cut clipboard actions as selected queue entries
  -> marks the previous active file queue as history when a new file clipboard queue appears
  -> records text clipboard history for later merge/edit/copy/paste
  -> coalesces consecutive identical clipboard actions into one history entry with a repeat count
  -> subscribes to Shell/file-system change notifications on available drive roots
  -> correlates external paste/move results into inferred encrypted history records
  -> clears selected file queue entries after a paste/move result is detected
  -> cache rebuild and history cleanup shortcuts

ClipCue.UI.exe
  -> native Win32 control panel
  -> dedicated Path Clip Queue workspace when launched with the queue argument
  -> Paths page for pinned targets, detected quick targets, and selected path queue entries
  -> queue detail panel with Copy, Move, Skip, and Activate editing controls
  -> target-folder selection and previewed queue apply actions in the dedicated queue workspace
  -> Text page for selecting, merging, editing, copying, and pasting text clipboard history
  -> Status page for monitor/store status and recent operations
  -> copy or paste edited combined text
  -> rebuild menu cache
  -> cleanup history
  -> register/unregister the classic menu

ClipCue.Installer.exe
  -> verifies, enables, and starts ClipCue.Monitor.exe for the current user
  -> per-user install/repair
  -> copies build artifacts to %LOCALAPPDATA%\Programs\ClipCue
  -> registers ClipCue.ShellClassic.dll under HKCU
  -> creates Start Menu shortcuts and uninstall registry entry
  -> uninstalls binaries while leaving user data intact
```

## Implemented design decisions

1. The Explorer DLL does not open the encrypted history store.
2. The Explorer DLL does not perform file copy/move work in-process.
3. Successful ClipCue operations are written as encrypted history records.
4. Menu ranking prioritizes exact source-parent matches, pinned targets, detected target folders, recent successful routes, and existing targets.
5. Permission failures can be retried by `ClipCue.Elevated.exe` through UAC.
6. The installer is per-user by default and does not require administrator rights.
7. The UI is native Win32 with no external GUI framework dependency; `ClipCue.UI.exe queue` opens a standalone Path Clip Queue workspace.
8. The monitor is a per-user tray process, not a machine-wide Windows service.
9. External Explorer operations are recorded only after the monitor can correlate file clipboard data or Shell rename/create notifications with an actual file-system result.
10. Move and Copy quick-target menus share the same learned/pinned target history; the command operation still follows the menu the user clicked.
11. Continuous file copy/cut clipboard actions remain active after ClipCue applies them so the same queue can be reused across multiple target folders.
12. Queue entries can be edited after capture: Copy, Move, Skip, or Activate.
13. A new file clipboard queue or an externally detected paste/move marks the previous active queue as history, not deletion.
14. Consecutive identical clipboard actions are stored as one entry and increment a repeat count instead of creating visual noise.
15. Text clipboard content is stored in the encrypted record store and can be merged/edited before being copied or pasted again.
16. The menu cache includes global recent targets, active queue counts, and queue-history counts so Explorer can render quickly without decrypting history.

## Extension points

The preview intentionally keeps external dependencies out of the default build. The intended production-grade extensions are:

- `StorageProviderSqlCipher`: SQLite/SQLCipher backend.
- `UsnJournalReader`: optional NTFS journal enhancement.
- `CopyHookFolderPolicy`: optional folder copy/move policy hook investigation.
- `ShellModern`: Windows 11 `IExplorerCommand` implementation using sparse package identity.
