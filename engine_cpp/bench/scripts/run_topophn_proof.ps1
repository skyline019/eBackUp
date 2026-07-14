# TopoPH-Native Gen5.1 proof: unit tests + nine-family eval + L5_topophn_ab
# Does not rebuild or modify TopoChain / Gen5.0 TopoPH cut sources.
param(
  [string]$BuildDir = "E:\recoveryProjects\build\engine_cpp\Release\Release"
)

$ErrorActionPreference = "Continue"
# Avoid parent-shell CDC_ALGO (e.g. topochain) poisoning InitDefaultRepo.
$env:EBBACKUP_CDC_ALGO = ""
$env:EBBACKUP_TOPOPHN_KERNEL = ""
$TestExe = Join-Path $BuildDir "ebbackup_tests.exe"
$TopoEval = Join-Path $BuildDir "ebbackup_topo_cdc_eval.exe"
$OutDir = Join-Path $env:TEMP "topophn_proof_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
New-Item -ItemType Directory -Path $OutDir | Out-Null

if (-not (Test-Path $TestExe)) {
  Write-Error "Missing $TestExe — build Release first."
  exit 1
}
Write-Host "=== TopoPH-Native unit tests ==="
& $TestExe --gtest_filter='TopoPhnTriTest.*:TopoPhnH0Test.*:TopoPhnEnvTest.*' 2>&1 |
  Tee-Object (Join-Path $OutDir "topophn_unit_tests.txt")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not (Test-Path $TopoEval)) {
  Write-Error "Missing $TopoEval"
  exit 1
}
Write-Host "=== Nine-family eval (incl. tri_native / ph_native) ==="
$EvalJson = Join-Path $OutDir "topo_cdc_eval.json"
& $TopoEval --json-out $EvalJson 2>&1 | Tee-Object -FilePath (Join-Path $OutDir "eval_stdout.txt")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "=== L5_topophn_ab (optional; skipped if bench_check missing) ==="
$BenchCheck = Join-Path $BuildDir "ebbackup_bench_check.exe"
$FloorJson = Join-Path (Split-Path $PSScriptRoot -Parent) "baselines\ci_floor_topophn_only.json"
if (Test-Path $BenchCheck) {
  $env:EB_BENCH_FLOOR_PATH = $FloorJson
  $benchOut = & $BenchCheck 2>&1
  $benchOut | Select-String -Pattern "L5_topophn_ab" | Tee-Object (Join-Path $OutDir "l5_topophn.txt")
  $benchOut | Out-File (Join-Path $OutDir "bench_check_full.txt")
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
  Write-Host "bench_check missing — unit+eval only"
}

Write-Host "=== D:\CUDA corpus smoke (optional; skip if missing) ==="
$CudaSmoke = Join-Path $PSScriptRoot "run_topophn_cuda_prod.ps1"
if (Test-Path $CudaSmoke) {
  & $CudaSmoke 2>&1 | Tee-Object (Join-Path $OutDir "cuda_prod.txt")
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "=== DONE (out=$OutDir) ==="
exit 0
