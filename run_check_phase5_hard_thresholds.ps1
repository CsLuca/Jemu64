param(
    [string]$Thresholds = "phase5_hard_thresholds.json",
    [string]$Matrix = "copier_matrix.json",
    [string]$Report = "copier_matrix_report.json"
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

$thresholdPath = Resolve-LocalPath -PathInput $Thresholds
$matrixPath = Resolve-LocalPath -PathInput $Matrix
$reportPath = Resolve-LocalPath -PathInput $Report

if (-not (Test-Path -LiteralPath $thresholdPath)) { throw "Missing thresholds file: $thresholdPath" }
if (-not (Test-Path -LiteralPath $matrixPath)) { throw "Missing matrix file: $matrixPath" }
if (-not (Test-Path -LiteralPath $reportPath)) { throw "Missing report file: $reportPath" }

$thresholdObj = Get-Content -LiteralPath $thresholdPath -Raw | ConvertFrom-Json
$matrixObj = Get-Content -LiteralPath $matrixPath -Raw | ConvertFrom-Json
$reportObj = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json

$reportRows = @($reportObj.scenarios)
$matrixRows = @($matrixObj.scenarios)
if ($reportRows.Count -lt 1 -or $matrixRows.Count -lt 1) {
    throw "Invalid matrix/report content"
}

$passRows = @($reportRows | Where-Object { [int]$_.overall_pass -eq 1 })
$passRate = [double]$passRows.Count / [double]$reportRows.Count
$runtimeExitCode = 999
if ($null -ne $reportObj.runtime_exit_code) {
    $runtimeExitCode = [int]$reportObj.runtime_exit_code
}

$minPassRate = [double]$thresholdObj.rules.matrix.min_pass_rate
$maxExitCode = [int]$thresholdObj.rules.matrix.max_runtime_exit_code
if ($passRate -lt $minPassRate) {
    throw "Phase5 threshold failed: passRate=$passRate < min=$minPassRate"
}
if ($runtimeExitCode -gt $maxExitCode) {
    throw "Phase5 threshold failed: runtime_exit_code=$runtimeExitCode > max=$maxExitCode"
}

$advancedRows = @($reportRows | Where-Object { [string]$_.level -eq "advanced" })
$advancedPassRows = @($advancedRows | Where-Object { [int]$_.overall_pass -eq 1 })
$minAdvanced = [int]$thresholdObj.rules.advanced.min_advanced_scenarios
if ($advancedRows.Count -lt $minAdvanced) {
    throw "Phase5 threshold failed: advanced scenarios=$($advancedRows.Count) < min=$minAdvanced"
}
$advancedPassRate = [double]$advancedPassRows.Count / [double]$advancedRows.Count
$minAdvancedPassRate = [double]$thresholdObj.rules.advanced.min_advanced_pass_rate
if ($advancedPassRate -lt $minAdvancedPassRate) {
    throw "Phase5 threshold failed: advanced passRate=$advancedPassRate < min=$minAdvancedPassRate"
}

$hardRows = @($reportRows | Where-Object {
    [string]$id = [string]$_.scenario_id
    $id -like "*_hard_*" -or $id -like "*_hard_baseline"
})
$hardFail = @($hardRows | Where-Object { [int]$_.overall_pass -ne 1 })
if ($hardFail.Count -gt 0) {
    $ids = @($hardFail | ForEach-Object { [string]$_.scenario_id })
    throw "Phase5 threshold failed: hard scenarios not passing: $($ids -join ', ')"
}

$relockFail = @($reportRows | Where-Object {
    ([string]$_.scenario_id -like "*relock*" -or [string]$_.scenario_id -like "*drift*") -and [int]$_.overall_pass -ne 1
}).Count
$jitterStaticFail = @($reportRows | Where-Object {
    [string]$_.scenario_id -like "*jitter*" -and [int]$_.overall_pass -ne 1
}).Count
$errorMapFail = @($reportRows | Where-Object {
    [string]$_.scenario_id -like "*dos_error_map*" -and [int]$_.overall_pass -ne 1
}).Count

$maxRelockFail = [int]$thresholdObj.rules.drift.max_relock_window_failures
$maxJitterFail = [int]$thresholdObj.rules.drift.max_jitter_digest_static_rows
$maxErrMapFail = [int]$thresholdObj.rules.drift.max_error_map_missing_rows

if ($relockFail -gt $maxRelockFail) {
    throw "Phase5 threshold failed: relock failures=$relockFail > max=$maxRelockFail"
}
if ($jitterStaticFail -gt $maxJitterFail) {
    throw "Phase5 threshold failed: jitter failures=$jitterStaticFail > max=$maxJitterFail"
}
if ($errorMapFail -gt $maxErrMapFail) {
    throw "Phase5 threshold failed: error-map failures=$errorMapFail > max=$maxErrMapFail"
}

"[PHASE5-HARD] PASS: pass_rate=$('{0:N3}' -f $passRate) advanced_pass_rate=$('{0:N3}' -f $advancedPassRate) hard_rows=$($hardRows.Count)"
exit 0
