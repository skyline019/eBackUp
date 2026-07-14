# TopoChain Gen4 proof (C++ only): chain unit tests + eval + L5_topochain_ab
param(
  [string]$BuildDir = "E:\recoveryProjects\build\engine_cpp\Release\Release"
)

$ErrorActionPreference = "Continue"
$TestExe = Join-Path $BuildDir "ebbackup_tests.exe"
$TopoEval = Join-Path $BuildDir "ebbackup_topo_cdc_eval.exe"
$BenchCheck = Join-Path $BuildDir "ebbackup_bench_check.exe"
$FloorJson = Join-Path (Split-Path $PSScriptRoot -Parent) "baselines\ci_floor_topochain_only.json"
$OutDir = Join-Path $env:TEMP "topochain_proof_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
New-Item -ItemType Directory -Path $OutDir | Out-Null

if (-not (Test-Path $TestExe)) {
  Write-Error "Missing $TestExe — build Release first."
  exit 1
}
Write-Host "=== TopoChain/Tri unit tests ==="
& $TestExe --gtest_filter='TopoCdcChainTest.*:TopoCdcTriTest.*' 2>&1 | Tee-Object (Join-Path $OutDir "topochain_unit_tests.txt")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not (Test-Path $TopoEval)) {
  Write-Error "Missing $TopoEval — build ebbackup_topo_cdc_eval."
  exit 1
}
Write-Host "=== C++ five-family eval (stream/gtcdc/topo/tri/chain) ==="
$EvalJson = Join-Path $OutDir "topo_cdc_eval.json"
& $TopoEval --json-out $EvalJson 2>&1 | Tee-Object -FilePath $EvalJson -Append
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "=== eval chain scan_ms (see JSON families.chain) ==="

if (-not (Test-Path $BenchCheck)) {
  Write-Error "Missing $BenchCheck — build ebbackup_bench_check."
  exit 1
}
Write-Host "=== L5_topochain_ab ==="
$env:EB_BENCH_FLOOR_PATH = $FloorJson
$benchOut = & $BenchCheck 2>&1
$benchOut | Select-String -Pattern "L5_topochain_ab" | Tee-Object (Join-Path $OutDir "l5_topochain.txt")
$benchOut | Out-File (Join-Path $OutDir "bench_check_full.txt")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "=== Parallel golden (EBBACKUP_TOPOCHAIN_PARALLEL_SCAN=1) ==="
$env:EBBACKUP_TOPOCHAIN_PARALLEL_SCAN = "1"
& $TestExe --gtest_filter='TopoCdcChainTest.ParallelMatchesSerial:TopoCdcChainTest.AllZeroDataParity' 2>&1 |
  Tee-Object (Join-Path $OutDir "topochain_parallel_golden.txt")
$parallelExit = $LASTEXITCODE
Remove-Item Env:EBBACKUP_TOPOCHAIN_PARALLEL_SCAN -ErrorAction SilentlyContinue
exit $parallelExit
