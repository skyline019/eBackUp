param(
  [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$guiRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $guiRoot
$cmakeBuild = Join-Path $repoRoot "build"

Write-Host "Building ebbackup_workbench ($Configuration)..."
cmake --build $cmakeBuild --config $Configuration --target ebbackup_workbench
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Push-Location $guiRoot
try {
  & "$PSScriptRoot\sync_runtime_binaries.ps1" -Configuration $Configuration
  if (-not (Test-Path "node_modules")) { npm install }
  npm run tauri:build
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
  Pop-Location
}

$bundleDir = Join-Path $guiRoot "src-tauri\target\release\bundle\nsis"
if (Test-Path $bundleDir) {
  Write-Host "`nRelease artifacts:"
  Get-ChildItem $bundleDir -Filter "*.exe" | ForEach-Object { Write-Host "  $($_.FullName)" }
}
