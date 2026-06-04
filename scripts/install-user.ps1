param(
  [Parameter(Mandatory=$true)]
  [string]$BuildDir
)

$ErrorActionPreference = "Stop"
$install = Join-Path $env:LOCALAPPDATA "Programs\PathCue"
New-Item -ItemType Directory -Force -Path $install | Out-Null
Copy-Item (Join-Path $BuildDir "PathCue.*.exe") $install -Force
Copy-Item (Join-Path $BuildDir "PathCue.*.dll") $install -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $PSScriptRoot "register-classic.ps1") $install -Force
Copy-Item (Join-Path $PSScriptRoot "unregister-classic.ps1") $install -Force
& powershell -ExecutionPolicy Bypass -File (Join-Path $install "register-classic.ps1") -BuildDir $install
Write-Host "Installed PathCue to $install"
