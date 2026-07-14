# Event-driven live progress (writes to eval_live.log; no agent poll loop)
param(
  [string]$ProgressFile = "E:\recoveryProjects\tools\topo_cdc_eval\reports\eval_progress.json",
  [string]$LogFile = "E:\recoveryProjects\tools\topo_cdc_eval\reports\eval_live.log"
)

$dir = Split-Path $ProgressFile -Parent
New-Item -ItemType Directory -Path $dir -Force | Out-Null

function Write-ProgressLine {
  param($Path, $Log)
  if (-not (Test-Path $Path)) { return $false }
  try {
    $j = Get-Content $Path -Raw -Encoding UTF8 | ConvertFrom-Json
  } catch { return $false }
  $barLen = 30
  $filled = [math]::Min($barLen, [math]::Max(0, [int]($j.pct / 100 * $barLen)))
  $bar = ("#" * $filled).PadRight($barLen, "-")
  $line = "[{0}] {1}% [{2}] {3} | {4} | elapsed {5}s" -f `
    (Get-Date -Format "HH:mm:ss"), $j.pct, $bar, $j.stage, $j.detail, $j.elapsed_s
  if ($j.bytes_done) { $line += " | scan {0}/{1}" -f $j.bytes_done, $j.bytes_total }
  Write-Host $line
  Add-Content -Path $Log -Value $line -Encoding UTF8
  return ($j.status -eq "done" -or $j.status -eq "failed")
}

"=== TopoCDC live progress stream ===" | Tee-Object -FilePath $LogFile
$null = Write-ProgressLine $ProgressFile $LogFile

$script:finished = $false
$watcher = New-Object System.IO.FileSystemWatcher
$watcher.Path = $dir
$watcher.Filter = Split-Path $ProgressFile -Leaf
$watcher.IncludeSubdirectories = $false
$watcher.NotifyFilter = [IO.NotifyFilters]::LastWrite -bor [IO.NotifyFilters]::Size

$action = {
  if ($Event.MessageData.Finished.Value) { return }
  $done = Write-ProgressLine $Event.MessageData.ProgressFile $Event.MessageData.LogFile
  if ($done) { $Event.MessageData.Finished.Value = $true }
}

$sub = Register-ObjectEvent -InputObject $watcher -EventName Changed -Action $action `
  -MessageData @{
    ProgressFile = $ProgressFile
    LogFile = $LogFile
    Finished = [ref]$script:finished
  }

$watcher.EnableRaisingEvents = $true
Write-Host "Watching $ProgressFile -> $LogFile (Ctrl+C to stop watch; eval continues)"
try {
  while (-not $script:finished) { Start-Sleep -Milliseconds 300 }
} finally {
  $watcher.EnableRaisingEvents = $false
  $watcher.Dispose()
  Unregister-Event -SourceIdentifier $sub.Name -ErrorAction SilentlyContinue
  Remove-Job -Name $sub.Name -Force -ErrorAction SilentlyContinue
}
