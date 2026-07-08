param(
  [string]$Configuration = "Release",
  [string]$BuildDir = "build",
  [string]$OutZip = ""
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
$exeSrc = Join-Path $repoRoot "$BuildDir\engine_cpp\$Configuration\ebrecover.exe"

if (-not (Test-Path $exeSrc)) {
  Write-Error "ebrecover.exe not found at $exeSrc — build with: cmake --build $BuildDir --config $Configuration --target ebrecover"
}

$stage = Join-Path $env:TEMP ("ebrecover-portable-" + [guid]::NewGuid().ToString("n"))
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item -Path $exeSrc -Destination (Join-Path $stage "ebrecover.exe") -Force

$readme = @"
ebrecover — minimal disaster-recovery runtime

Usage:
  ebrecover list <repo> [--at TXN]
  ebrecover browse <repo> [--at TXN]
  ebrecover restore <repo> <dest> [--at TXN]
  ebrecover verify <repo> [--at TXN]

Encrypted repos:
  --password PASS
  --recovery-key KEY

Copy this folder to USB/offline media together with your repo or EBB delta bundle.
"@
Set-Content -Path (Join-Path $stage "README-RECOVER.txt") -Value $readme -Encoding UTF8

$vcruntime = @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll")
foreach ($dll in $vcruntime) {
  $found = Get-Command $dll -ErrorAction SilentlyContinue
  if ($found) {
    Copy-Item -Path $found.Source -Destination $stage -Force -ErrorAction SilentlyContinue
  }
}

if (-not $OutZip) {
  $OutZip = Join-Path $repoRoot "$BuildDir\ebrecover-portable.zip"
}
if (Test-Path $OutZip) { Remove-Item $OutZip -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $OutZip -Force
Remove-Item -Recurse -Force $stage
Write-Host "Created $OutZip"
