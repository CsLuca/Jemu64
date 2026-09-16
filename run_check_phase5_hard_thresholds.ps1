param(
    [string]$Thresholds = "phase5_hard_thresholds.json",
    [string]$Matrix = "copier_matrix.json",
    [string]$Report = "copier_matrix_report.json",
    [string]$AdvancedRealCorpusReport = "advanced_real_corpus_runtime.csv",
    [string]$AdvancedDosRecoveryReport = "advanced_dos_recovery_runtime.csv",
    [string]$SoakReport = "phase5_soak_runtime.csv",
    [string]$RevisionMetrics = "reference\\edge\\revision_tolerance_metrics.json",
    [string]$PromotionReportJson = "reference\\edge\\level1_vs_level2_vs_level3.json",
    [string]$PromotionReportCsv = "reference\\edge\\level1_vs_level2_vs_level3.csv",
    [string]$SignoffPromotionJson = "reference\\edge\\level_promotion_signoff.json",
    [string]$SignoffPromotionCsv = "reference\\edge\\level_promotion_signoff.csv",
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

$thresholdPath = Resolve-LocalPath -PathInput $Thresholds
$matrixPath = Resolve-LocalPath -PathInput $Matrix
$reportPath = Resolve-LocalPath -PathInput $Report
$advancedCorpusPath = Resolve-LocalPath -PathInput $AdvancedRealCorpusReport
$advancedDosPath = Resolve-LocalPath -PathInput $AdvancedDosRecoveryReport
$soakPath = Resolve-LocalPath -PathInput $SoakReport
$revisionMetricsPath = Resolve-LocalPath -PathInput $RevisionMetrics
$promotionJsonPath = Resolve-LocalPath -PathInput $PromotionReportJson
$promotionCsvPath = Resolve-LocalPath -PathInput $PromotionReportCsv
$signoffPromotionJsonPath = Resolve-LocalPath -PathInput $SignoffPromotionJson
$signoffPromotionCsvPath = Resolve-LocalPath -PathInput $SignoffPromotionCsv

if (-not [string]::IsNullOrWhiteSpace($OutputDir)) {
    $outputDirFull = Resolve-LocalPath -PathInput $OutputDir
    if (-not (Test-Path -LiteralPath $outputDirFull)) {
        New-Item -ItemType Directory -Path $outputDirFull -Force | Out-Null
    }

    if (-not [System.IO.Path]::IsPathRooted($Report)) {
        $reportPath = Join-Path -Path $outputDirFull -ChildPath $Report
    }
    if (-not [System.IO.Path]::IsPathRooted($AdvancedRealCorpusReport)) {
        $advancedCorpusPath = Join-Path -Path $outputDirFull -ChildPath $AdvancedRealCorpusReport
    }
    if (-not [System.IO.Path]::IsPathRooted($AdvancedDosRecoveryReport)) {
        $advancedDosPath = Join-Path -Path $outputDirFull -ChildPath $AdvancedDosRecoveryReport
    }
    if (-not [System.IO.Path]::IsPathRooted($SoakReport)) {
        $soakPath = Join-Path -Path $outputDirFull -ChildPath $SoakReport
    }
}

if (-not (Test-Path -LiteralPath $thresholdPath)) { throw "Missing thresholds file: $thresholdPath" }
if (-not (Test-Path -LiteralPath $matrixPath)) { throw "Missing matrix file: $matrixPath" }
if (-not (Test-Path -LiteralPath $reportPath)) { throw "Missing report file: $reportPath" }
if (-not (Test-Path -LiteralPath $advancedCorpusPath)) { throw "Missing advanced corpus report: $advancedCorpusPath" }
if (-not (Test-Path -LiteralPath $advancedDosPath)) { throw "Missing advanced DOS recovery report: $advancedDosPath" }
if (-not (Test-Path -LiteralPath $soakPath)) { throw "Missing soak report: $soakPath" }

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

$baselinePassRate = 0.0
$baselineRows = @($reportRows | Where-Object { [string]$_.level -eq "baseline" })
if ($baselineRows.Count -gt 0) {
    $baselinePassRows = @($baselineRows | Where-Object { [int]$_.overall_pass -eq 1 })
    $baselinePassRate = [double]$baselinePassRows.Count / [double]$baselineRows.Count
}

$timingCorePassRate = 0.0
if (Test-Path -LiteralPath $revisionMetricsPath) {
    $revisionObj = Get-Content -LiteralPath $revisionMetricsPath -Raw | ConvertFrom-Json
    if ($null -ne $revisionObj -and $null -ne $revisionObj.metrics) {
        $m = $revisionObj.metrics
        $timingChecks = @(
            ([int]$m.week45_drive_final_rows -gt 0),
            ([int]$m.week46_drive_iec_timing_rows -gt 0),
            ([int]$m.week47_host_timing_rows -gt 0),
            ([int]$m.week48_drive_core_rows -gt 0),
            ([int]$m.week49_drive_cpu_rows -gt 0),
            ([int]$m.week50_drive_opcode_rows -gt 0)
        )
        $timingPassCount = @($timingChecks | Where-Object { $_ }).Count
        $timingCorePassRate = [double]$timingPassCount / [double]$timingChecks.Count
    }
}

$advCorpusRows = @(Import-Csv -LiteralPath $advancedCorpusPath)
if ($advCorpusRows.Count -lt 1) {
    throw "Invalid advanced corpus report: $advancedCorpusPath"
}
$advCorpusPass = @($advCorpusRows | Where-Object { [string]$_.overall_pass -eq "True" }).Count
$advCorpusPassRate = [double]$advCorpusPass / [double]$advCorpusRows.Count

$advDosRows = @(Import-Csv -LiteralPath $advancedDosPath)
if ($advDosRows.Count -lt 1) {
    throw "Invalid advanced DOS recovery report: $advancedDosPath"
}
$advDosPass = @($advDosRows | Where-Object { [int]$_.pass -eq 1 }).Count
$advDosPassRate = [double]$advDosPass / [double]$advDosRows.Count

$soakRows = @(Import-Csv -LiteralPath $soakPath)
if ($soakRows.Count -lt 1) {
    throw "Invalid soak report: $soakPath"
}
$soakPassRows = @($soakRows | Where-Object { [int]$_.pass -eq 1 })
$soakPassRate = [double]$soakPassRows.Count / [double]$soakRows.Count
$flakeRate = 1.0 - $soakPassRate
$maxFlakeBudget = 0.05

$level1Pass = ($passRate -ge 1.0)
$level2Pass = $level1Pass -and ($baselinePassRate -ge 1.0) -and ($advancedPassRate -ge 1.0) -and ($timingCorePassRate -ge 1.0)
$level3Pass = $level2Pass -and ($advCorpusPassRate -ge 1.0) -and ($advDosPassRate -ge 1.0) -and ($flakeRate -le $maxFlakeBudget)

if (-not $level1Pass) {
    throw "Promotion policy failed: Level1 must be green (matrix pass rate = 1.0)"
}
if (-not $level2Pass) {
    throw "Promotion policy failed: Level2 requires parity matrix + timing core"
}
if (-not $level3Pass) {
    throw "Promotion policy failed: Level3 requires corpus/copier-hard + soak + flake budget"
}

$promotedLevel = if ($level3Pass) { "level3" } elseif ($level2Pass) { "level2" } elseif ($level1Pass) { "level1" } else { "none" }
$promotionObj = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    levels = [ordered]@{
        level1 = [ordered]@{
            pass = $level1Pass
            pass_rate = $passRate
        }
        level2 = [ordered]@{
            pass = $level2Pass
            parity_matrix_pass_rate = $passRate
            baseline_pass_rate = $baselinePassRate
            advanced_pass_rate = $advancedPassRate
            timing_core_pass_rate = $timingCorePassRate
        }
        level3 = [ordered]@{
            pass = $level3Pass
            corpus_pass_rate = $advCorpusPassRate
            dos_recovery_pass_rate = $advDosPassRate
            soak_pass_rate = $soakPassRate
            flake_rate = $flakeRate
            flake_budget = $maxFlakeBudget
        }
    }
    promoted_level = $promotedLevel
}

