# Installer and UI

ClipCue now includes three user-facing Windows applications in addition to the command-line tools.

## `ClipCue.Installer.exe`

The installer is a dependency-free per-user installer. It is intentionally implemented as a native Win32 executable instead of requiring WiX, NSIS, MSIX, or external build tooling.

It can:

- copy the compiled ClipCue binaries into `%LOCALAPPDATA%\Programs\ClipCue` by default;
- register `ClipCue.ShellClassic.dll` for the current user under `HKCU\Software\Classes`;
- write `HKCU\Software\ClipCue\InstallDir` so the Explorer extension can find `ClipCue.Agent.exe`;
- verify, auto-start, and launch `ClipCue.Monitor.exe` as the current-user background monitor and tree tray icon;
- create Start Menu shortcuts;
- create an Add/Remove Programs entry under the current user uninstall registry key;
- uninstall the shell extension and installed files while leaving user data/history under `%LOCALAPPDATA%\ClipCue` intact.

### GUI use

Run:

```powershell
.\ClipCue.Installer.exe
```

Choose the install directory, keep the default options checked, and click **Install / Repair**. The default options include the background monitor, which starts immediately and again at future sign-ins.

### Silent install

```powershell
.\ClipCue.Installer.exe /install /silent
```

Install to a custom folder:

```powershell
.\ClipCue.Installer.exe /install /silent /dir "D:\Tools\ClipCue"
```

### Silent uninstall

```powershell
.\ClipCue.Installer.exe /uninstall /silent
```

The installer is per-user by design and does not require administrator rights. It does not delete `%LOCALAPPDATA%\ClipCue\Data` during uninstall.

## `ClipCue.Monitor.exe`

`ClipCue.Monitor.exe` is the per-user background monitor. It is not a `LocalSystem` Windows service; it runs in the signed-in user's session so it can show a tree icon in the notification area.

The tray menu can:

- show monitor, install, Explorer menu, store, cache, pinned-target, and operation-record status;
- show whether clipboard and Shell/file-change monitoring are active;
- learn external Explorer copy/cut/paste and move routes by correlating file clipboard data with Shell/file-system changes;
- rebuild quick menu suggestions from pinned targets and detected target folders;
- open the dedicated Path Clip Queue workspace;
- open the native control panel;
- open the ClipCue data folder;
- rebuild the Explorer menu cache;
- run 90-day history cleanup;
- toggle current-user auto-start;
- exit the monitor for the current session.

## `ClipCue.UI.exe`

`ClipCue.UI.exe` is the native control panel.

`ClipCue.UI.exe queue` opens the dedicated Path Clip Queue workspace.

It can:

- show the encrypted history store path and plaintext menu-cache path;
- show pinned quick targets;
- show detected quick targets used by `ClipCue Move to...` and `ClipCue Copy to...`;
- add a pinned target using a folder picker;
- remove a pinned target;
- rebuild the Explorer menu cache;
- run 90-day history cleanup;
- show recent ClipCue operations;
- register or unregister the classic Explorer context menu for the current user;
- open the ClipCue data folder;
- launch the installer.

Run it directly:

```powershell
.\ClipCue.UI.exe
```

or open it through the Start Menu shortcut created by the installer.

## CLI still available

`ClipCue.Settings.exe` remains available for scripted workflows:

```powershell
.\ClipCue.Settings.exe show
.\ClipCue.Settings.exe add-pin both C:\Temp Temp
.\ClipCue.Settings.exe build-cache
.\ClipCue.Settings.exe cleanup 90
```

## Build artifact layout

GitHub Actions stages the following user-facing files in the ZIP artifact:

```text
ClipCue.Agent.exe
ClipCue.Worker.exe
ClipCue.Elevated.exe
ClipCue.Settings.exe
ClipCue.Monitor.exe
ClipCue.UI.exe
ClipCue.Installer.exe
ClipCue.ShellClassic.dll
LICENSE
NOTICE
README.md
docs\
scripts\
```

For normal users, the simplest path is to unzip the artifact and run `ClipCue.Installer.exe`.
