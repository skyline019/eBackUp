# TopoPH Gen5 proof: unit tests + seven-family eval
# Note: does not rebuild or modify TopoChain sources.
param(
  [string]$BuildDir = "E:\recoveryProjects\build\engine_cpp\Release\Release"
)

$ErrorActionPreference = "Continue"
$TestExe = Join-Path $BuildDir "ebbackup_tests.exe"
$TopoEval = Join-Path $BuildDir "ebbackup_topo_cdc_eval.exe"
$OutDir = Join-Path $env:TEMP "topoph_proof_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
New-Item -ItemType Directory -Path $OutDir | Out-Null

if (-not (Test-Path $TestExe)) {
  Write-Error "Missing $TestExe — build Release first."
  exit 1
}
Write-Host "=== TopoPH unit tests ==="
& $TestExe --gtest_filter='TopoPhTriTest.*:TopoPhH0Test.*:TopoPhEnvTest.*' 2>&1 |
  Tee-Object (Join-Path $OutDir "topoph_unit_tests.txt")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not (Test-Path $TopoEval)) {
  Write-Error "Missing $TopoEval"
  exit 1
}
Write-Host "=== Seven-family eval ==="
$EvalJson = Join-Path $OutDir "topo_cdc_eval.json"
& $TopoEval --json-out $EvalJson 2>&1 | Tee-Object -FilePath (Join-Path $OutDir "eval_stdout.txt")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "=== L5_topoph_ab (optional; skipped if bench_check missing) ==="
$BenchCheck = Join-Path $BuildDir "ebbackup_bench_check.exe"
$FloorJson = Join-Path (Split-Path $PSScriptRoot -Parent) "baselines\ci_floor_topoph_only.json"
if (Test-Path $BenchCheck) {
  $env:EB_BENCH_FLOOR_PATH = $FloorJson
  $benchOut = & $BenchCheck 2>&1
  $benchOut | Select-String -Pattern "L5_topoph_ab" | Tee-Object (Join-Path $OutDir "l5_topoph.txt")
  $benchOut | Out-File (Join-Path $OutDir "bench_check_full.txt")
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
  Write-Host "bench_check missing — unit+eval only"
}

Write-Host "=== DONE (out=$OutDir) ==="
exit 0
