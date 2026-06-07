param(
  [Parameter(Mandatory=$false)]
  [string]$BuildDir = "."
)

$ErrorActionPreference = "Stop"
$dll = Join-Path $BuildDir "ClipCue.ShellClassic.dll"
if (!(Test-Path $dll)) {
  throw "ClipCue.ShellClassic.dll not found in $BuildDir"
}
$agent = Join-Path $BuildDir "ClipCue.Agent.exe"
if (!(Test-Path $agent)) {
  Write-Warning "ClipCue.Agent.exe not found beside the DLL. The context menu can register, but commands may not run."
}

$full = (Resolve-Path $dll).Path
$installDir = Split-Path $full -Parent
New-Item -Path "HKCU:\Software\ClipCue" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\ClipCue" -Name "InstallDir" -Value $installDir

& regsvr32.exe /s $full
if ($LASTEXITCODE -ne 0) {
  throw "regsvr32 failed with exit code $LASTEXITCODE"
}
Write-Host "Registered ClipCue classic shell extension for current user."
Write-Host "If Explorer does not show the menu, restart Explorer or sign out/in."
