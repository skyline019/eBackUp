# TopoCDC Gen3 proof (C++ only): unit tests + topo_cdc_eval + L5_topo_ab
param(
  [string]$BuildDir = "E:\recoveryProjects\build\engine_cpp\Release\Release"
)

$ErrorActionPreference = "Continue"
$TestExe = Join-Path $BuildDir "ebbackup_tests.exe"
$TopoEval = Join-Path $BuildDir "ebbackup_topo_cdc_eval.exe"
$BenchCheck = Join-Path $BuildDir "ebbackup_bench_check.exe"
$OutDir = Join-Path $env:TEMP "topo_proof_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
New-Item -ItemType Directory -Path $OutDir | Out-Null

Write-Host "=== TopoCDC Hom unit tests ==="
if (-not (Test-Path $TestExe)) {
  Write-Error "Missing $TestExe — build Release first."
  exit 1
}
& $TestExe --gtest_filter=TopoCdcHomTest.*:TopoGearParityTest.* 2>&1 | Tee-Object (Join-Path $OutDir "topo_unit_tests.txt")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not (Test-Path $TopoEval)) {
  Write-Error "Missing $TopoEval — build ebbackup_topo_cdc_eval."
  exit 1
}
Write-Host "=== C++ three-family eval ==="
$EvalJson = Join-Path $OutDir "topo_cdc_eval.json"
& $TopoEval --json-out $EvalJson 2>&1 | Tee-Object -FilePath $EvalJson -Append
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not (Test-Path $BenchCheck)) {
  Write-Error "Missing $BenchCheck — build ebbackup_bench_check."
  exit 1
}
Write-Host "=== L5_topo_ab ==="
& $BenchCheck 2>&1 | Select-String -Pattern "L5_topo_ab" | Tee-Object (Join-Path $OutDir "l5_topo.txt")
exit $LASTEXITCODE
