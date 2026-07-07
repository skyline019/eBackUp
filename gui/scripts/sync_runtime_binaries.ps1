param(
  [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$guiRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $guiRoot
$src = Join-Path $repoRoot "build\engine_cpp\$Configuration\ebbackup_workbench.dll"
$destDir = Join-Path $guiRoot "src-tauri\bin"
$dest = Join-Path $destDir "ebbackup_workbench.dll"

if (-not (Test-Path $src)) {
  Write-Error "ebbackup_workbench.dll not found at $src — run: cmake --build build --config $Configuration --target ebbackup_workbench"
}

New-Item -ItemType Directory -Path $destDir -Force | Out-Null
Copy-Item -Path $src -Destination $dest -Force
Write-Host "Synced $src -> $dest"
