param(
    [string]$MatrixPath = "copier_matrix.json",
    [string]$ReportPath = "copier_matrix_report.json",
    [string]$ChecklistPath = "COPIER_SUPPORT_MATRIX.md",
    [switch]$GenerateChecklist
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
$checklistFull = Resolve-LocalPath -PathInput $ChecklistPath

if ($GenerateChecklist) {
    & "$repo\run_generate_copier_support_matrix.ps1" -MatrixPath $matrixFull -ReportPath $reportFull -OutputPath $checklistFull
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to generate support matrix before closure check"
    }
}

if (-not (Test-Path -LiteralPath $matrixFull)) {
    throw "Missing matrix file: $matrixFull"
}
if (-not (Test-Path -LiteralPath $reportFull)) {
    throw "Missing report file: $reportFull"
}
if (-not (Test-Path -LiteralPath $checklistFull)) {
    throw "Missing checklist file: $checklistFull"
}

$matrixObj = Get-Content -LiteralPath $matrixFull -Raw | ConvertFrom-Json
$reportObj = Get-Content -LiteralPath $reportFull -Raw | ConvertFrom-Json
$checklistText = Get-Content -LiteralPath $checklistFull -Raw

if ($null -eq $matrixObj.scenarios -or $matrixObj.scenarios.Count -lt 1) {
    throw "Invalid matrix scenarios in: $matrixFull"
}
if ($null -eq $reportObj.scenarios -or $reportObj.scenarios.Count -lt 1) {
    throw "Invalid report scenarios in: $reportFull"
}

$matrixIds = @($matrixObj.scenarios | ForEach-Object { [string]$_.id })
$reportRows = @($reportObj.scenarios)

if ($reportRows.Count -ne $matrixIds.Count) {
    throw "Phase5 gate mismatch: report scenarios=$($reportRows.Count) matrix scenarios=$($matrixIds.Count)"
}

$failedRows = @($reportRows | Where-Object { [int]$_.overall_pass -ne 1 })
if ($failedRows.Count -gt 0) {
    $failedIds = @($failedRows | ForEach-Object { [string]$_.scenario_id })
    throw "Phase5 gate failed scenarios: $($failedIds -join ', ')"
}

$missingInChecklist = @()
foreach ($id in $matrixIds) {
    if ($checklistText -notmatch [regex]::Escape($id)) {
        $missingInChecklist += $id
    }
}
if ($missingInChecklist.Count -gt 0) {
    throw "Checklist missing scenario IDs: $($missingInChecklist -join ', ')"
}

"[PHASE5-CLOSURE] PASS: scenarios=$($matrixIds.Count) report=$reportFull checklist=$checklistFull"
exit 0
