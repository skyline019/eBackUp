# TopoPH-Native: clean caches → preheat D:\CUDA → timed warm-start backup.
# Warm = source page-cache hot; repo is fresh (InitRepo + first Backup).
param(
  [string]$SourceDir = "D:\CUDA",
  [string]$RepoDir = "E:\recoveryProjects\prod_test\topophn_cuda_repo",
  [string]$EbExe = "E:\recoveryProjects\build\engine_cpp\Release\Release\eb.exe",
  [string]$LogPath = "E:\recoveryProjects\prod_test\topophn_cuda_warm.log",
  [ValidateSet("tri", "ph")]
  [string]$Kernel = "tri",
  [double]$GateMBps = 25.0
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
  Write-Host "SKIP: Source not found: $SourceDir"
  exit 0
}
if (-not (Test-Path -LiteralPath $EbExe)) { throw "eb.exe not found: $EbExe" }

Get-Process eb -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1

"" | Set-Content -LiteralPath $LogPath
Log "=== TopoPH-Native CUDA WARM-START (clear cache + preheat + backup) ==="
Log "source=$SourceDir"
Log "repo=$RepoDir"
Log "eb=$EbExe"
Log "EBBACKUP_CDC_ALGO=topophn KERNEL=$Kernel gate_MBps=$GateMBps"

# --- wipe repo / prior logs artifacts ---
if (Test-Path -LiteralPath $RepoDir) {
  Log "Removing existing repo..."
  Remove-Item -LiteralPath $RepoDir -Recurse -Force
}

# --- clear standby / working-set caches (best effort; needs admin for EmptyStandbyList) ---
Log "--- Clear OS file cache (best effort) ---"
$cleared = $false
$emptyTools = @(
  "E:\recoveryProjects\tools\EmptyStandbyList.exe",
  "C:\Sysinternals\EmptyStandbyList.exe",
  "C:\Tools\EmptyStandbyList.exe"
)
foreach ($t in $emptyTools) {
  if (Test-Path -LiteralPath $t) {
    try {
      & $t standbylist 2>&1 | ForEach-Object { Log "EmptyStandbyList: $_" }
      & $t workingsets 2>&1 | ForEach-Object { Log "EmptyStandbyList: $_" }
      $cleared = $true
      break
    } catch {
      Log ("EmptyStandbyList failed: {0}" -f $_)
    }
  }
}
if (-not $cleared) {
  # Drop this process working set; force GC. Full standby purge needs admin tool.
  try {
    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()
    $p = Get-Process -Id $PID
    $p.MinWorkingSet = [IntPtr]([int64]1MB)
    $p.MaxWorkingSet = [IntPtr]([int64]16MB)
    Log "trimmed PowerShell working set (standby purge tool not found)"
  } catch {
    Log ("working-set trim skipped: {0}" -f $_)
  }
  Log "NOTE: without EmptyStandbyList.exe as admin, prior page cache may partially remain"
}

# --- preheat source into page cache ---
Log "--- Preheat source (sequential read) ---"
$swPre = [System.Diagnostics.Stopwatch]::StartNew()
$preBytes = [uint64]0
$preFiles = 0
$buf = New-Object byte[] (8MB)
Get-ChildItem -LiteralPath $SourceDir -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object {
  try {
    $fs = [System.IO.File]::Open($_.FullName, [System.IO.FileMode]::Open,
      [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
      while ($true) {
        $n = $fs.Read($buf, 0, $buf.Length)
        if ($n -le 0) { break }
        $preBytes += [uint64]$n
      }
    } finally { $fs.Close() }
    $preFiles++
  } catch {
    Log ("preheat skip: {0} ({1})" -f $_.FullName, $_.Exception.Message)
  }
}
$swPre.Stop()
$preSec = [Math]::Max(0.001, $swPre.Elapsed.TotalSeconds)
Log ("preheat_ok files={0} bytes={1} ({2:N2} GiB) elapsed_s={3:N2} read_MBps={4:N2}" -f `
  $preFiles, $preBytes, ($preBytes / 1GB), $preSec, (($preBytes / 1MB) / $preSec))

# --- InitRepo + timed backup (source warm) ---
$env:EBBACKUP_CDC_ALGO = "topophn"
$env:EBBACKUP_TOPOPHN_KERNEL = $Kernel
Remove-Item Env:EBBACKUP_TOPO_VARIANT -ErrorAction SilentlyContinue
Remove-Item Env:EBBACKUP_TOPOCHAIN_PARALLEL_SCAN -ErrorAction SilentlyContinue

Log "--- InitRepo ---"
$swInit = [System.Diagnostics.Stopwatch]::StartNew()
$initOut = & $EbExe init $RepoDir 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) {
  Log $initOut
  throw "eb init failed: $LASTEXITCODE"
}
$swInit.Stop()
Log ("init_ok elapsed_ms={0}" -f $swInit.ElapsedMilliseconds)

Log "--- Backup (warm source page cache, fresh repo) ---"
$swBak = [System.Diagnostics.Stopwatch]::StartNew()
# Avoid per-line ForEach Log (pipes stall measured throughput).
$bakOut = & $EbExe backup $RepoDir $SourceDir --pipeline 2>&1 | Out-String
$bakRc = $LASTEXITCODE
$swBak.Stop()
if ($bakRc -ne 0) {
  Log $bakOut
  throw "eb backup failed: $bakRc"
}
if ($bakOut -match 'stats:') {
  Log (($bakOut -split "`n" | Where-Object { $_ -match 'stats:' } | Select-Object -First 1).Trim())
}

$elapsedSec = [Math]::Max(0.001, $swBak.Elapsed.TotalSeconds)
$srcBytes = [int64]$preBytes
if ($srcBytes -le 0) {
  $srcBytes = (Get-ChildItem -LiteralPath $SourceDir -Recurse -File -ErrorAction SilentlyContinue |
    Measure-Object -Property Length -Sum).Sum
}
$repoBytes = (Get-ChildItem -LiteralPath $RepoDir -Recurse -File -ErrorAction SilentlyContinue |
  Measure-Object -Property Length -Sum).Sum
$mbps = ($srcBytes / 1MB) / $elapsedSec
$gate = if ($mbps -ge $GateMBps) { "PASS" } else { "FAIL" }
Log ("backup_ok elapsed_s={0:N2}" -f $elapsedSec)
Log ("source_bytes={0} ({1:N2} GiB)" -f $srcBytes, ($srcBytes / 1GB))
Log ("repo_bytes={0} ({1:N2} GiB)" -f $repoBytes, ($repoBytes / 1GB))
Log ("throughput_MBps={0:N2}" -f $mbps)
Log ("gate_{0:N0}MBps={1}" -f $GateMBps, $gate)
Log "=== DONE warm-start ==="

if ($gate -ne "PASS") { exit 2 }
exit 0
