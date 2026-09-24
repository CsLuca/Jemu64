param(
    [string]$Manifest = "datasets/level6/manifests/level6_capture_calibration_manifest_sample.json",
    [string]$BaseProfile = "config/iec_profiles/baseline_1541.json",
    [string]$OutputProfile = "config/iec_profiles/calibrated_synthetic_1541.json",
    [string]$ProfileId = "calibrated_synthetic_1541",
    [double]$BlendWithBase = 0.35,
    [string]$ReportJson = "datasets/level6/quality_reports/level6_calibration_fit_metrics.json"
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

function Blend-Int {
    param(
        [double]$BaseValue,
        [double]$MeasuredValue,
        [double]$BlendBase
    )
    $blend = [Math]::Round(($BaseValue * $BlendBase) + ($MeasuredValue * (1.0 - $BlendBase)), 0)
    if ($blend -lt 0) {
        $blend = 0
    }
    return [int]$blend
}

function Weighted-Average {
    param(
        [object[]]$Samples,
        [double[]]$Weights,
        [double]$Fallback
    )
    if ($null -eq $Samples -or $Samples.Count -eq 0) {
        return $Fallback
    }

    $sumW = 0.0
    $sum = 0.0
    for ($i = 0; $i -lt $Samples.Count; $i++) {
        $v = [double]$Samples[$i]
        $w = [double]$Weights[$i]
        if ($w -le 0) {
            continue
        }
        $sum += ($v * $w)
        $sumW += $w
    }
    if ($sumW -le 0.0) {
        return $Fallback
    }
    return ($sum / $sumW)
}

$manifestPath = Resolve-LocalPath -PathInput $Manifest
$baseProfilePath = Resolve-LocalPath -PathInput $BaseProfile
$outputProfilePath = Resolve-LocalPath -PathInput $OutputProfile
$reportJsonPath = Resolve-LocalPath -PathInput $ReportJson

if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Missing manifest: $manifestPath"
}
if (-not (Test-Path -LiteralPath $baseProfilePath)) {
    throw "Missing base profile: $baseProfilePath"
}

$manifestObj = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$base = Get-Content -LiteralPath $baseProfilePath -Raw | ConvertFrom-Json

$capturesList = @($manifestObj.captures)
if ($capturesList.Count -eq 0) {
    throw "Manifest has no captures entries: $manifestPath"
}

$captures = @()
foreach ($entry in $capturesList) {
    $entryPath = ""
    if ($entry -is [string]) {
        $entryPath = [string]$entry
    } elseif ($entry -is [System.Collections.IDictionary]) {
        if ($entry.Contains("path")) {
            $entryPath = [string]$entry["path"]
        }
    } elseif ($null -ne $entry -and $null -ne $entry.PSObject) {
        $pathProp = $entry.PSObject.Properties["path"]
        if ($null -ne $pathProp) {
            $entryPath = [string]$pathProp.Value
        }
    }
    if ([string]::IsNullOrWhiteSpace($entryPath)) {
        throw "Capture entry has empty path in manifest: $manifestPath"
    }
    $p = Resolve-LocalPath -PathInput $entryPath
    if (-not (Test-Path -LiteralPath $p)) {
        throw "Missing capture file: $p"
    }
    $obj = Get-Content -LiteralPath $p -Raw | ConvertFrom-Json
    $weight = $null
    if ($entry -is [System.Collections.IDictionary]) {
        if ($entry.Contains("weight")) {
            $weight = $entry["weight"]
        }
    } elseif ($null -ne $entry -and $null -ne $entry.PSObject) {
        $weightProp = $entry.PSObject.Properties["weight"]
        if ($null -ne $weightProp) {
            $weight = $weightProp.Value
        }
    }
    $w = if ($null -ne $weight) { [double]$weight } else { 1.0 }
    if ($w -le 0) {
        $w = 1.0
    }
    $captures += [pscustomobject]@{
        path = $p
        weight = $w
        metrics = $obj.metrics
    }
}

$weights = @($captures | ForEach-Object { [double]$_.weight })

function Sample-Field {
    param([string]$Expr)
    $values = @()
    foreach ($c in $captures) {
        $cursor = $c.metrics
        foreach ($part in $Expr.Split('.')) {
            if ($null -eq $cursor) {
                break
            }
            $cursor = $cursor.$part
        }
        if ($null -ne $cursor) {
            $values += [double]$cursor
        } else {
            $values += $null
        }
    }
    return ,$values
}

