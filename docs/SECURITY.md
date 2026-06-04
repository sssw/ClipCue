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

This file is intentionally plaintext because the Explorer in-process extension should not hold the database key or perform slow database work. It only contains target labels and target directories needed to render quick menu entries. Delete the file to disable quick cached entries.

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
- uninstall entry: `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\PathCue`.

The installer does not request administrator rights and does not create a `LocalSystem` service. Uninstall removes binaries and shell registration but intentionally leaves `%LOCALAPPDATA%\PathCue\Data` intact so users do not lose encrypted history accidentally.

## UI security model

`PathCue.UI.exe` opens the encrypted DPAPI record store through the same library as the command-line settings tool. It can add/remove pinned targets, rebuild the plaintext Explorer menu cache, and register/unregister the classic context menu for the current user. It does not run elevated by default.