$promoDir = Split-Path -Path $promotionJsonPath -Parent
if (-not (Test-Path -LiteralPath $promoDir)) {
    New-Item -ItemType Directory -Path $promoDir | Out-Null
}

(@{ promotion = $promotionObj } | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $promotionJsonPath -Encoding ASCII

$promotionCsvRows = @(
    [pscustomobject]@{ level = "level1"; pass = [int]$level1Pass; pass_rate = $passRate; timing_core_pass_rate = ""; flake_rate = ""; promoted_level = $promotedLevel },
    [pscustomobject]@{ level = "level2"; pass = [int]$level2Pass; pass_rate = $passRate; timing_core_pass_rate = $timingCorePassRate; flake_rate = ""; promoted_level = $promotedLevel },
    [pscustomobject]@{ level = "level3"; pass = [int]$level3Pass; pass_rate = $advCorpusPassRate; timing_core_pass_rate = $timingCorePassRate; flake_rate = $flakeRate; promoted_level = $promotedLevel }
)
($promotionCsvRows | ConvertTo-Csv -NoTypeInformation) | Set-Content -LiteralPath $promotionCsvPath -Encoding ASCII

$signoffPromotionObj = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    levels = [ordered]@{
        level1 = [ordered]@{
            pass = $level1Pass
            matrix_pass_rate = $passRate
        }
        level2 = [ordered]@{
            pass = $level2Pass
            parity_matrix_pass_rate = $passRate
            baseline_matrix_pass_rate = $baselinePassRate
            advanced_matrix_pass_rate = $advancedPassRate
            timing_core_pass_rate = $timingCorePassRate
        }
    }
    promoted_level = $(if ($level2Pass) { "level2" } elseif ($level1Pass) { "level1" } else { "none" })
}

(@{ promotion = $signoffPromotionObj } | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $signoffPromotionJsonPath -Encoding ASCII

$signoffCsvRows = @(
    [pscustomobject]@{ level = "level1"; pass = [int]$level1Pass; matrix_pass_rate = $passRate; timing_core_pass_rate = ""; notes = "mandatory baseline" },
    [pscustomobject]@{ level = "level2"; pass = [int]$level2Pass; matrix_pass_rate = $passRate; timing_core_pass_rate = $timingCorePassRate; notes = "parity matrix + timing core" }
)
($signoffCsvRows | ConvertTo-Csv -NoTypeInformation) | Set-Content -LiteralPath $signoffPromotionCsvPath -Encoding ASCII

"[PHASE5-HARD] PASS: pass_rate=$('{0:N3}' -f $passRate) advanced_pass_rate=$('{0:N3}' -f $advancedPassRate) hard_rows=$($hardRows.Count) promoted=$promotedLevel flake=$('{0:N4}' -f $flakeRate)"
exit 0
