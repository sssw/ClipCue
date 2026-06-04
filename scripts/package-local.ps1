param(
  [Parameter(Mandatory=$false)]
  [string]$BuildDir = "build\Release",

  [Parameter(Mandatory=$false)]
  [string]$Output = "dist\PathCue-local.zip"
)

$ErrorActionPreference = "Stop"
$stage = Join-Path ([System.IO.Path]::GetTempPath()) ("PathCueStage-" + [guid]::NewGuid())
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item (Join-Path $BuildDir "PathCue.*.exe") $stage -ErrorAction SilentlyContinue
Copy-Item (Join-Path $BuildDir "PathCue.*.dll") $stage -ErrorAction SilentlyContinue
Copy-Item LICENSE,NOTICE,README.md $stage
Copy-Item scripts $stage -Recurse
Copy-Item docs $stage -Recurse
New-Item -ItemType Directory -Force -Path (Split-Path $Output -Parent) | Out-Null
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $Output -Force
Remove-Item $stage -Recurse -Force
Write-Host "Created $Output"
