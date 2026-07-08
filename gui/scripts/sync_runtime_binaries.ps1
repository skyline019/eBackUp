param(
  [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$guiRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $guiRoot
$src = Join-Path $repoRoot "build\engine_cpp\$Configuration\ebbackup_workbench.dll"
$syncSrc = Join-Path $repoRoot "build\sync_cpp\$Configuration\eb-sync.exe"
$destDir = Join-Path $guiRoot "src-tauri\bin"
$dest = Join-Path $destDir "ebbackup_workbench.dll"
$syncDest = Join-Path $destDir "eb-sync.exe"

if (-not (Test-Path $src)) {
  Write-Error "ebbackup_workbench.dll not found at $src — run: cmake --build build --config $Configuration --target ebbackup_workbench"
}

if (-not (Test-Path $syncSrc)) {
  Write-Error "eb-sync.exe not found at $syncSrc — run: cmake --build build --config $Configuration --target eb-sync"
}

New-Item -ItemType Directory -Path $destDir -Force | Out-Null
Copy-Item -Path $src -Destination $dest -Force
Write-Host "Synced $src -> $dest"
Copy-Item -Path $syncSrc -Destination $syncDest -Force
Write-Host "Synced $syncSrc -> $syncDest"
