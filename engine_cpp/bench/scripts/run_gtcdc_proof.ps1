# G-TCDC v6 proof: scan microbench p50 + 10-run L5 A/B metrics (2F-Gear 0x10000 repos)
param(
  [int]$Runs = 10,
  [string]$BuildDir = "E:\recoveryProjects\build\engine_cpp\Release"
)

$ErrorActionPreference = "Continue"
$ScanBench = Join-Path $BuildDir "ebbackup_gtcdc_bench.exe"
$BenchCheck = Join-Path $BuildDir "ebbackup_bench_check.exe"
$FloorPath = "E:\recoveryProjects\engine_cpp\bench\baselines\ci_floor.json"
$OutDir = Join-Path $env:TEMP "gtcdc_proof_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
New-Item -ItemType Directory -Path $OutDir | Out-Null
$Csv = Join-Path $OutDir "gtcdc_proof_summary.csv"

if (-not (Test-Path $ScanBench)) {
  Write-Error "Missing $ScanBench — build Release targets first."
}

function ParseTensorRatioP50($text) {
  if ($text -match 'tensor_vs_scalar=([\d.]+) p50=([\d.]+)') {
    return [double]$Matches[2]
  }
  if ($text -match 'tensor_vs_scalar=([\d.]+)') {
    return [double]$Matches[1]
  }
  return 0.0
}

Write-Host "=== G-TCDC scan microbench (3 runs, p50) ==="
$microP50s = @()
for ($m = 1; $m -le 3; $m++) {
  $microLog = Join-Path $OutDir "scan_bench_$m.txt"
  & $ScanBench --runs 5 2>&1 | Tee-Object -FilePath $microLog
  $microText = Get-Content $microLog -Raw
  $microP50s += ParseTensorRatioP50 $microText
}
$microP50 = ($microP50s | Sort-Object)[[int][math]::Floor(($microP50s.Count - 1) / 2)]
Write-Host "microbench tensor_vs_scalar p50 (3-run median) = $microP50"

$streamMBps = @()
$gtcdcMBps = @()
$ratios = @()
$scanRatios = @()
$scanPerProbeRatios = @()

for ($i = 1; $i -le $Runs; $i++) {
  Write-Host "=== L5 A/B run $i / $Runs ==="
  $log = Join-Path $OutDir "ab_run_$i.txt"
  $env:EB_BENCH_FLOOR_PATH = $FloorPath
  $env:EBBACKUP_PIPELINE_PROFILE = "1"
  & $BenchCheck 2>&1 | Tee-Object -FilePath $log
  $benchExit = $LASTEXITCODE
  if ($benchExit -ne 0) {
    Write-Host "bench_check exit $benchExit (continuing proof capture)"
  }
  $text = Get-Content $log -Raw
  if ($text -match 'L5_gtcdc_ab: stream_256MBps=([\d.]+) gtcdc_256MBps=([\d.]+) gtcdc_vs_stream_ratio=([\d.]+) scan_ns_ratio=([\d.]+) scan_ns_per_probe_ratio=([\d.]+)') {
    $streamMBps += [double]$Matches[1]
    $gtcdcMBps += [double]$Matches[2]
    $ratios += [double]$Matches[3]
    $scanRatios += [double]$Matches[4]
    $scanPerProbeRatios += [double]$Matches[5]
  }
}

function Mean($arr) { if ($arr.Count -eq 0) { return 0 } ($arr | Measure-Object -Average).Average }
function CvPct($arr) {
  if ($arr.Count -lt 2) { return 0 }
  $m = Mean $arr
  if ($m -eq 0) { return 0 }
  $var = 0.0
  foreach ($x in $arr) { $var += ($x - $m) * ($x - $m) }
  $var /= ($arr.Count - 1)
  100.0 * [math]::Sqrt($var) / $m
}

$ratioMean = Mean $ratios
$scanMean = Mean $scanRatios
$scanPerProbeMean = Mean $scanPerProbeRatios
$dualPass = ($ratioMean -ge 1.0) -and ($scanPerProbeMean -le 1.05)

$summary = [PSCustomObject]@{
  runs = $Runs
  tensor_vs_scalar_p50 = [math]::Round($microP50, 3)
  stream_256MBps_mean = [math]::Round((Mean $streamMBps), 2)
  gtcdc_256MBps_mean = [math]::Round((Mean $gtcdcMBps), 2)
  gtcdc_vs_stream_ratio_mean = [math]::Round($ratioMean, 3)
  scan_ns_ratio_mean = [math]::Round($scanMean, 3)
  scan_ns_per_probe_ratio_mean = [math]::Round($scanPerProbeMean, 3)
  dual_gate_pass = $dualPass
  stream_cv_pct = [math]::Round((CvPct $streamMBps), 2)
  gtcdc_cv_pct = [math]::Round((CvPct $gtcdcMBps), 2)
}
$summary | Export-Csv -Path $Csv -NoTypeInformation
Write-Host ""
Write-Host "Summary written to $Csv"
$summary | Format-List

if ($dualPass) {
  Write-Host "DUAL GATE: PASS (ratio_mean >= 1.0 and scan_ns_per_probe_mean <= 1.05)"
  $floorRatio = [math]::Round($ratioMean * 0.95, 3)
  $floorScanPerProbe = [math]::Round($scanPerProbeMean * 1.05, 3)
  $floorGtcdcMbps = [math]::Round((Mean $gtcdcMBps) * 0.70, 2)
  Write-Host "Suggested ci_floor: gtcdc_vs_stream_ratio_min=$floorRatio gtcdc_scan_ns_per_probe_ratio_max=$floorScanPerProbe gtcdc_backup_pipeline_256MBps_min=$floorGtcdcMbps"
} else {
  Write-Host "DUAL GATE: FAIL — keep ci_floor.json gtcdc keys at 0"
  if ($ratioMean -lt 1.0) { Write-Host "  gtcdc_vs_stream_ratio_mean $([math]::Round($ratioMean,3)) < 1.0" }
  if ($scanPerProbeMean -gt 1.05) { Write-Host "  scan_ns_per_probe_ratio_mean $([math]::Round($scanPerProbeMean,3)) > 1.05" }
  Write-Host "  (diagnostic scan_ns_ratio_mean $([math]::Round($scanMean,3)))"
}

if ($microP50 -lt 1.30) {
  Write-Host "microbench p50 $microP50 below 1.30 target"
}
