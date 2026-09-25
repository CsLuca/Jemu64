param(
    [ValidateSet("daily-fast", "nightly-strict")]
    [string]$OperationalProfile = "daily-fast",
    [string]$Level5Manifest = "datasets/level5/manifests/level5_official_testset_v1_manifest.json",
    [string]$SyntheticManifest = "datasets/level6/manifests/level6_synthetic_profiles_manifest.json",
    [string]$CalibrationManifest = "datasets/level6/manifests/level6_capture_calibration_manifest_sample.json",
    [string]$BaseProfile = "config/iec_profiles/baseline_1541.json",
    [string]$CalibratedProfileOut = "config/iec_profiles/calibrated_synthetic_1541.json",
    [string]$CalibratedProfileId = "calibrated_synthetic_1541",
    [string]$OutputDir = "datasets/level6/quality_reports/full_stack_gate",
    [double]$MaxRuntimeMultiplier = 2.0
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot

# Procedure: normalize relative paths against repository root.
function Resolve-LocalPath {
    param([string]$PathInput)
    if ([System.IO.Path]::IsPathRooted($PathInput)) {
        return $PathInput
    }
    return (Join-Path -Path $repo -ChildPath $PathInput)
}

# Procedure: ensure output folders exist before writing artifacts.
function Ensure-Directory {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

# Procedure: run one gate step and collect pass/fail telemetry.
function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Action
    )
    $start = [DateTime]::UtcNow
    try {
        $stepOutput = @(& $Action 2>&1)
        foreach ($line in $stepOutput) {
            "$line"
        }
        $exitCode = $LASTEXITCODE
        if ($exitCode -ne 0) {
            return [pscustomobject]@{
                step = $Name
                pass = 0
                exit_code = $exitCode
                issue = "exit=$exitCode"
                elapsed_s = [Math]::Round(([DateTime]::UtcNow - $start).TotalSeconds, 3)
            }
        }
        return [pscustomobject]@{
            step = $Name
            pass = 1
            exit_code = 0
            issue = "ok"
            elapsed_s = [Math]::Round(([DateTime]::UtcNow - $start).TotalSeconds, 3)
        }
    }
    catch {
        return [pscustomobject]@{
            step = $Name
            pass = 0
            exit_code = 1
            issue = $_.Exception.Message
            elapsed_s = [Math]::Round(([DateTime]::UtcNow - $start).TotalSeconds, 3)
        }
    }
}

# Procedure: derive operational knobs from selected profile.
$skipStrict = $false
$level5Profile = "strict"
$kernelUsePureGuard = $false
if ($OperationalProfile -eq "daily-fast") {
    $skipStrict = $true
    $level5Profile = "fast"
    $kernelUsePureGuard = $true
}

$level5ManifestPath = Resolve-LocalPath -PathInput $Level5Manifest
$syntheticManifestPath = Resolve-LocalPath -PathInput $SyntheticManifest
$calibrationManifestPath = Resolve-LocalPath -PathInput $CalibrationManifest
$baseProfilePath = Resolve-LocalPath -PathInput $BaseProfile
$calibratedProfilePath = Resolve-LocalPath -PathInput $CalibratedProfileOut
$outputRoot = Resolve-LocalPath -PathInput $OutputDir

if (-not (Test-Path -LiteralPath $level5ManifestPath)) {
    throw "Missing Level5 manifest: $level5ManifestPath"
}
if (-not (Test-Path -LiteralPath $syntheticManifestPath)) {
    throw "Missing synthetic manifest: $syntheticManifestPath"
}
if (-not (Test-Path -LiteralPath $calibrationManifestPath)) {
    throw "Missing calibration manifest: $calibrationManifestPath"
}
if (-not (Test-Path -LiteralPath $baseProfilePath)) {
    throw "Missing base profile: $baseProfilePath"
}

Ensure-Directory -Path $outputRoot

$rows = @()

$kernelOut = Join-Path -Path $outputRoot -ChildPath "kernel_e2e"
Ensure-Directory -Path $kernelOut
$rows += Invoke-Step -Name "kernel_iec_e2e" -Action {
    if ($kernelUsePureGuard) {
        & "$repo\run_kernel_iec_e2e.ps1" -Mode pure -Repeat 1 -Quiet -UseTestOnlyPureCmdGuard
    } else {
        # Procedure: strict profile validates natural no-guard kernel IEC path.
        & "$repo\run_kernel_iec_e2e.ps1" -Mode pure -Repeat 1 -Quiet
    }
}

