param(
    [string]$Manifest = "datasets/level5/manifests/level5_official_testset_v1_manifest.json",
    [string]$ExternalManifest = "external_tests_manifest.json",
    [double]$SpreadBudget = 0.0,
    [int]$ChaosRuns = 6,
    [double]$MaxFlakeRate = 0.05,
    [switch]$AllowChaosWithoutPowerMatrix,
    [string]$OutputDir = "datasets/level5/quality_reports",
    [string]$ReportCsv = "level5_final_signoff_runtime.csv",
    [string]$MetricsJson = "level5_final_signoff_metrics.json"
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot

function Resolve-LocalPath {
    param([string]$PathInput)
    if ([System.IO.Path]::IsPathRooted($PathInput)) {
        return $PathInput
    }
    return (Join-Path -Path $repo -ChildPath $PathInput)
}

$manifestPath = Resolve-LocalPath -PathInput $Manifest
$externalManifestPath = Resolve-LocalPath -PathInput $ExternalManifest
$outputRoot = Resolve-LocalPath -PathInput $OutputDir

if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Missing manifest: $manifestPath"
}
if (-not (Test-Path -LiteralPath $externalManifestPath)) {
    throw "Missing external manifest: $externalManifestPath"
}

if (-not (Test-Path -LiteralPath $outputRoot)) {
    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
}

$requirePowerMatrix = -not $AllowChaosWithoutPowerMatrix

& "$repo\run_check_level5_promotion.ps1" `
    -Manifest $manifestPath `
    -ExternalManifest $externalManifestPath `
    -SpreadBudget $SpreadBudget `
    -ChaosRuns $ChaosRuns `
    -MaxFlakeRate $MaxFlakeRate `
    -RequirePowerMatrixGateInChaos $requirePowerMatrix `
    -OutputDir $outputRoot `
    -ReportCsv $ReportCsv `
    -MetricsJson $MetricsJson

if ($LASTEXITCODE -ne 0) {
    throw "Level5 final signoff failed"
}

"[L5-FINAL] PASS: manifest=$manifestPath chaos_runs=$ChaosRuns flake_budget=$MaxFlakeRate spread_budget=$SpreadBudget power_matrix_required=$requirePowerMatrix"
exit 0
