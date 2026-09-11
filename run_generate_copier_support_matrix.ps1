param(
    [string]$MatrixPath = "copier_matrix.json",
    [string]$ReportPath = "copier_matrix_report.json",
    [string]$OutputPath = "COPIER_SUPPORT_MATRIX.md"
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

$matrixFull = Resolve-LocalPath -PathInput $MatrixPath
$reportFull = Resolve-LocalPath -PathInput $ReportPath
$outputFull = Resolve-LocalPath -PathInput $OutputPath

if (-not (Test-Path -LiteralPath $matrixFull)) { throw "Missing matrix: $matrixFull" }
if (-not (Test-Path -LiteralPath $reportFull)) { throw "Missing report: $reportFull" }

$matrixObj = Get-Content -LiteralPath $matrixFull -Raw | ConvertFrom-Json
$reportObj = Get-Content -LiteralPath $reportFull -Raw | ConvertFrom-Json

if ($null -eq $matrixObj.scenarios -or $matrixObj.scenarios.Count -lt 1) {
    throw "Invalid matrix scenarios in $matrixFull"
}
if ($null -eq $reportObj.scenarios -or $reportObj.scenarios.Count -lt 1) {
    throw "Invalid report scenarios in $reportFull"
}

$reportMap = @{}
foreach ($r in $reportObj.scenarios) {
    $reportMap[[string]$r.scenario_id] = $r
}

$statusRows = @()
foreach ($s in $matrixObj.scenarios) {
    $id = [string]$s.id
    $level = [string]$s.level
    $mode = [string]$s.mode
    $fmt = "{0}->{1}" -f ([string]$s.source_format), ([string]$s.target_format)
    $row = $reportMap[$id]
    $status = "MISSING"
    if ($null -ne $row) {
        $status = if ([int]$row.overall_pass -eq 1) { "PASS" } else { "FAIL" }
    }
    $statusRows += [pscustomobject]@{
        id = $id
        level = $level
        mode = $mode
        format = $fmt
        status = $status
    }
}

$passCount = @($statusRows | Where-Object { $_.status -eq "PASS" }).Count
$totalCount = $statusRows.Count

$lines = @()
$lines += "# Copier Support Matrix"
$lines += ""
$lines += "## Scope"
$lines += ""
$lines += "Auto-generated from runtime report + matrix contract."
$lines += ""
$lines += "- Matrix source: ``copier_matrix.json``"
$lines += "- Runtime report: ``copier_matrix_report.json``"
$lines += "- Summary: ``PASS $passCount/$totalCount``"
$lines += ""
$lines += "## Scenario Status"
$lines += ""
$lines += "| Scenario ID | Level | Mode | Format | Status |"
$lines += "|---|---|---|---|---|"
foreach ($row in $statusRows) {
    $lines += "| {0} | {1} | {2} | {3} | {4} |" -f $row.id, $row.level, $row.mode, $row.format, $row.status
}
$lines += ""
$lines += "## Quasi-Closure Criteria (Phase 5)"
$lines += ""
$lines += "- ``run_signoff_week13_14.ps1`` remains green with matrix gate enabled."
$lines += "- ``run_copier_matrix.ps1`` remains full-pass on active scenario set."
$lines += "- ``run_check_phase5_closure.ps1`` and ``run_check_phase5_hard_thresholds.ps1`` remain green."
$lines += ""
$lines += "## Freeze Statement"
$lines += ""
$lines += "Phase 5 is currently considered **near-closure achieved** under frozen gate conditions."

[System.IO.File]::WriteAllLines($outputFull, $lines, [System.Text.Encoding]::ASCII)
"[COPIER-SUPPORT] PASS: generated=$outputFull pass=$passCount/$totalCount"
exit 0
