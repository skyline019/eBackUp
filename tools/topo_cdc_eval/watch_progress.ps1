# Watch TopoCDC offline eval progress (reads reports/eval_progress.json)
param(
  [int]$IntervalSec = 15,
  [string]$ProgressFile = "E:\recoveryProjects\tools\topo_cdc_eval\reports\eval_progress.json",
  [switch]$Once
)

function Show-Progress {
  if (-not (Test-Path $ProgressFile)) {
    Write-Host "[$(Get-Date -Format 'HH:mm:ss')] waiting for $ProgressFile ..."
    return
  }
  try {
    $j = Get-Content $ProgressFile -Raw -Encoding UTF8 | ConvertFrom-Json
  } catch {
    Write-Host "[$(Get-Date -Format 'HH:mm:ss')] progress file locked or invalid"
    return
  }
  $barLen = 30
  $filled = [math]::Min($barLen, [math]::Max(0, [int]($j.pct / 100 * $barLen)))
  $bar = ("#" * $filled).PadRight($barLen, "-")
  Write-Host ("[{0}] {1}% [{2}] stage={3} status={4}" -f (Get-Date -Format "HH:mm:ss"), $j.pct, $bar, $j.stage, $j.status)
  Write-Host ("         {0} (elapsed {1}s)" -f $j.detail, $j.elapsed_s)
  if ($j.corpus) { Write-Host ("         corpus={0} bytes={1}" -f $j.corpus, $j.bytes) }
  if ($j.bytes_done) { Write-Host ("         scan {0}/{1}" -f $j.bytes_done, $j.bytes_total) }
  if ($j.status -eq "done") {
    if ($j.summary) {
      Write-Host ("         summary: mean_ratio={0} max_cut={1} perm={2}" -f `
        $j.summary.synthetic_mean_ratio, $j.summary.synthetic_max_cut_ratio, $j.summary.synthetic_calib_permille)
    }
    return $true
  }
  if ($j.status -eq "failed") {
    Write-Host ("         ERROR: {0}" -f $j.error)
    return $true
  }
  return $false
}

do {
  $done = Show-Progress
  if ($Once -or $done) { break }
  Start-Sleep -Seconds $IntervalSec
} while ($true)