function Average-Field {
    param(
        [string]$Expr,
        [double]$Fallback
    )
    $samples = @()
    $sampleWeights = @()
    $raw = Sample-Field -Expr $Expr
    for ($i = 0; $i -lt $raw.Count; $i++) {
        if ($null -eq $raw[$i]) {
            continue
        }
        $samples += [double]$raw[$i]
        $sampleWeights += [double]$weights[$i]
    }
    return (Weighted-Average -Samples $samples -Weights $sampleWeights -Fallback $Fallback)
}

$lineAtnReleaseAvg = Average-Field -Expr "line.atn.release_delay_ticks" -Fallback ([double]$base.line.atn.release_delay_ticks)
$lineClkReleaseAvg = Average-Field -Expr "line.clk.release_delay_ticks" -Fallback ([double]$base.line.clk.release_delay_ticks)
$lineDataReleaseAvg = Average-Field -Expr "line.data.release_delay_ticks" -Fallback ([double]$base.line.data.release_delay_ticks)

$lineAtnMinAvg = Average-Field -Expr "line.atn.min_low_pulse_ticks" -Fallback ([double]$base.line.atn.min_low_pulse_ticks)
$lineClkMinAvg = Average-Field -Expr "line.clk.min_low_pulse_ticks" -Fallback ([double]$base.line.clk.min_low_pulse_ticks)
$lineDataMinAvg = Average-Field -Expr "line.data.min_low_pulse_ticks" -Fallback ([double]$base.line.data.min_low_pulse_ticks)

# Procedure: calibrate protocol-level timing windows from capture metrics when present.
$timingControllerBitHoldAvg = Average-Field -Expr "timing.controller_bit_hold_ticks" -Fallback ([double]$base.timing.controller_bit_hold_ticks)
$timingDeviceBitHoldAvg = Average-Field -Expr "timing.device_bit_hold_ticks" -Fallback ([double]$base.timing.device_bit_hold_ticks)
$timingControllerBetweenBytesAvg = Average-Field -Expr "timing.controller_between_bytes_ticks" -Fallback ([double]$base.timing.controller_between_bytes_ticks)
$timingDeviceBetweenBytesAvg = Average-Field -Expr "timing.device_between_bytes_ticks" -Fallback ([double]$base.timing.device_between_bytes_ticks)
$timingAtnResponseTimeoutAvg = Average-Field -Expr "timing.atn_response_timeout_ticks" -Fallback ([double]$base.timing.atn_response_timeout_ticks)
$timingDeviceNotPresentTimeoutAvg = Average-Field -Expr "timing.device_not_present_timeout_ticks" -Fallback ([double]$base.timing.device_not_present_timeout_ticks)
$timingSenderTimeoutAvg = Average-Field -Expr "timing.sender_timeout_ticks" -Fallback ([double]$base.timing.sender_timeout_ticks)
$timingReceiverTimeoutAvg = Average-Field -Expr "timing.receiver_timeout_ticks" -Fallback ([double]$base.timing.receiver_timeout_ticks)
$timingEoiSignalMinAvg = Average-Field -Expr "timing.eoi_signal_min_ticks" -Fallback ([double]$base.timing.eoi_signal_min_ticks)
$timingEoiSignalMaxAvg = Average-Field -Expr "timing.eoi_signal_max_ticks" -Fallback ([double]$base.timing.eoi_signal_max_ticks)
$timingEmptyStreamTimeoutAvg = Average-Field -Expr "timing.empty_stream_timeout_ticks" -Fallback ([double]$base.timing.empty_stream_timeout_ticks)

$analogVddAvg = Average-Field -Expr "analog.vdd_milli" -Fallback ([double]$base.analog.vdd_milli)
$analogRiseThresholdAvg = Average-Field -Expr "analog.rise_threshold_milli" -Fallback ([double]$base.analog.rise_threshold_milli)
$analogFallThresholdAvg = Average-Field -Expr "analog.fall_threshold_milli" -Fallback ([double]$base.analog.fall_threshold_milli)
$analogRiseTauAvg = Average-Field -Expr "analog.rise_tau_ticks" -Fallback ([double]$base.analog.rise_tau_ticks)
$analogFallTauAvg = Average-Field -Expr "analog.fall_tau_ticks" -Fallback ([double]$base.analog.fall_tau_ticks)

