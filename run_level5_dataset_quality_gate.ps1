param(
    [string]$Manifest = "datasets/level5/manifests/level5_dataset_manifest.sample.json",
    [string]$ReportJson = "datasets/level5/quality_reports/level5_dataset_quality_report.json",
    [string]$ReportCsv = "datasets/level5/quality_reports/level5_dataset_quality_report.csv",
    [string]$OutputDir = ""
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

function Test-HexSha256 {
    param([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $false
    }
    return ($Value -match '^[A-Fa-f0-9]{64}$')
}

function Add-Failure {
    param(
        [System.Collections.Generic.List[string]]$Errors,
        [string]$Message
    )
    $Errors.Add($Message) | Out-Null
}

$manifestPath = Resolve-LocalPath -PathInput $Manifest
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Missing manifest: $manifestPath"
}

$reportJsonPath = Resolve-LocalPath -PathInput $ReportJson
$reportCsvPath = Resolve-LocalPath -PathInput $ReportCsv

if (-not [string]::IsNullOrWhiteSpace($OutputDir)) {
    $outputDirFull = Resolve-LocalPath -PathInput $OutputDir
    if (-not (Test-Path -LiteralPath $outputDirFull)) {
        New-Item -ItemType Directory -Path $outputDirFull -Force | Out-Null
    }
    if (-not [System.IO.Path]::IsPathRooted($ReportJson)) {
        $reportJsonPath = Join-Path -Path $outputDirFull -ChildPath ([System.IO.Path]::GetFileName($ReportJson))
    }
    if (-not [System.IO.Path]::IsPathRooted($ReportCsv)) {
        $reportCsvPath = Join-Path -Path $outputDirFull -ChildPath ([System.IO.Path]::GetFileName($ReportCsv))
    }
}

$reportJsonDir = Split-Path -Path $reportJsonPath -Parent
if (-not [string]::IsNullOrWhiteSpace($reportJsonDir) -and -not (Test-Path -LiteralPath $reportJsonDir)) {
    New-Item -ItemType Directory -Path $reportJsonDir -Force | Out-Null
}
$reportCsvDir = Split-Path -Path $reportCsvPath -Parent
if (-not [string]::IsNullOrWhiteSpace($reportCsvDir) -and -not (Test-Path -LiteralPath $reportCsvDir)) {
    New-Item -ItemType Directory -Path $reportCsvDir -Force | Out-Null
}

$manifestObj = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($null -eq $manifestObj -or $null -eq $manifestObj.titles -or $manifestObj.titles.Count -lt 1) {
    throw "Invalid manifest structure (titles missing): $manifestPath"
}

$rows = @()

foreach ($title in $manifestObj.titles) {
    $id = [string]$title.id
    $format = [string]$title.format
    $fluxPath = Resolve-LocalPath -PathInput ([string]$title.path)
    $metadataPath = Resolve-LocalPath -PathInput ([string]$title.metadata)
    $oraclePath = Resolve-LocalPath -PathInput ([string]$title.oracle)

    $errors = New-Object 'System.Collections.Generic.List[string]'

    if ([string]::IsNullOrWhiteSpace($id)) { Add-Failure -Errors $errors -Message "missing_id" }
    if ([string]::IsNullOrWhiteSpace($format)) { Add-Failure -Errors $errors -Message "missing_format" }

    if (-not (Test-Path -LiteralPath $fluxPath)) {
        Add-Failure -Errors $errors -Message "missing_flux_path"
    }
    if (-not (Test-Path -LiteralPath $metadataPath)) {
        Add-Failure -Errors $errors -Message "missing_metadata"
    }
    if (-not (Test-Path -LiteralPath $oraclePath)) {
        Add-Failure -Errors $errors -Message "missing_oracle"
    }

    $md = $null
    if (Test-Path -LiteralPath $metadataPath) {
        try {
            $md = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
        }
        catch {
            Add-Failure -Errors $errors -Message "invalid_metadata_json"
        }
    }

    if ($null -ne $md) {
        if ($null -eq $md.dataset_version) { Add-Failure -Errors $errors -Message "metadata_missing_dataset_version" }
        if ([string]::IsNullOrWhiteSpace([string]$md.title_id)) { Add-Failure -Errors $errors -Message "metadata_missing_title_id" }
        if ([string]::IsNullOrWhiteSpace([string]$md.side)) { Add-Failure -Errors $errors -Message "metadata_missing_side" }
        if ([string]::IsNullOrWhiteSpace([string]$md.track_range) -or ([string]$md.track_range -notmatch '^[0-9]+-[0-9]+$')) {
            Add-Failure -Errors $errors -Message "metadata_invalid_track_range"
        }

        if ($null -eq $md.capture -or [string]::IsNullOrWhiteSpace([string]$md.capture.hardware) -or
            [string]::IsNullOrWhiteSpace([string]$md.capture.operator) -or
            $null -eq $md.capture.sample_rate_hz -or [int]$md.capture.sample_rate_hz -le 0) {
            Add-Failure -Errors $errors -Message "metadata_capture_incomplete"
        }

        if ($null -eq $md.capture -or [string]::IsNullOrWhiteSpace([string]$md.capture.timestamp_utc)) {
            Add-Failure -Errors $errors -Message "metadata_missing_timestamp"
        } else {
            $dt = [DateTime]::MinValue
            if (-not [DateTime]::TryParse([string]$md.capture.timestamp_utc, [ref]$dt)) {
                Add-Failure -Errors $errors -Message "metadata_invalid_timestamp"
            }
        }

        if ($null -eq $md.drive -or [string]::IsNullOrWhiteSpace([string]$md.drive.model) -or
            [string]::IsNullOrWhiteSpace([string]$md.drive.revision)) {
            Add-Failure -Errors $errors -Message "metadata_drive_incomplete"
        }

        if ($null -eq $md.mechanical -or $null -eq $md.mechanical.rpm_stddev) {
            Add-Failure -Errors $errors -Message "metadata_mechanical_incomplete"
        } else {
            if ([double]$md.mechanical.rpm_stddev -gt 1.5) {
                Add-Failure -Errors $errors -Message "rpm_stddev_above_threshold"
            }
        }

        if ($null -eq $md.source_integrity -or -not (Test-HexSha256 -Value ([string]$md.source_integrity.sha256_flux)) -or
            $null -eq $md.source_integrity.file_size_bytes -or [int64]$md.source_integrity.file_size_bytes -le 0) {
            Add-Failure -Errors $errors -Message "metadata_integrity_incomplete"
        }

        if ($null -eq $md.license -or [string]::IsNullOrWhiteSpace([string]$md.license.rights_note)) {
            Add-Failure -Errors $errors -Message "metadata_missing_rights"
        }
    }

    $or = $null
    if (Test-Path -LiteralPath $oraclePath) {
        try {
            $or = Get-Content -LiteralPath $oraclePath -Raw | ConvertFrom-Json
        }
        catch {
            Add-Failure -Errors $errors -Message "invalid_oracle_json"
        }
    }

    if ($null -ne $or) {
        if ($null -eq $or.oracle_version) { Add-Failure -Errors $errors -Message "oracle_missing_version" }
        if ([string]::IsNullOrWhiteSpace([string]$or.title_id)) { Add-Failure -Errors $errors -Message "oracle_missing_title_id" }

        if ($null -eq $or.timing_window -or [int]$or.timing_window.max_halfcycles -le 0 -or [int]$or.timing_window.min_done_hits -lt 1) {
            Add-Failure -Errors $errors -Message "oracle_invalid_timing_window"
        }

        if ($null -eq $or.iec_behavior -or -not ($or.iec_behavior.require_no_host_fallback -is [bool]) -or [int]$or.iec_behavior.max_retry_count -ne 0) {
            Add-Failure -Errors $errors -Message "oracle_invalid_iec_behavior"
        }

        if ($null -eq $or.error_semantics -or $null -eq $or.error_semantics.expected_dos_codes -or
            @($or.error_semantics.expected_dos_codes).Count -lt 1 -or [int]$or.error_semantics.max_unexpected_error_rows -ne 0) {
            Add-Failure -Errors $errors -Message "oracle_invalid_error_semantics"
        }

        if ($null -eq $or.loader_profile -or [int]$or.loader_profile.parity_max_mismatch_rows -ne 0) {
            Add-Failure -Errors $errors -Message "oracle_invalid_loader_profile"
        }

        if ($null -eq $or.weak_sync_profile) {
            Add-Failure -Errors $errors -Message "oracle_missing_weak_sync_profile"
        } else {
            if ([int]$or.weak_sync_profile.min_weak_events -lt 25) {
                Add-Failure -Errors $errors -Message "oracle_weak_events_below_threshold"
            }
            if ([int]$or.weak_sync_profile.min_sync_recovery_events -lt 15) {
                Add-Failure -Errors $errors -Message "oracle_sync_recovery_below_threshold"
            }
        }
    }

    $pass = ($errors.Count -eq 0)
    $rows += [pscustomobject]@{
        title_id = $id
        format = $format
        flux_path = $fluxPath
        metadata_path = $metadataPath
        oracle_path = $oraclePath
        pass = if ($pass) { 1 } else { 0 }
        issues = if ($pass) { "ok" } else { ($errors -join ";") }
    }

    "[L5-QUALITY] title=$id pass=$pass issues=$($rows[-1].issues)"
}

$passCount = @($rows | Where-Object { $_.pass -eq 1 }).Count
$total = $rows.Count

$reportObj = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    manifest = $manifestPath
    pass = $passCount
    total = $total
    pass_rate = if ($total -gt 0) { [double]$passCount / [double]$total } else { 0.0 }
    rows = $rows
}

($reportObj | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $reportJsonPath -Encoding ASCII
$rows | Export-Csv -LiteralPath $reportCsvPath -NoTypeInformation -Encoding ASCII

"[L5-QUALITY] summary pass=$passCount/$total report_json=$reportJsonPath report_csv=$reportCsvPath"
if ($passCount -ne $total) {
    exit 1
}
exit 0
