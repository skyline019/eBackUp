param(
  [string]$SourceDir = "..\..\DBProject\gui\public\fonts"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root $SourceDir
$dest = Join-Path $root "public\fonts"

if (-not (Test-Path $src)) {
  Write-Error "Font source not found: $src (expected eB-Tree Workbench public/fonts)"
}

New-Item -ItemType Directory -Path $dest -Force | Out-Null
Copy-Item -Path (Join-Path $src "*.ttf") -Destination $dest -Force
Write-Host "Synced fonts from $src -> $dest"
