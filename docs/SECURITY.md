# Security and Privacy

## Default protection

PathCue uses a dependency-free DPAPI record store in this engineering preview. Records are encrypted with Windows DPAPI `CurrentUser`, so the normal case requires the same Windows user profile to decrypt the data.

The store path is:

```text
%LOCALAPPDATA%\PathCue\Data\pathcue.db
```

## Plaintext menu cache

The classic Explorer extension reads:

```text
%LOCALAPPDATA%\PathCue\Cache\menu_cache.tsv
```

This file is intentionally plaintext because the Explorer in-process extension should not hold the database key or perform slow database work. It only contains target labels and target directories needed to render the `PathCue Move to...` and `PathCue Copy to...` quick-path submenus, including pinned targets and target folders detected from file copy/move/clipboard activity. Delete the file to disable quick cached entries.

## Elevated helper

`PathCue.Elevated.exe` has a `requireAdministrator` manifest. It executes one job and writes a result file. It does not open the encrypted history database.

## Recommended production hardening

- Replace the preview record store with SQLCipher or SQLite SEE for a relational encrypted database.
- Add IPC authentication for long-running Agent mode.
- Keep the Shell extension on a short timeout and never block Explorer on network paths.
- Add path canonicalization policies for system directories.
- Add deny-by-default handling for moving Windows, Program Files, and profile roots.
- Avoid writing full paths to crash logs.

## Installer security model

`PathCue.Installer.exe` performs a per-user install by default:

- install directory: `%LOCALAPPDATA%\Programs\PathCue`;
- COM registration: `HKCU\Software\Classes`;
- product registry state: `HKCU\Software\PathCue`;
- background monitor auto-start: `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`;
- uninstall entry: `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\PathCue`.

The installer does not request administrator rights and does not create a `LocalSystem` service. `PathCue.Monitor.exe` is a current-user tray process so it can show status and settings in the notification area. Uninstall removes binaries, shell registration, and monitor auto-start but intentionally leaves `%LOCALAPPDATA%\PathCue\Data` intact so users do not lose encrypted history accidentally.

## Monitor privacy model

`PathCue.Monitor.exe` observes file paths from the user's file clipboard (`CF_HDROP` plus the Shell preferred drop effect) and Shell/file-system change notifications on available drive roots. It records only inferred source-parent and destination-parent routes after a copy, cut/paste, or move can be correlated with an actual file-system result. It does not read file contents, and the Explorer in-process extension still does not open the encrypted history database.

## UI security model

`PathCue.UI.exe` opens the encrypted DPAPI record store through the same library as the command-line settings tool. It can add/remove pinned targets, show detected quick-target suggestions, rebuild the plaintext Explorer menu cache, and register/unregister the classic context menu for the current user. It does not run elevated by default.
