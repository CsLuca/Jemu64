param(
    [string]$Manifest = "datasets/level6/manifests/level6_synthetic_profiles_manifest.json",
    [string]$Level5Manifest = "datasets/level5/manifests/level5_official_testset_v1_manifest.json",
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
$level5ManifestPath = Resolve-LocalPath -PathInput $Level5Manifest
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Missing synthetic manifest: $manifestPath"
}
if (-not (Test-Path -LiteralPath $level5ManifestPath)) {
    throw "Missing level5 manifest: $level5ManifestPath"
}

$outputRoot = Resolve-LocalPath -PathInput $OutputDir
Ensure-Directory -Path $outputRoot

$manifestJson = Read-Json -Path $manifestPath
$profiles = @($manifestJson.profiles)
if ($profiles.Count -eq 0) {
    throw "Synthetic profile manifest contains no profiles."
}

$rows = @()
$allPass = $true

foreach ($profile in $profiles) {
    $savedProfile = [Environment]::GetEnvironmentVariable("IEC_PROFILE", "Process")
    try {
        [Environment]::SetEnvironmentVariable("IEC_PROFILE", [string]$profile, "Process")
        $profileOut = Join-Path -Path $outputRoot -ChildPath ("synthetic_" + [string]$profile)
        Ensure-Directory -Path $profileOut

        $args = @{
            Manifest = $level5ManifestPath
            OutputDir = $profileOut
            MaxRuntimeMultiplier = $MaxRuntimeMultiplier
        }
        if ($SkipStrict) {
            $args.SkipStrict = $true
        }

        & "$repo\run_level6_physical_gate.ps1" @args
        $pass = ($LASTEXITCODE -eq 0)
        if (-not $pass) {
            $allPass = $false
        }

        $metricsPath = Join-Path -Path $profileOut -ChildPath "level6_physical_gate_metrics.json"
        if (-not (Test-Path -LiteralPath $metricsPath)) {
            throw "Missing profile metrics: $metricsPath"
        }
        $metrics = Read-Json -Path $metricsPath
        $rows += [pscustomobject]@{
            profile = [string]$profile
            pass = if ($pass) { 1 } else { 0 }
            dataset_pass_rate = [double]$metrics.metrics.cycle_fast_dataset_pass_rate
            runtime_ratio_vs_fast = [double]$metrics.metrics.cycle_strict_runtime_work_ratio_vs_fast
            runtime_ratio_budget = $MaxRuntimeMultiplier
        }
    }
    finally {
        [Environment]::SetEnvironmentVariable("IEC_PROFILE", $savedProfile, "Process")
    }
}

$runtimeCsv = Join-Path -Path $outputRoot -ChildPath "level6_synthetic_envelope_runtime.csv"
$metricsJson = Join-Path -Path $outputRoot -ChildPath "level6_synthetic_envelope_metrics.json"

$rows | Export-Csv -LiteralPath $runtimeCsv -NoTypeInformation -Encoding ASCII

$summary = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    synthetic_manifest = $manifestPath
    level5_manifest = $level5ManifestPath
    profile_count = [int]$profiles.Count
    metrics = [ordered]@{
        pass_count = [int](($rows | Measure-Object -Property pass -Sum).Sum)
        runtime_ratio_budget = $MaxRuntimeMultiplier
    }
    overall_pass = $allPass
}

($summary | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $metricsJson -Encoding ASCII

"[L6-SYNTH] PASS=$allPass profiles=$((($rows | Measure-Object -Property pass -Sum).Sum))/$($profiles.Count) budget=$MaxRuntimeMultiplier"
if (-not $allPass) {
    exit 1
}
exit 0
