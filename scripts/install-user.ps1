param(
  [Parameter(Mandatory=$true)]
  [string]$BuildDir
)

$ErrorActionPreference = "Stop"
$install = Join-Path $env:LOCALAPPDATA "Programs\ClipCue"
New-Item -ItemType Directory -Force -Path $install | Out-Null
Copy-Item (Join-Path $BuildDir "ClipCue.*.exe") $install -Force
Copy-Item (Join-Path $BuildDir "ClipCue.*.dll") $install -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $PSScriptRoot "register-classic.ps1") $install -Force
Copy-Item (Join-Path $PSScriptRoot "unregister-classic.ps1") $install -Force
& powershell -ExecutionPolicy Bypass -File (Join-Path $install "register-classic.ps1") -BuildDir $install
$monitor = Join-Path $install "ClipCue.Monitor.exe"
if (Test-Path $monitor) {
  New-Item -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Force | Out-Null
  Set-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Name "ClipCue Monitor" -Value "`"$monitor`" --background"
  Start-Process -FilePath $monitor -ArgumentList "--background" -WorkingDirectory $install -WindowStyle Hidden
  Write-Host "Enabled ClipCue background monitor and tray icon."
} else {
  Write-Warning "ClipCue.Monitor.exe was not found; background monitor was not enabled."
}
Write-Host "Installed ClipCue to $install"
