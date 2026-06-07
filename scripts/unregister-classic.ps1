param(
  [Parameter(Mandatory=$false)]
  [string]$BuildDir = "."
)

$ErrorActionPreference = "Stop"
$dll = Join-Path $BuildDir "ClipCue.ShellClassic.dll"
if (Test-Path $dll) {
  $full = (Resolve-Path $dll).Path
  & regsvr32.exe /s /u $full
  if ($LASTEXITCODE -ne 0) {
    Write-Warning "regsvr32 unregister returned $LASTEXITCODE; continuing registry cleanup."
  }
}
Remove-Item -Path "HKCU:\Software\ClipCue" -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "Unregistered ClipCue classic shell extension for current user."
