param(
  [Parameter(Mandatory = $true)]
  [string]$ConfigPath,
  [string]$ServiceName = "EbbackupDaemon",
  [string]$DisplayName = "ebbackup Daemon",
  [string]$EbExe = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ConfigPath)) {
  throw "Config not found: $ConfigPath"
}

$configDir = Split-Path $ConfigPath -Parent
New-Item -ItemType Directory -Force -Path $configDir | Out-Null
New-Item -ItemType Directory -Force -Path "C:\ProgramData\ebbackup" | Out-Null

if (-not $EbExe) {
  $EbExe = Join-Path $PSScriptRoot "..\build\engine_cpp\Release\eb.exe"
}
if (-not (Test-Path $EbExe)) {
  throw "eb.exe not found at $EbExe; pass -EbExe"
}

$binPath = "`"$EbExe`" service run --config `"$ConfigPath`""
& sc.exe create $ServiceName binPath= $binPath start= auto DisplayName= $DisplayName obj= "NT AUTHORITY\LocalService"
if ($LASTEXITCODE -ne 0) {
  throw "sc create failed with exit code $LASTEXITCODE"
}

Write-Host "Installed $ServiceName"
Write-Host "Start with: sc start $ServiceName"