$l5Out = Join-Path -Path $outputRoot -ChildPath "l5_cycle"
Ensure-Directory -Path $l5Out
$rows += Invoke-Step -Name "l5_cycle_$level5Profile" -Action {
    & "$repo\run_level5_cycle_coupling_gate.ps1" -Profile $level5Profile -Manifest $level5ManifestPath -OutputDir $l5Out
}

$copyOut = Join-Path -Path $outputRoot -ChildPath "copy_8_to_9"
Ensure-Directory -Path $copyOut
$rows += Invoke-Step -Name "iec_copy_8_to_9_gate" -Action {
    & "$repo\run_iec_copy_8_to_9_gate.ps1" -Profile fast -OutputDir $copyOut
}

$l6PhysicalOut = Join-Path -Path $outputRoot -ChildPath "l6_physical"
Ensure-Directory -Path $l6PhysicalOut
$rows += Invoke-Step -Name "l6_physical_gate" -Action {
    if ($skipStrict) {
        & "$repo\run_level6_physical_gate.ps1" -Manifest $level5ManifestPath -OutputDir $l6PhysicalOut -MaxRuntimeMultiplier $MaxRuntimeMultiplier -SkipStrict
    } else {
        & "$repo\run_level6_physical_gate.ps1" -Manifest $level5ManifestPath -OutputDir $l6PhysicalOut -MaxRuntimeMultiplier $MaxRuntimeMultiplier
    }
}

$l6SynthOut = Join-Path -Path $outputRoot -ChildPath "l6_synthetic"
Ensure-Directory -Path $l6SynthOut
$rows += Invoke-Step -Name "l6_synthetic_envelope_gate" -Action {
    if ($skipStrict) {
        & "$repo\run_level6_synthetic_envelope_gate.ps1" -Manifest $syntheticManifestPath -Level5Manifest $level5ManifestPath -OutputDir $l6SynthOut -MaxRuntimeMultiplier $MaxRuntimeMultiplier -SkipStrict
    } else {
        & "$repo\run_level6_synthetic_envelope_gate.ps1" -Manifest $syntheticManifestPath -Level5Manifest $level5ManifestPath -OutputDir $l6SynthOut -MaxRuntimeMultiplier $MaxRuntimeMultiplier
    }
}

$l6HardeningOut = Join-Path -Path $outputRoot -ChildPath "l6_hardening"
Ensure-Directory -Path $l6HardeningOut
$rows += Invoke-Step -Name "l6_hardening_gate" -Action {
    if ($skipStrict) {
        & "$repo\run_level6_hardening_gate.ps1" -CalibrationManifest $calibrationManifestPath -BaseProfile $baseProfilePath -CalibratedProfileOut $calibratedProfilePath -CalibratedProfileId $CalibratedProfileId -OutputDir $l6HardeningOut -MaxRuntimeMultiplier $MaxRuntimeMultiplier -SkipStrict
    } else {
        & "$repo\run_level6_hardening_gate.ps1" -CalibrationManifest $calibrationManifestPath -BaseProfile $baseProfilePath -CalibratedProfileOut $calibratedProfilePath -CalibratedProfileId $CalibratedProfileId -OutputDir $l6HardeningOut -MaxRuntimeMultiplier $MaxRuntimeMultiplier
    }
}

$csvPath = Join-Path -Path $outputRoot -ChildPath "iec_full_stack_gate_runtime.csv"
$jsonPath = Join-Path -Path $outputRoot -ChildPath "iec_full_stack_gate_metrics.json"

$rows = @($rows | Where-Object { $_ -is [psobject] -and ($_.PSObject.Properties.Name -contains "step") })
$rows | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding ASCII

$allPass = (($rows | Measure-Object -Property pass -Sum).Sum -eq $rows.Count)
$metrics = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    operational_profile = $OperationalProfile
    level5_manifest = $level5ManifestPath
    synthetic_manifest = $syntheticManifestPath
    calibration_manifest = $calibrationManifestPath
    metrics = [ordered]@{
        steps_total = [int]$rows.Count
        steps_pass = [int](($rows | Measure-Object -Property pass -Sum).Sum)
        skip_strict = [bool]$skipStrict
    }
    overall_pass = $allPass
}

($metrics | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $jsonPath -Encoding ASCII

"[IEC-FULL-STACK] profile=$OperationalProfile pass=$allPass steps=$((($rows | Measure-Object -Property pass -Sum).Sum))/$($rows.Count)"
if (-not $allPass) {
    exit 1
}
exit 0
