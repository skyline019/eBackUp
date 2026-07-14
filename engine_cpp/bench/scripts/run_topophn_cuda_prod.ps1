# TopoPH-Native production smoke: InitRepo + full Backup of D:\CUDA (real Toolkit corpus).
# Optional: skips cleanly if SourceDir missing. Logs to -LogPath.
# Does not rebuild frozen FastCDC / Hom / Chain / Gen5.0 cut sources.
param(
  [string]$SourceDir = "D:\CUDA",
  [string]$RepoDir = "E:\recoveryProjects\prod_test\topophn_cuda_repo",
  [string]$EbExe = "E:\recoveryProjects\build\engine_cpp\Release\Release\eb.exe",
  [string]$LogPath = "E:\recoveryProjects\prod_test\topophn_cuda_backup.log",
  [ValidateSet("tri", "ph")]
  [string]$Kernel = "tri"
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Path (Split-Path -Parent $RepoDir) -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path -Parent $LogPath) -Force | Out-Null

function Log([string]$msg) {
  $line = "{0} {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $msg
  Add-Content -LiteralPath $LogPath -Value $line
  Write-Host $line
}

if (-not (Test-Path -LiteralPath $SourceDir)) {
  Write-Host "SKIP: Source not found: $SourceDir (optional CUDA Toolkit corpus)"
  exit 0
}
if (-not (Test-Path -LiteralPath $EbExe)) { throw "eb.exe not found: $EbExe" }

"" | Set-Content -LiteralPath $LogPath
Log "=== TopoPH-Native CUDA production backup ==="
Log "source=$SourceDir"
Log "repo=$RepoDir"
Log "eb=$EbExe"
Log "EBBACKUP_CDC_ALGO=topophn KERNEL=$Kernel"

if (Test-Path -LiteralPath $RepoDir) {
  Log "Removing existing repo..."
  Remove-Item -LiteralPath $RepoDir -Recurse -Force
}

$env:EBBACKUP_CDC_ALGO = "topophn"
$env:EBBACKUP_TOPOPHN_KERNEL = $Kernel
Remove-Item Env:EBBACKUP_TOPO_VARIANT -ErrorAction SilentlyContinue
Remove-Item Env:EBBACKUP_TOPOCHAIN_PARALLEL_SCAN -ErrorAction SilentlyContinue

Log "--- InitRepo ---"
$swInit = [System.Diagnostics.Stopwatch]::StartNew()
& $EbExe init $RepoDir 2>&1 | ForEach-Object { Log "$_" }
if ($LASTEXITCODE -ne 0) { throw "eb init failed: $LASTEXITCODE" }
$swInit.Stop()
Log ("init_ok elapsed_ms={0}" -f $swInit.ElapsedMilliseconds)

Log "--- Backup (full, no --progress) ---"
$swBak = [System.Diagnostics.Stopwatch]::StartNew()
& $EbExe backup $RepoDir $SourceDir --pipeline 2>&1 | ForEach-Object { Log "$_" }
$bakRc = $LASTEXITCODE
$swBak.Stop()
if ($bakRc -ne 0) { throw "eb backup failed: $bakRc" }

$elapsedSec = [Math]::Max(0.001, $swBak.Elapsed.TotalSeconds)
Log ("backup_ok elapsed_s={0:N2}" -f $elapsedSec)

$srcBytes = (Get-ChildItem -LiteralPath $SourceDir -Recurse -File -ErrorAction SilentlyContinue |
  Measure-Object -Property Length -Sum).Sum
$repoBytes = (Get-ChildItem -LiteralPath $RepoDir -Recurse -File -ErrorAction SilentlyContinue |
  Measure-Object -Property Length -Sum).Sum
$mbps = ($srcBytes / 1MB) / $elapsedSec
Log ("source_bytes={0} ({1:N2} GiB)" -f $srcBytes, ($srcBytes / 1GB))
Log ("repo_bytes={0} ({1:N2} GiB)" -f $repoBytes, ($repoBytes / 1GB))
Log ("throughput_MBps={0:N2}" -f $mbps)
Log "=== DONE (InitRepo + Backup only) ==="
