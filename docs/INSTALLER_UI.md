# Installer and UI

ClipCue includes three dependency-free, native Windows applications in addition to the command-line tools. This page documents the modernized installer and control-center workflows.

## `ClipCue.Installer.exe`

The installer is a per-user Win32 installer. It does not require WiX, NSIS, MSIX, or administrator rights. The modern installer presents the setup flow as three clear stages: install location, recommended integrations, and finish/open actions.

It can:

- copy the compiled ClipCue binaries into `%LOCALAPPDATA%\Programs\ClipCue` by default;
- install to a custom absolute folder selected with the folder picker;
- register `ClipCue.ShellClassic.dll` for the current user under `HKCU\Software\Classes`;
- write `HKCU\Software\ClipCue\InstallDir` so the Explorer extension can find `ClipCue.Agent.exe`;
- verify, auto-start, and launch `ClipCue.Monitor.exe` as the current-user background monitor and tree tray icon;
- create Start Menu shortcuts for the Control Center, Path Clip Queue, monitor, installer, and uninstaller;
- create an Add/Remove Programs entry under the current-user uninstall registry key;
- launch the Control Center and optionally the Path Clip Queue immediately after install;
- uninstall the shell extension and installed files while leaving user data/history under `%LOCALAPPDATA%\ClipCue` intact.

### GUI use

Run:

```powershell
.\ClipCue.Installer.exe
```

Use the default location or choose a custom absolute folder. Keep the recommended integration options checked for the smoothest workflow: Explorer menu registration, Start Menu shortcuts, and the background monitor. Click **Install / Repair**. The log pane shows every file and integration step.

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
- open the native Control Center;
- open the ClipCue data folder;
- rebuild the Explorer menu cache;
- run 90-day history cleanup;
- toggle current-user auto-start;
- exit the monitor for the current session.

## `ClipCue.UI.exe`

`ClipCue.UI.exe` is the modern native Control Center. It is still implemented with dependency-free Win32 controls, but the workflow is organized around user tasks rather than implementation details.

Run it directly:

```powershell
.\ClipCue.UI.exe
```

or open it through the Start Menu shortcut created by the installer.

### Control Center pages

#### Targets & path queue

This page combines target management and queue execution in one workflow:

- pin quick targets and choose whether they appear for copy, move, or both;
- review smart targets detected from previous ClipCue and Explorer activity;
- double-click a pinned or suggested target to use it as the queue target;
- open the selected target folder directly from the UI;
- review the active path clipboard queue and inspect full entry details;
- mark selected queue entries as copy, move, skipped, or active;
- save the current queue selection;
- archive the active queue into history;
- apply the prepared queue to a selected folder with **Apply all**, **Copy only**, or **Move only**.

#### Text editor

The Text editor page turns clipboard text history into an editable workspace:

- select one or more text clips and merge them into the editor;
- select all clips with one click;
- clear the editor;
- normalize whitespace for quick cleanup;
- copy edited text back to the clipboard;
- paste edited text back to the previously focused application when possible;
- see live character and line counts while editing.

#### System status

The System status page provides diagnostics and maintenance:

- show the encrypted history store path and plaintext menu-cache path;
- show pinned quick-target, operation-record, path-queue, and text-history counts;
- show recent ClipCue operations;
- register or unregister the classic Explorer context menu for the current user;
- rebuild the Explorer menu cache;
- run 90-day history cleanup;
- open the ClipCue data folder;
- launch the installer.

### Dedicated Path Clip Queue workspace

`ClipCue.UI.exe queue` opens a focused queue-only window for users who want to keep the batch apply flow separate from the full Control Center.

```powershell
.\ClipCue.UI.exe queue
```

The dedicated workspace supports queue inspection, copy/move/skip/activate actions, target folder selection, applying the queue, opening the full Control Center, and refreshing the queue state.

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
