param(
    [string]$Manifest = "datasets/level5/manifests/level5_official_testset_v1_manifest.json",
    [string]$OutputDir = "datasets/level6/quality_reports",
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

$manifestPath = Resolve-LocalPath -PathInput $Manifest
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Missing manifest: $manifestPath"
}

$outputRoot = Resolve-LocalPath -PathInput $OutputDir
Ensure-Directory -Path $outputRoot

$profiles = @("baseline_1541", "baseline_1541c", "baseline_1541ii")
$rows = @()
$allPass = $true

foreach ($profile in $profiles) {
    $profileOut = Join-Path -Path $outputRoot -ChildPath "matrix_$profile"
    Ensure-Directory -Path $profileOut

    $args = @{
        Manifest = $manifestPath
        OutputDir = $profileOut
        MaxRuntimeMultiplier = $MaxRuntimeMultiplier
    }
    if ($SkipStrict) {
        $args.SkipStrict = $true
    }

    $savedProfile = [Environment]::GetEnvironmentVariable("IEC_PROFILE", "Process")
    try {
        [Environment]::SetEnvironmentVariable("IEC_PROFILE", $profile, "Process")
        & "$repo\run_level6_physical_gate.ps1" @args
        $exitCode = $LASTEXITCODE
        $pass = ($exitCode -eq 0)
        if (-not $pass) {
            $allPass = $false
        }
    }
    finally {
        [Environment]::SetEnvironmentVariable("IEC_PROFILE", $savedProfile, "Process")
    }

    $metricsPath = Join-Path -Path $profileOut -ChildPath "level6_physical_gate_metrics.json"
    if (-not (Test-Path -LiteralPath $metricsPath)) {
        throw "Missing profile metrics: $metricsPath"
    }
    $metrics = Read-Json -Path $metricsPath
    $rows += [pscustomobject]@{
        profile = $profile
        pass = if ($pass) { 1 } else { 0 }
        dataset_pass_rate = [double]$metrics.metrics.cycle_fast_dataset_pass_rate
        runtime_ratio_vs_fast = [double]$metrics.metrics.cycle_strict_runtime_work_ratio_vs_fast
        runtime_ratio_budget = $MaxRuntimeMultiplier
    }
}

$csvPath = Join-Path -Path $outputRoot -ChildPath "level6_profile_matrix_runtime.csv"
$jsonPath = Join-Path -Path $outputRoot -ChildPath "level6_profile_matrix_metrics.json"

$rows | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding ASCII

$metricsOut = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    manifest = $manifestPath
    profiles = $profiles
    max_runtime_multiplier = $MaxRuntimeMultiplier
    metrics = [ordered]@{
        profile_pass_count = [int](($rows | Measure-Object -Property pass -Sum).Sum)
        profile_total = [int]$profiles.Count
    }
    overall_pass = $allPass
}

($metricsOut | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $jsonPath -Encoding ASCII

"[L6-MATRIX] PASS=$allPass profiles=$((($rows | Measure-Object -Property pass -Sum).Sum))/$($profiles.Count) budget=$MaxRuntimeMultiplier"
if (-not $allPass) {
    exit 1
}
exit 0
