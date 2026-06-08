# Modern UI workflow patch

This patch redesigns the native Win32 surfaces without introducing a new runtime dependency. It keeps the existing CMake and Visual Studio build path, but reorganizes the user experience around task-focused workspaces.

## Control Panel

- **Overview** summarizes pinned targets, active path queue, learned operations, and text history.
- **Targets** adds a polished quick-target manager with custom labels, operation intent, folder picking, detected suggestions, cache rebuild, and cleanup.
- **Path Queue** brings the dedicated queue workflow into the main panel: review entries, set copy/move intent, skip or activate entries, choose a target, and apply.
- **Text Studio** expands clipboard text history into an editor with merge-on-select, file import/export, clear, copy, and paste-forward actions.
- **System** centralizes data folder access, shell registration, installer launch, menu cache rebuild, and cleanup.

## Dedicated Path Queue

`ClipCue.UI.exe queue` now opens a larger focused window for clipboard path review and apply actions. It uses the same store and cache operations as the main panel.

## Installer

The installer is still a per-user installer, but now uses a single-screen professional flow:

1. choose or reset the install path,
2. select integrations,
3. inspect the plan,
4. install, repair, uninstall, or open folders.

The silent command-line behavior is intentionally preserved:

```powershell
.\ClipCue.Installer.exe --install --silent --dir "$env:LOCALAPPDATA\Programs\ClipCue"
.\ClipCue.Installer.exe --uninstall --silent
```

## Local application

Run the patch from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\apply-clipcue-modern-ui.ps1
```

The script creates a branch, rewrites the UI and installer files, updates CMake link libraries, adds this document, and commits the change.
