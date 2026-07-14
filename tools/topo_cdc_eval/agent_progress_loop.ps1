# Agent loop: poll TopoCDC eval progress every N seconds
param([int]$IntervalSec = 30)

$ProgressFile = "E:\recoveryProjects\tools\topo_cdc_eval\reports\eval_progress.json"
while ($true) {
  Start-Sleep -Seconds $IntervalSec
  $payload = '{"prompt":"check topo eval progress and report status"}'
  if (Test-Path $ProgressFile) {
    try {
      $j = Get-Content $ProgressFile -Raw -Encoding UTF8 | ConvertFrom-Json
      $payload = (@{
        prompt = "check topo eval progress and report status"
        stage = $j.stage
        pct = $j.pct
        status = $j.status
        detail = $j.detail
      } | ConvertTo-Json -Compress)
    } catch { }
  }
  Write-Output "AGENT_LOOP_TICK_topo_eval $payload"
}
