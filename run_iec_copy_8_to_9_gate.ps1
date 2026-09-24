param(
    [ValidateSet("fast", "strict")]
    [string]$Profile = "fast",
    [string]$MatrixPath = "copier_matrix.json",
    [string]$Manifest = "external_tests_manifest.json",
    [string]$OutputDir = "datasets/level5/quality_reports/iec_copy_8_to_9_gate"
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

$matrixFull = Resolve-LocalPath -PathInput $MatrixPath
$manifestFull = Resolve-LocalPath -PathInput $Manifest
$outputRoot = Resolve-LocalPath -PathInput $OutputDir
Ensure-Directory -Path $outputRoot

$reportJson = Join-Path -Path $outputRoot -ChildPath "copy_8_to_9_matrix_report.json"
$reportCsv = Join-Path -Path $outputRoot -ChildPath "copy_8_to_9_matrix_report.csv"
$gateMetricsPath = Join-Path -Path $outputRoot -ChildPath "iec_copy_8_to_9_gate_metrics.json"
$gateRuntimePath = Join-Path -Path $outputRoot -ChildPath "iec_copy_8_to_9_gate_runtime.csv"

& "$repo\run_copier_matrix.ps1" -Profile $Profile -MatrixPath $matrixFull -Manifest $manifestFull -ReportJson $reportJson -ReportCsv $reportCsv -OutputDir $outputRoot -ReportPrefix "copy_8_to_9"

$matrixExit = $LASTEXITCODE
if (-not (Test-Path -LiteralPath $reportCsv)) {
    throw "Missing copier matrix report csv: $reportCsv"
}

$rows = @(Import-Csv -LiteralPath $reportCsv)

$fileRow = $rows | Where-Object { $_.scenario_id -eq "copy_8_to_9_file_e2e" } | Select-Object -First 1
$diskRow = $rows | Where-Object { $_.scenario_id -eq "copy_8_to_9_disk_e2e" } | Select-Object -First 1

if ($null -eq $fileRow -or $null -eq $diskRow) {
    throw "Missing copy_8_to_9 rows in matrix report"
}

$manifestCsvPath = Resolve-LocalPath -PathInput "copy_8_to_9_disk_e2e_manifest.csv"
if (-not (Test-Path -LiteralPath $manifestCsvPath)) {
    throw "Missing disk copy manifest csv: $manifestCsvPath"
}

$manifestRows = @(Import-Csv -LiteralPath $manifestCsvPath)
$manifestOk = $false
$srcChecksum = ""
$dstChecksum = ""
$manifestMatch = "0"
if ($manifestRows.Count -gt 0) {
    $last = $manifestRows[$manifestRows.Count - 1]
    $srcChecksum = [string]$last.src_checksum
    $dstChecksum = [string]$last.dst_checksum
    $manifestMatch = [string]$last.match
    $manifestOk = ($manifestMatch -eq "1") -and ($srcChecksum -eq $dstChecksum) -and (-not [string]::IsNullOrWhiteSpace($srcChecksum))
}

$runtimeExitOk = ($matrixExit -eq 0)
$fileRuntimeOk = ([string]$fileRow.runtime_exit_ok -eq "1")
$diskRuntimeOk = ([string]$diskRow.runtime_exit_ok -eq "1")
$fileMarkerOk = ([string]$fileRow.marker_pass -eq "1")
$diskMarkerOk = ([string]$diskRow.marker_pass -eq "1")
$overallPass = $fileMarkerOk -and $diskMarkerOk -and $manifestOk

@(
    [pscustomobject]@{ metric = "matrix_exit_ok_info"; value = if ($runtimeExitOk) { 1 } else { 0 }; budget = 1; pass = 1 },
    [pscustomobject]@{ metric = "file_runtime_ok_info"; value = if ($fileRuntimeOk) { 1 } else { 0 }; budget = 1; pass = 1 },
    [pscustomobject]@{ metric = "disk_runtime_ok_info"; value = if ($diskRuntimeOk) { 1 } else { 0 }; budget = 1; pass = 1 },
    [pscustomobject]@{ metric = "file_marker_ok"; value = if ($fileMarkerOk) { 1 } else { 0 }; budget = 1; pass = if ($fileMarkerOk) { 1 } else { 0 } },
    [pscustomobject]@{ metric = "disk_marker_ok"; value = if ($diskMarkerOk) { 1 } else { 0 }; budget = 1; pass = if ($diskMarkerOk) { 1 } else { 0 } },
    [pscustomobject]@{ metric = "manifest_checksum_match"; value = if ($manifestOk) { 1 } else { 0 }; budget = 1; pass = if ($manifestOk) { 1 } else { 0 } }
) | Export-Csv -LiteralPath $gateRuntimePath -NoTypeInformation -Encoding ASCII

$metrics = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    profile = $Profile
    matrix_path = $matrixFull
    external_manifest = $manifestFull
    matrix_report_json = $reportJson
    matrix_report_csv = $reportCsv
    disk_manifest_csv = $manifestCsvPath
    metrics = [ordered]@{
        matrix_exit_ok = $runtimeExitOk
        file_runtime_ok = $fileRuntimeOk
        disk_runtime_ok = $diskRuntimeOk
        file_marker_ok = $fileMarkerOk
        disk_marker_ok = $diskMarkerOk
        manifest_match = $manifestMatch
        src_checksum = $srcChecksum
        dst_checksum = $dstChecksum
        checksum_equal = ($srcChecksum -eq $dstChecksum)
    }
    overall_pass = $overallPass
}

($metrics | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $gateMetricsPath -Encoding ASCII

"[IEC-COPY-8-9] PASS=$overallPass profile=$Profile matrix_exit_ok=$runtimeExitOk file_marker_ok=$fileMarkerOk disk_marker_ok=$diskMarkerOk manifest_ok=$manifestOk src=$srcChecksum dst=$dstChecksum"
if (-not $overallPass) {
    exit 1
}
exit 0
