# Installer and UI

PathCue now includes three user-facing Windows applications in addition to the command-line tools.

## `PathCue.Installer.exe`

The installer is a dependency-free per-user installer. It is intentionally implemented as a native Win32 executable instead of requiring WiX, NSIS, MSIX, or external build tooling.

It can:

- copy the compiled PathCue binaries into `%LOCALAPPDATA%\Programs\PathCue` by default;
- register `PathCue.ShellClassic.dll` for the current user under `HKCU\Software\Classes`;
- write `HKCU\Software\PathCue\InstallDir` so the Explorer extension can find `PathCue.Agent.exe`;
- verify, auto-start, and launch `PathCue.Monitor.exe` as the current-user background monitor and tree tray icon;
- create Start Menu shortcuts;
- create an Add/Remove Programs entry under the current user uninstall registry key;
- uninstall the shell extension and installed files while leaving user data/history under `%LOCALAPPDATA%\PathCue` intact.

### GUI use

Run:

```powershell
.\PathCue.Installer.exe
```

Choose the install directory, keep the default options checked, and click **Install / Repair**. The default options include the background monitor, which starts immediately and again at future sign-ins.

### Silent install

```powershell
.\PathCue.Installer.exe /install /silent
```

Install to a custom folder:

```powershell
.\PathCue.Installer.exe /install /silent /dir "D:\Tools\PathCue"
```

### Silent uninstall

```powershell
.\PathCue.Installer.exe /uninstall /silent
```

The installer is per-user by design and does not require administrator rights. It does not delete `%LOCALAPPDATA%\PathCue\Data` during uninstall.

## `PathCue.Monitor.exe`

`PathCue.Monitor.exe` is the per-user background monitor. It is not a `LocalSystem` Windows service; it runs in the signed-in user's session so it can show a tree icon in the notification area.

The tray menu can:

- show monitor, install, Explorer menu, store, cache, pinned-target, and operation-record status;
- show whether clipboard and Shell/file-change monitoring are active;
- learn external Explorer copy/cut/paste and move routes by correlating file clipboard data with Shell/file-system changes;
- rebuild quick menu suggestions from pinned targets and detected target folders;
- open the native control panel;
- open the PathCue data folder;
- rebuild the Explorer menu cache;
- run 90-day history cleanup;
- toggle current-user auto-start;
- exit the monitor for the current session.

## `PathCue.UI.exe`

`PathCue.UI.exe` is the native control panel.

It can:

- show the encrypted history store path and plaintext menu-cache path;
- show pinned quick targets;
- show detected quick targets used by `PathCue Move to...` and `PathCue Copy to...`;
- add a pinned target using a folder picker;
- remove a pinned target;
- rebuild the Explorer menu cache;
- run 90-day history cleanup;
- show recent PathCue operations;
- register or unregister the classic Explorer context menu for the current user;
- open the PathCue data folder;
- launch the installer.

Run it directly:

```powershell
.\PathCue.UI.exe
```

or open it through the Start Menu shortcut created by the installer.

## CLI still available

`PathCue.Settings.exe` remains available for scripted workflows:

```powershell
.\PathCue.Settings.exe show
.\PathCue.Settings.exe add-pin both C:\Temp Temp
.\PathCue.Settings.exe build-cache
.\PathCue.Settings.exe cleanup 90
```

## Build artifact layout

GitHub Actions stages the following user-facing files in the ZIP artifact:

```text
PathCue.Agent.exe
PathCue.Worker.exe
PathCue.Elevated.exe
PathCue.Settings.exe
PathCue.Monitor.exe
PathCue.UI.exe
PathCue.Installer.exe
PathCue.ShellClassic.dll
LICENSE
NOTICE
README.md
docs\
scripts\
```

For normal users, the simplest path is to unzip the artifact and run `PathCue.Installer.exe`.
