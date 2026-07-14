# Start TopoCDC eval with real-time progress + optional watcher loop
param(
  [int]$SyntheticMb = 8,
  [int]$WatchIntervalSec = 15,
  [switch]$NoWatch,
  [string]$OutJson = "E:\recoveryProjects\tools\topo_cdc_eval\reports\go_no_go_v2.json",
  [string]$ProgressFile = "E:\recoveryProjects\tools\topo_cdc_eval\reports\eval_progress.json"
)

$ErrorActionPreference = "Stop"
$Python = "d:\anaconda3\python.exe"
$EvalScript = "E:\recoveryProjects\tools\topo_cdc_eval\eval_corpus.py"
$WatchScript = "E:\recoveryProjects\tools\topo_cdc_eval\watch_progress.ps1"

# Kill stale eval jobs
Get-Process python* -ErrorAction SilentlyContinue | Where-Object {
  $_.CommandLine -like "*eval_corpus.py*"
} | Stop-Process -Force -ErrorAction SilentlyContinue

if (Test-Path $ProgressFile) { Remove-Item $ProgressFile -Force -ErrorAction SilentlyContinue }

Write-Host "=== Starting tracked eval (synthetic ${SyntheticMb}MB) ==="
$evalArgs = @(
  $EvalScript,
  "--grid",
  "--out", $OutJson,
  "--progress-file", $ProgressFile,
  "--synthetic-mb", $SyntheticMb
)
Start-Process -FilePath $Python -ArgumentList $evalArgs -WorkingDirectory "E:\recoveryProjects\tools\topo_cdc_eval" -WindowStyle Hidden

if (-not $NoWatch) {
  Write-Host "=== Progress watcher every ${WatchIntervalSec}s (Ctrl+C to stop watch; eval continues) ==="
  & $WatchScript -IntervalSec $WatchIntervalSec -ProgressFile $ProgressFile
}