$hostSkewAvg = Average-Field -Expr "analog.node.host.skew_ticks" -Fallback ([double]$base.analog.node.host.skew_ticks)
$hostRiseTauAvg = Average-Field -Expr "analog.node.host.rise_tau_ticks" -Fallback ([double]$base.analog.node.host.rise_tau_ticks)
$hostFallTauAvg = Average-Field -Expr "analog.node.host.fall_tau_ticks" -Fallback ([double]$base.analog.node.host.fall_tau_ticks)

$driveSkewAvg = Average-Field -Expr "analog.node.drive.skew_ticks" -Fallback ([double]$base.analog.node.drive.skew_ticks)
$driveRiseTauAvg = Average-Field -Expr "analog.node.drive.rise_tau_ticks" -Fallback ([double]$base.analog.node.drive.rise_tau_ticks)
$driveFallTauAvg = Average-Field -Expr "analog.node.drive.fall_tau_ticks" -Fallback ([double]$base.analog.node.drive.fall_tau_ticks)

$blend = $BlendWithBase
if ($blend -lt 0.0) { $blend = 0.0 }
if ($blend -gt 1.0) { $blend = 1.0 }

$out = [ordered]@{
    profile_id = $ProfileId
    base = [string]$base.profile_id
    model_mode = "physical-l6"
    line = [ordered]@{
        atn = [ordered]@{
            release_delay_ticks = Blend-Int -BaseValue ([double]$base.line.atn.release_delay_ticks) -MeasuredValue $lineAtnReleaseAvg -BlendBase $blend
            min_low_pulse_ticks = Blend-Int -BaseValue ([double]$base.line.atn.min_low_pulse_ticks) -MeasuredValue $lineAtnMinAvg -BlendBase $blend
        }
        clk = [ordered]@{
            release_delay_ticks = Blend-Int -BaseValue ([double]$base.line.clk.release_delay_ticks) -MeasuredValue $lineClkReleaseAvg -BlendBase $blend
            min_low_pulse_ticks = Blend-Int -BaseValue ([double]$base.line.clk.min_low_pulse_ticks) -MeasuredValue $lineClkMinAvg -BlendBase $blend
        }
        data = [ordered]@{
            release_delay_ticks = Blend-Int -BaseValue ([double]$base.line.data.release_delay_ticks) -MeasuredValue $lineDataReleaseAvg -BlendBase $blend
            min_low_pulse_ticks = Blend-Int -BaseValue ([double]$base.line.data.min_low_pulse_ticks) -MeasuredValue $lineDataMinAvg -BlendBase $blend
        }
    }
    timing = [ordered]@{
        controller_bit_hold_ticks = Blend-Int -BaseValue ([double]$base.timing.controller_bit_hold_ticks) -MeasuredValue $timingControllerBitHoldAvg -BlendBase $blend
        device_bit_hold_ticks = Blend-Int -BaseValue ([double]$base.timing.device_bit_hold_ticks) -MeasuredValue $timingDeviceBitHoldAvg -BlendBase $blend
        controller_between_bytes_ticks = Blend-Int -BaseValue ([double]$base.timing.controller_between_bytes_ticks) -MeasuredValue $timingControllerBetweenBytesAvg -BlendBase $blend
        device_between_bytes_ticks = Blend-Int -BaseValue ([double]$base.timing.device_between_bytes_ticks) -MeasuredValue $timingDeviceBetweenBytesAvg -BlendBase $blend
        atn_response_timeout_ticks = Blend-Int -BaseValue ([double]$base.timing.atn_response_timeout_ticks) -MeasuredValue $timingAtnResponseTimeoutAvg -BlendBase $blend
        device_not_present_timeout_ticks = Blend-Int -BaseValue ([double]$base.timing.device_not_present_timeout_ticks) -MeasuredValue $timingDeviceNotPresentTimeoutAvg -BlendBase $blend
        sender_timeout_ticks = Blend-Int -BaseValue ([double]$base.timing.sender_timeout_ticks) -MeasuredValue $timingSenderTimeoutAvg -BlendBase $blend
        receiver_timeout_ticks = Blend-Int -BaseValue ([double]$base.timing.receiver_timeout_ticks) -MeasuredValue $timingReceiverTimeoutAvg -BlendBase $blend
        eoi_signal_min_ticks = Blend-Int -BaseValue ([double]$base.timing.eoi_signal_min_ticks) -MeasuredValue $timingEoiSignalMinAvg -BlendBase $blend
        eoi_signal_max_ticks = Blend-Int -BaseValue ([double]$base.timing.eoi_signal_max_ticks) -MeasuredValue $timingEoiSignalMaxAvg -BlendBase $blend
        empty_stream_timeout_ticks = Blend-Int -BaseValue ([double]$base.timing.empty_stream_timeout_ticks) -MeasuredValue $timingEmptyStreamTimeoutAvg -BlendBase $blend
    }
    rx = [ordered]@{
        setup_ticks = [int]$base.rx.setup_ticks
        hold_ticks = [int]$base.rx.hold_ticks
    }
    timeout = [ordered]@{
        hysteresis_ticks = [int]$base.timeout.hysteresis_ticks
    }
    analog = [ordered]@{
        enabled = $true
        vdd_milli = Blend-Int -BaseValue ([double]$base.analog.vdd_milli) -MeasuredValue $analogVddAvg -BlendBase $blend
        rise_threshold_milli = Blend-Int -BaseValue ([double]$base.analog.rise_threshold_milli) -MeasuredValue $analogRiseThresholdAvg -BlendBase $blend
        fall_threshold_milli = Blend-Int -BaseValue ([double]$base.analog.fall_threshold_milli) -MeasuredValue $analogFallThresholdAvg -BlendBase $blend
        rise_tau_ticks = [Math]::Max(1, (Blend-Int -BaseValue ([double]$base.analog.rise_tau_ticks) -MeasuredValue $analogRiseTauAvg -BlendBase $blend))
        fall_tau_ticks = [Math]::Max(1, (Blend-Int -BaseValue ([double]$base.analog.fall_tau_ticks) -MeasuredValue $analogFallTauAvg -BlendBase $blend))
        node = [ordered]@{
            host = [ordered]@{
                skew_ticks = Blend-Int -BaseValue ([double]$base.analog.node.host.skew_ticks) -MeasuredValue $hostSkewAvg -BlendBase $blend
                rise_tau_ticks = [Math]::Max(1, (Blend-Int -BaseValue ([double]$base.analog.node.host.rise_tau_ticks) -MeasuredValue $hostRiseTauAvg -BlendBase $blend))
                fall_tau_ticks = [Math]::Max(1, (Blend-Int -BaseValue ([double]$base.analog.node.host.fall_tau_ticks) -MeasuredValue $hostFallTauAvg -BlendBase $blend))
            }
            drive = [ordered]@{
                skew_ticks = Blend-Int -BaseValue ([double]$base.analog.node.drive.skew_ticks) -MeasuredValue $driveSkewAvg -BlendBase $blend
                rise_tau_ticks = [Math]::Max(1, (Blend-Int -BaseValue ([double]$base.analog.node.drive.rise_tau_ticks) -MeasuredValue $driveRiseTauAvg -BlendBase $blend))
                fall_tau_ticks = [Math]::Max(1, (Blend-Int -BaseValue ([double]$base.analog.node.drive.fall_tau_ticks) -MeasuredValue $driveFallTauAvg -BlendBase $blend))
            }
        }
    }
    calibration_meta = [ordered]@{
        generated_at_utc = [DateTime]::UtcNow.ToString("o")
        manifest = $manifestPath
        captures_used = [int]$captures.Count
        blend_with_base = $blend
    }
}

if ($out.analog.rise_threshold_milli -gt $out.analog.vdd_milli) {
    $out.analog.rise_threshold_milli = $out.analog.vdd_milli
}
if ($out.analog.fall_threshold_milli -gt $out.analog.vdd_milli) {
    $out.analog.fall_threshold_milli = $out.analog.vdd_milli
}

Ensure-Directory -Path (Split-Path -Path $outputProfilePath -Parent)
($out | ConvertTo-Json -Depth 16) | Set-Content -LiteralPath $outputProfilePath -Encoding ASCII

$report = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    manifest = $manifestPath
    base_profile = $baseProfilePath
    output_profile = $outputProfilePath
    profile_id = $ProfileId
    captures_used = [int]$captures.Count
    blend_with_base = $blend
    fitted = [ordered]@{
        line = $out.line
        timing = $out.timing
        analog = $out.analog
    }
}

Ensure-Directory -Path (Split-Path -Path $reportJsonPath -Parent)
($report | ConvertTo-Json -Depth 16) | Set-Content -LiteralPath $reportJsonPath -Encoding ASCII

"[L6-CALIBRATE] PASS=True captures=$($captures.Count) out=$outputProfilePath report=$reportJsonPath"
exit 0
