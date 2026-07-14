# G-TCDC v6 supplemental evaluation (chunk stats, incr A/B, 2GB/multi/mixed, microbench)
param(
  [switch]$Quick,
  [string]$Source = "",
  [string]$BuildDir = "E:\recoveryProjects\build\engine_cpp\Release"
)

$Eval = Join-Path $BuildDir "ebbackup_gtcdc_v6_eval.exe"
if (-not (Test-Path $Eval)) {
  Write-Error "Missing $Eval — build Release target ebbackup_gtcdc_v6_eval first."
}

$OutDir = Join-Path $env:TEMP "gtcdc_v6_eval_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
New-Item -ItemType Directory -Path $OutDir | Out-Null
$Log = Join-Path $OutDir "v6_eval.log"

$env:EBBACKUP_PIPELINE_PROFILE = "1"
$args = @()
if ($Quick) { $args += "--quick" }
if ($Source) { $args += "--source"; $args += $Source }

& $Eval @args 2>&1 | Tee-Object -FilePath $Log
$exit = $LASTEXITCODE
Write-Host ""
Write-Host "Log: $Log"
Write-Host "Exit: $exit"
exit $exit
