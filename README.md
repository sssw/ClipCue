# ClipCue

ClipCue is a Windows Explorer enhancement that adds context-menu commands for fast file/folder copy and move operations. It learns successful ClipCue and external Explorer operations, shares target history between Move to and Copy to, prioritizes targets that match the current source path, surfaces detected target folders as quick suggestions, supports conflict policies, UAC elevation fallback, and a per-user DPAPI-encrypted history store.

> Status: engineering preview. The repository is designed to compile on GitHub Actions `windows-latest` with CMake and Visual Studio. The classic Explorer context menu, encrypted append-only history store, worker, elevated helper, settings CLI, native control panel UI, per-user background monitor with tray icon, external Explorer copy/cut/paste learning, clipboard file queue, clipboard text history/editor, per-user installer, cache generation, and core file operation path are implemented. USN journal learning, SQLCipher, and Windows 11 `IExplorerCommand` are documented extension points and intentionally disabled in the default build.

## License

Apache License 2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

## Build locally

```powershell
cmake -S . -B build -A x64 -DCLIPCUE_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Output binaries are under:

```text
build/Release/
```

Create a local distributable ZIP:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package-local.ps1 -BuildDir build\Release -Output dist\ClipCue-local.zip
```

or for multi-config Visual Studio generators:

```text
build/x64/Release/
```

## GitHub Actions

The repository includes `.github/workflows/windows.yml`. It builds x64 and Win32 Release artifacts and uploads ZIP artifacts via `actions/upload-artifact@v4`.

## Install from a GitHub Actions artifact

Unzip the artifact and run:

```powershell
.\ClipCue.Installer.exe
```

The installer performs a per-user install by default, registers the classic Explorer context menu under `HKCU`, enables `ClipCue.Monitor.exe` at sign-in through the current-user Run key, starts the tree tray icon, creates Start Menu shortcuts, and leaves user data under `%LOCALAPPDATA%\ClipCue` intact during uninstall. See [docs/INSTALLER_UI.md](docs/INSTALLER_UI.md).

Open the control panel with:

```powershell
.\ClipCue.UI.exe
```

Open the dedicated Path Clip Queue workspace with:

```powershell
.\ClipCue.UI.exe queue
```

## Quick local test

After building Release x64:

```powershell
cd build\Release
.\ClipCue.Settings.exe add-pin both C:\Temp Temp
.\ClipCue.Settings.exe build-cache
.\ClipCue.Settings.exe show
```

Register the classic Explorer extension for the current user:

```powershell
powershell -ExecutionPolicy Bypass -File ..\..\scripts\register-classic.ps1 -BuildDir .
```

Restart Explorer or sign out/sign in if the menu does not appear immediately.

Unregister:

```powershell
powershell -ExecutionPolicy Bypass -File ..\..\scripts\unregister-classic.ps1 -BuildDir .
```

## Implemented components

| Component | Target | Status |
|---|---|---|
| `ClipCue.ShellClassic.dll` | Explorer classic context menu | Implemented |
| `ClipCue.Agent.exe` | job execution, folder picker, history writes | Implemented |
| `ClipCue.Worker.exe` | direct job execution | Implemented |
| `ClipCue.Elevated.exe` | UAC helper | Implemented |
| `ClipCue.Settings.exe` | pinned and detected targets, cleanup, cache | Implemented CLI |
| `ClipCue.Monitor.exe` | tray monitor, external clipboard/shell-change learning | Implemented |
| `ClipCue.UI.exe` | native control panel and dedicated Path Clip Queue workspace | Implemented GUI |
| `ClipCue.Installer.exe` | per-user installer/uninstaller | Implemented GUI + silent CLI |
| DPAPI encrypted history store | per-user local history | Implemented |
| Source-path prioritized target ranking | history/pinned/detected route scoring | Implemented |
| Recycle-before-overwrite | Shell recycle fallback | Implemented |
| Clipboard/shell-change learning | external Explorer copy/cut/paste inference and quick suggestions | Implemented |
| Shared target history | `ClipCue Move to...` and `ClipCue Copy to...` use the same learned/pinned target history | Implemented |
| Clipboard file queue | multiple copy/cut clipboard actions can be previewed, edited, and repeatedly applied to target folders | Implemented |
| Dedicated Path Clip Queue UI | standalone queue workspace with active/history status, entry editing, target folder selection, and previewed apply actions | Implemented |
| Clipboard duplicate coalescing | repeated identical clipboard actions are kept as one entry with a repeat count | Implemented |
| Clipboard text history/editor | text clipboard history has a dedicated Text page for select, merge, edit, copy, or paste | Implemented |
| USN/CopyHook learning | optional external operation enhancement | Extension point |
| SQLCipher backend | full SQLite encryption | Extension point |
| Windows 11 modern menu | `IExplorerCommand` | Extension point |

## Job file format

```text
op=copy
source=C:\Users\ws\Downloads\a.pdf
target=D:\Archive
conflict=ask
overwrite=ask
allow_elevation=1
```

Run directly:

```powershell
.\ClipCue.Worker.exe --job job.txt --result result.txt
```

## Registering the Shell Extension

The project uses per-user COM registration under `HKCU\Software\Classes`, so admin rights are not required for the classic menu registration.

```powershell
scripts\register-classic.ps1 -BuildDir build\Release
```

## Security notes

The default store is `%LOCALAPPDATA%\ClipCue\Data\clipcue.db`. Each record is protected with Windows DPAPI `CurrentUser`. The legacy menu cache is plaintext under `%LOCALAPPDATA%\ClipCue\Cache\menu_cache.tsv` because Explorer context-menu handlers must not open the encrypted store or block on decryption. It includes pinned targets, target folders detected from copy/move/clipboard activity, active queue counts, and queue-history counts so `ClipCue Move to...`, `ClipCue Copy to...`, and `ClipCue Clipboard Queue...` can render quickly. You can delete this file to disable cached quick-target display; the menu still offers manual target selection for selected files.

See [docs/SECURITY.md](docs/SECURITY.md).
