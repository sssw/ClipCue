# PathCue

PathCue is a Windows Explorer enhancement that adds context-menu commands for fast file/folder copy and move operations. It learns successful PathCue and external Explorer operations, prioritizes targets that match the current source path, surfaces detected target folders as quick suggestions, supports conflict policies, UAC elevation fallback, and a per-user DPAPI-encrypted history store.

> Status: engineering preview. The repository is designed to compile on GitHub Actions `windows-latest` with CMake and Visual Studio. The classic Explorer context menu, encrypted append-only history store, worker, elevated helper, settings CLI, native control panel UI, per-user background monitor with tray icon, external Explorer copy/cut/paste learning, per-user installer, cache generation, and core file operation path are implemented. USN journal learning, SQLCipher, and Windows 11 `IExplorerCommand` are documented extension points and intentionally disabled in the default build.

## License

Apache License 2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

## Build locally

```powershell
cmake -S . -B build -A x64 -DPATHCUE_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Output binaries are under:

```text
build/Release/
```

Create a local distributable ZIP:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package-local.ps1 -BuildDir build\Release -Output dist\PathCue-local.zip
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
.\PathCue.Installer.exe
```

The installer performs a per-user install by default, registers the classic Explorer context menu under `HKCU`, enables `PathCue.Monitor.exe` at sign-in through the current-user Run key, starts the tree tray icon, creates Start Menu shortcuts, and leaves user data under `%LOCALAPPDATA%\PathCue` intact during uninstall. See [docs/INSTALLER_UI.md](docs/INSTALLER_UI.md).

Open the control panel with:

```powershell
.\PathCue.UI.exe
```

## Quick local test

After building Release x64:

```powershell
cd build\Release
.\PathCue.Settings.exe add-pin both C:\Temp Temp
.\PathCue.Settings.exe build-cache
.\PathCue.Settings.exe show
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
| `PathCue.ShellClassic.dll` | Explorer classic context menu | Implemented |
| `PathCue.Agent.exe` | job execution, folder picker, history writes | Implemented |
| `PathCue.Worker.exe` | direct job execution | Implemented |
| `PathCue.Elevated.exe` | UAC helper | Implemented |
| `PathCue.Settings.exe` | pinned and detected targets, cleanup, cache | Implemented CLI |
| `PathCue.Monitor.exe` | tray monitor, external clipboard/shell-change learning | Implemented |
| `PathCue.UI.exe` | native control panel | Implemented GUI |
| `PathCue.Installer.exe` | per-user installer/uninstaller | Implemented GUI + silent CLI |
| DPAPI encrypted history store | per-user local history | Implemented |
| Source-path prioritized target ranking | history/pinned/detected route scoring | Implemented |
| Recycle-before-overwrite | Shell recycle fallback | Implemented |
| Clipboard/shell-change learning | external Explorer copy/cut/paste inference and quick suggestions | Implemented |
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
.\PathCue.Worker.exe --job job.txt --result result.txt
```

## Registering the Shell Extension

The project uses per-user COM registration under `HKCU\Software\Classes`, so admin rights are not required for the classic menu registration.

```powershell
scripts\register-classic.ps1 -BuildDir build\Release
```

## Security notes

The default store is `%LOCALAPPDATA%\PathCue\Data\pathcue.db`. Each record is protected with Windows DPAPI `CurrentUser`. The legacy menu cache is plaintext under `%LOCALAPPDATA%\PathCue\Cache\menu_cache.tsv` because Explorer context-menu handlers must not open the encrypted store or block on decryption. It includes pinned targets and target folders detected from copy/move/clipboard activity so `PathCue Move to...` and `PathCue Copy to...` can show quick suggestions. You can delete this file to disable cached quick-target display; the menu still offers both submenus with a manual target picker.

See [docs/SECURITY.md](docs/SECURITY.md).
