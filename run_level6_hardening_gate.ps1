param(
    [string]$CalibrationManifest = "datasets/level6/manifests/level6_capture_calibration_manifest_sample.json",
    [string]$BaseProfile = "config/iec_profiles/baseline_1541.json",
    [string]$CalibratedProfileOut = "config/iec_profiles/calibrated_synthetic_1541.json",
    [string]$CalibratedProfileId = "calibrated_synthetic_1541",
    [double]$BlendWithBase = 0.35,
    [string]$Level5Manifest = "datasets/level5/manifests/level5_official_testset_v1_manifest.json",
    [string]$OutputDir = "datasets/level6/quality_reports/hardening_gate",
    [double]$MaxRuntimeMultiplier = 2.0,
    [int]$MaxDriftLineTicks = 2,
    [int]$MaxDriftTimingTicks = 64,
    [int]$MaxDriftTauTicks = 2,
    [int]$MaxDriftThresholdMilli = 80,
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

function Get-PathValue {
    param(
        [object]$Root,
        [string]$Path
    )
    $cursor = $Root
    foreach ($part in $Path.Split('.')) {
        if ($null -eq $cursor) {
            return $null
        }
        if ($cursor -is [System.Collections.IDictionary]) {
            if (-not $cursor.Contains($part)) {
                return $null
            }
            $cursor = $cursor[$part]
            continue
        }
        $prop = $cursor.PSObject.Properties[$part]
        if ($null -eq $prop) {
            return $null
        }
        $cursor = $prop.Value
    }
    return $cursor
}

function Compare-PathDelta {
    param(
        [object]$Reference,
        [object]$Current,
        [string]$Path,
        [double]$Budget
    )
    $ref = Get-PathValue -Root $Reference -Path $Path
    $cur = Get-PathValue -Root $Current -Path $Path
    if ($null -eq $ref -and $null -eq $cur) {
        return [pscustomobject]@{ path = $Path; reference = $ref; current = $cur; delta = $null; budget = $Budget; pass = 0; issue = "missing_field_both" }
    }
    if ($null -eq $ref -and $null -ne $cur) {
        return [pscustomobject]@{ path = $Path; reference = $ref; current = $cur; delta = $null; budget = $Budget; pass = 1; issue = "missing_field_reference" }
    }
    if ($null -ne $ref -and $null -eq $cur) {
        return [pscustomobject]@{ path = $Path; reference = $ref; current = $cur; delta = $null; budget = $Budget; pass = 0; issue = "missing_field_current" }
    }
    $r = [double]$ref
    $c = [double]$cur
    $d = [Math]::Abs($c - $r)
    return [pscustomobject]@{ path = $Path; reference = $r; current = $c; delta = $d; budget = $Budget; pass = [int]($d -le $Budget); issue = "ok" }
}

$calibrationManifestPath = Resolve-LocalPath -PathInput $CalibrationManifest
$baseProfilePath = Resolve-LocalPath -PathInput $BaseProfile
$calibratedProfilePath = Resolve-LocalPath -PathInput $CalibratedProfileOut
$level5ManifestPath = Resolve-LocalPath -PathInput $Level5Manifest
$outputRoot = Resolve-LocalPath -PathInput $OutputDir

Ensure-Directory -Path $outputRoot

$referenceProfilePath = Join-Path -Path $outputRoot -ChildPath "reference_profile_snapshot.json"
$referenceAvailable = $false
if (Test-Path -LiteralPath $calibratedProfilePath) {
    Copy-Item -LiteralPath $calibratedProfilePath -Destination $referenceProfilePath -Force
    $referenceAvailable = $true
}

$calGateOut = Join-Path -Path $outputRoot -ChildPath "calibration_run"
Ensure-Directory -Path $calGateOut

if ($SkipStrict) {
    & "$repo\run_level6_calibration_gate.ps1" -CalibrationManifest $calibrationManifestPath -BaseProfile $baseProfilePath -CalibratedProfileOut $calibratedProfilePath -CalibratedProfileId $CalibratedProfileId -BlendWithBase $BlendWithBase -Level5Manifest $level5ManifestPath -OutputDir $calGateOut -MaxRuntimeMultiplier $MaxRuntimeMultiplier -SkipStrict
} else {
    & "$repo\run_level6_calibration_gate.ps1" -CalibrationManifest $calibrationManifestPath -BaseProfile $baseProfilePath -CalibratedProfileOut $calibratedProfilePath -CalibratedProfileId $CalibratedProfileId -BlendWithBase $BlendWithBase -Level5Manifest $level5ManifestPath -OutputDir $calGateOut -MaxRuntimeMultiplier $MaxRuntimeMultiplier
}
if ($LASTEXITCODE -ne 0) {
    throw "Calibration gate failed"
}

if (-not (Test-Path -LiteralPath $calibratedProfilePath)) {
    throw "Missing calibrated profile after calibration gate: $calibratedProfilePath"
}

$currentProfile = Read-Json -Path $calibratedProfilePath
$driftRows = @()

if ($referenceAvailable -and (Test-Path -LiteralPath $referenceProfilePath)) {
    $referenceProfile = Read-Json -Path $referenceProfilePath

    $linePaths = @(
        "line.atn.release_delay_ticks",
        "line.atn.min_low_pulse_ticks",
        "line.clk.release_delay_ticks",
        "line.clk.min_low_pulse_ticks",
        "line.data.release_delay_ticks",
        "line.data.min_low_pulse_ticks"
    )
    $tauPaths = @(
        "analog.rise_tau_ticks",
        "analog.fall_tau_ticks",
        "analog.node.host.rise_tau_ticks",
        "analog.node.host.fall_tau_ticks",
        "analog.node.drive.rise_tau_ticks",
        "analog.node.drive.fall_tau_ticks",
        "analog.node.host.skew_ticks",
        "analog.node.drive.skew_ticks"
    )
    $timingPaths = @(
        "timing.controller_bit_hold_ticks",
        "timing.device_bit_hold_ticks",
        "timing.controller_between_bytes_ticks",
        "timing.device_between_bytes_ticks",
        "timing.atn_response_timeout_ticks",
        "timing.device_not_present_timeout_ticks",
        "timing.sender_timeout_ticks",
        "timing.receiver_timeout_ticks",
        "timing.eoi_signal_min_ticks",
        "timing.eoi_signal_max_ticks",
        "timing.empty_stream_timeout_ticks"
    )
    $thresholdPaths = @(
        "analog.rise_threshold_milli",
        "analog.fall_threshold_milli"
    )

    foreach ($p in $linePaths) {
        $driftRows += Compare-PathDelta -Reference $referenceProfile -Current $currentProfile -Path $p -Budget $MaxDriftLineTicks
    }
    foreach ($p in $tauPaths) {
        $driftRows += Compare-PathDelta -Reference $referenceProfile -Current $currentProfile -Path $p -Budget $MaxDriftTauTicks
    }
    foreach ($p in $timingPaths) {
        $driftRows += Compare-PathDelta -Reference $referenceProfile -Current $currentProfile -Path $p -Budget $MaxDriftTimingTicks
    }
    foreach ($p in $thresholdPaths) {
        $driftRows += Compare-PathDelta -Reference $referenceProfile -Current $currentProfile -Path $p -Budget $MaxDriftThresholdMilli
    }
} else {
    $driftRows += [pscustomobject]@{ path = "reference_profile"; reference = $null; current = $null; delta = $null; budget = $null; pass = 1; issue = "no_reference_snapshot" }
}

$calGateMetricsPath = Join-Path -Path $calGateOut -ChildPath "level6_calibration_gate_metrics.json"
if (-not (Test-Path -LiteralPath $calGateMetricsPath)) {
    throw "Missing calibration gate metrics: $calGateMetricsPath"
}
$calGateMetrics = Read-Json -Path $calGateMetricsPath

$driftOverallPass = (($driftRows | Where-Object { $_.pass -eq 0 }).Count -eq 0)
$overallPass = ([bool]$calGateMetrics.overall_pass) -and $driftOverallPass

$runtimeCsvPath = Join-Path -Path $outputRoot -ChildPath "level6_hardening_gate_runtime.csv"
$metricsJsonPath = Join-Path -Path $outputRoot -ChildPath "level6_hardening_gate_metrics.json"

$driftRows | Export-Csv -LiteralPath $runtimeCsvPath -NoTypeInformation -Encoding ASCII

$maxObservedDelta = 0.0
foreach ($r in $driftRows) {
    if ($null -ne $r.delta) {
        $d = [double]$r.delta
        if ($d -gt $maxObservedDelta) {
            $maxObservedDelta = $d
        }
    }
}

$metrics = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    calibration_manifest = $calibrationManifestPath
    base_profile = $baseProfilePath
    calibrated_profile = $calibratedProfilePath
    reference_profile_snapshot = if ($referenceAvailable) { $referenceProfilePath } else { "(none)" }
    level5_manifest = $level5ManifestPath
    metrics = [ordered]@{
        calibration_gate_pass = [bool]$calGateMetrics.overall_pass
        drift_checks_total = [int]$driftRows.Count
        drift_checks_pass = [int](($driftRows | Measure-Object -Property pass -Sum).Sum)
        max_observed_delta = [double]$maxObservedDelta
    }
    budgets = [ordered]@{
        max_drift_line_ticks = $MaxDriftLineTicks
        max_drift_timing_ticks = $MaxDriftTimingTicks
        max_drift_tau_ticks = $MaxDriftTauTicks
        max_drift_threshold_milli = $MaxDriftThresholdMilli
        max_runtime_multiplier_vs_fast = $MaxRuntimeMultiplier
    }
    overall_pass = $overallPass
}

($metrics | ConvertTo-Json -Depth 12) | Set-Content -LiteralPath $metricsJsonPath -Encoding ASCII

"[L6-HARDEN] PASS=$overallPass drift_pass=$driftOverallPass cal_gate_pass=$($calGateMetrics.overall_pass) drift_checks=$((($driftRows | Measure-Object -Property pass -Sum).Sum))/$($driftRows.Count)"
if (-not $overallPass) {
    exit 1
}
exit 0
