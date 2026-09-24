param(
    [string]$CalibrationManifest = "datasets/level6/manifests/level6_capture_calibration_manifest_sample.json",
    [string]$BaseProfile = "config/iec_profiles/baseline_1541.json",
    [string]$CalibratedProfileOut = "config/iec_profiles/calibrated_synthetic_1541.json",
    [string]$CalibratedProfileId = "calibrated_synthetic_1541",
    [double]$BlendWithBase = 0.35,
    [string]$Level5Manifest = "datasets/level5/manifests/level5_official_testset_v1_manifest.json",
    [string]$OutputDir = "datasets/level6/quality_reports/calibration_gate",
    [double]$MaxRuntimeMultiplier = 2.0,
    [switch]$SkipStrict
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

function Ensure-Directory {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Read-Json {
    param([string]$Path)
    return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json)
}

$calManifestPath = Resolve-LocalPath -PathInput $CalibrationManifest
$baseProfilePath = Resolve-LocalPath -PathInput $BaseProfile
$calibratedProfilePath = Resolve-LocalPath -PathInput $CalibratedProfileOut
$level5ManifestPath = Resolve-LocalPath -PathInput $Level5Manifest
$outputRoot = Resolve-LocalPath -PathInput $OutputDir

if (-not (Test-Path -LiteralPath $calManifestPath)) {
    throw "Missing calibration manifest: $calManifestPath"
}
if (-not (Test-Path -LiteralPath $baseProfilePath)) {
    throw "Missing base profile: $baseProfilePath"
}
if (-not (Test-Path -LiteralPath $level5ManifestPath)) {
    throw "Missing level5 manifest: $level5ManifestPath"
}

Ensure-Directory -Path $outputRoot

$calibrateReportPath = Join-Path -Path $outputRoot -ChildPath "level6_calibration_fit_metrics.json"

& "$repo\run_level6_calibrate_profile.ps1" -Manifest $calManifestPath -BaseProfile $baseProfilePath -OutputProfile $calibratedProfilePath -ProfileId $CalibratedProfileId -BlendWithBase $BlendWithBase -ReportJson $calibrateReportPath
if ($LASTEXITCODE -ne 0) {
    throw "Calibration script failed"
}

if (-not (Test-Path -LiteralPath $calibratedProfilePath)) {
    throw "Missing calibrated profile output: $calibratedProfilePath"
}

$savedProfile = [Environment]::GetEnvironmentVariable("IEC_PROFILE", "Process")
try {
    [Environment]::SetEnvironmentVariable("IEC_PROFILE", $calibratedProfilePath, "Process")
    if ($SkipStrict) {
        & "$repo\run_level6_physical_gate.ps1" -Manifest $level5ManifestPath -OutputDir $outputRoot -MaxRuntimeMultiplier $MaxRuntimeMultiplier -SkipStrict
    } else {
        & "$repo\run_level6_physical_gate.ps1" -Manifest $level5ManifestPath -OutputDir $outputRoot -MaxRuntimeMultiplier $MaxRuntimeMultiplier
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Physical gate failed with calibrated profile"
    }
}
finally {
    [Environment]::SetEnvironmentVariable("IEC_PROFILE", $savedProfile, "Process")
}

$physicalMetricsPath = Join-Path -Path $outputRoot -ChildPath "level6_physical_gate_metrics.json"
if (-not (Test-Path -LiteralPath $physicalMetricsPath)) {
    throw "Missing physical metrics output: $physicalMetricsPath"
}

$fit = Read-Json -Path $calibrateReportPath
$physical = Read-Json -Path $physicalMetricsPath

$overallPass = [bool]$physical.overall_pass

$gateMetricsPath = Join-Path -Path $outputRoot -ChildPath "level6_calibration_gate_metrics.json"
$gateRuntimePath = Join-Path -Path $outputRoot -ChildPath "level6_calibration_gate_runtime.csv"

$gateMetrics = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    calibration_manifest = $calManifestPath
    base_profile = $baseProfilePath
    calibrated_profile = $calibratedProfilePath
    calibrated_profile_id = $CalibratedProfileId
    level5_manifest = $level5ManifestPath
    blend_with_base = $BlendWithBase
    metrics = [ordered]@{
        captures_used = [int]$fit.captures_used
        cycle_fast_dataset_pass_rate = [double]$physical.metrics.cycle_fast_dataset_pass_rate
        runtime_ratio_vs_fast = [double]$physical.metrics.cycle_strict_runtime_work_ratio_vs_fast
    }
    budgets = [ordered]@{
        cycle_fast_dataset_min_pass_rate = 1.0
        max_runtime_multiplier_vs_fast = $MaxRuntimeMultiplier
    }
    overall_pass = $overallPass
}

($gateMetrics | ConvertTo-Json -Depth 10) | Set-Content -LiteralPath $gateMetricsPath -Encoding ASCII

@(
    [pscustomobject]@{ metric = "captures_used"; value = [int]$fit.captures_used; budget = 1; pass = [int]([int]$fit.captures_used -ge 1) },
    [pscustomobject]@{ metric = "cycle_fast_dataset_pass_rate"; value = [double]$physical.metrics.cycle_fast_dataset_pass_rate; budget = 1.0; pass = [int]([double]$physical.metrics.cycle_fast_dataset_pass_rate -ge 1.0) },
    [pscustomobject]@{ metric = "runtime_multiplier_vs_fast"; value = [double]$physical.metrics.cycle_strict_runtime_work_ratio_vs_fast; budget = $MaxRuntimeMultiplier; pass = [int]([double]$physical.metrics.cycle_strict_runtime_work_ratio_vs_fast -le $MaxRuntimeMultiplier) }
) | Export-Csv -LiteralPath $gateRuntimePath -NoTypeInformation -Encoding ASCII

"[L6-CAL-GATE] PASS=$overallPass captures=$($fit.captures_used) runtime_ratio=$('{0:N3}' -f [double]$physical.metrics.cycle_strict_runtime_work_ratio_vs_fast) budget=$MaxRuntimeMultiplier"
if (-not $overallPass) {
    exit 1
}
exit 0
