# Installer and UI

PathCue now includes two user-facing Windows applications in addition to the command-line tools.

## `PathCue.Installer.exe`

The installer is a dependency-free per-user installer. It is intentionally implemented as a native Win32 executable instead of requiring WiX, NSIS, MSIX, or external build tooling.

It can:

- copy the compiled PathCue binaries into `%LOCALAPPDATA%\Programs\PathCue` by default;
- register `PathCue.ShellClassic.dll` for the current user under `HKCU\Software\Classes`;
- write `HKCU\Software\PathCue\InstallDir` so the Explorer extension can find `PathCue.Agent.exe`;
- create Start Menu shortcuts;
- create an Add/Remove Programs entry under the current user uninstall registry key;
- uninstall the shell extension and installed files while leaving user data/history under `%LOCALAPPDATA%\PathCue` intact.

### GUI use

Run:

```powershell
.\PathCue.Installer.exe
```

Choose the install directory, keep the default options checked, and click **Install / Repair**.

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

## `PathCue.UI.exe`

`PathCue.UI.exe` is the native control panel.

It can:

- show the encrypted history store path and plaintext menu-cache path;
- show pinned quick targets;
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
