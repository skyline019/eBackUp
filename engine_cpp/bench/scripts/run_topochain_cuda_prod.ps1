# TopoChain production smoke: InitRepo + full Backup of D:\CUDA (no verify/restore).
# Avoids pre-scan hang and --progress spam. Logs to -LogPath.
param(
  [string]$SourceDir = "D:\CUDA",
  [string]$RepoDir = "E:\recoveryProjects\prod_test\topochain_cuda_repo",
  [string]$EbExe = "E:\recoveryProjects\build\engine_cpp\Release\Release\eb.exe",
  [string]$LogPath = "E:\recoveryProjects\prod_test\topochain_cuda_backup.log",
  [int]$ParallelScan = 1
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Path (Split-Path -Parent $RepoDir) -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path -Parent $LogPath) -Force | Out-Null

function Log([string]$msg) {
  $line = "{0} {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $msg
  Add-Content -LiteralPath $LogPath -Value $line
  Write-Host $line
}

if (-not (Test-Path -LiteralPath $SourceDir)) { throw "Source not found: $SourceDir" }
if (-not (Test-Path -LiteralPath $EbExe)) { throw "eb.exe not found: $EbExe" }

"" | Set-Content -LiteralPath $LogPath
Log "=== TopoChain CUDA production backup ==="
Log "source=$SourceDir"
Log "repo=$RepoDir"
Log "eb=$EbExe"
Log "EBBACKUP_CDC_ALGO=topochain parallel_scan=$ParallelScan"

if (Test-Path -LiteralPath $RepoDir) {
  Log "Removing existing repo..."
  Remove-Item -LiteralPath $RepoDir -Recurse -Force
}

$env:EBBACKUP_CDC_ALGO = "topochain"
$env:EBBACKUP_TOPOCHAIN_PARALLEL_SCAN = "$ParallelScan"
Remove-Item Env:EBBACKUP_TOPO_VARIANT -ErrorAction SilentlyContinue

Log "--- InitRepo ---"
$swInit = [System.Diagnostics.Stopwatch]::StartNew()
& $EbExe init $RepoDir 2>&1 | ForEach-Object { Log "$_" }
if ($LASTEXITCODE -ne 0) { throw "eb init failed: $LASTEXITCODE" }
$swInit.Stop()
Log ("init_ok elapsed_ms={0}" -f $swInit.ElapsedMilliseconds)

Log "--- Backup (full, no --progress) ---"
$swBak = [System.Diagnostics.Stopwatch]::StartNew()
# Capture stdout/stderr line-by-line so progress does not freeze the host pipe.
& $EbExe backup $RepoDir $SourceDir --pipeline 2>&1 | ForEach-Object { Log "$_" }
$bakRc = $LASTEXITCODE
$swBak.Stop()
if ($bakRc -ne 0) { throw "eb backup failed: $bakRc" }

$elapsedSec = [Math]::Max(0.001, $swBak.Elapsed.TotalSeconds)
Log ("backup_ok elapsed_s={0:N2}" -f $elapsedSec)

$repoBytes = (Get-ChildItem -LiteralPath $RepoDir -Recurse -File -ErrorAction SilentlyContinue |
  Measure-Object -Property Length -Sum).Sum
Log ("repo_bytes={0} ({1:N2} GiB)" -f $repoBytes, ($repoBytes / 1GB))
Log "=== DONE (InitRepo + Backup only) ==="
