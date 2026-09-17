param(
    [string]$PathologicalManifest = "level4_pathological_corpus_manifest.json",
    [string]$AdvancedManifest = "advanced_real_corpus_manifest.json",
    [string]$RealGoldenManifest = "real_golden_manifest.json",
    [string]$DosRecoveryManifest = "advanced_dos_recovery_manifest.json",
    [string]$ExternalManifest = "external_tests_manifest.json",
    [string]$ReportCsv = "level4_kpi_gate_runtime.csv",
    [string]$KpiJson = "level4_kpi_metrics.json",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot
$artifactsHelpersPath = Join-Path -Path $repo -ChildPath "tools\artifacts.ps1"
. $artifactsHelpersPath

function Resolve-LocalPath {
    param([string]$PathInput)
    if ([System.IO.Path]::IsPathRooted($PathInput)) {
        return $PathInput
    }
    return (Join-Path -Path $repo -ChildPath $PathInput)
}

function Assert-PathExists {
    param([string]$Path, [string]$Label)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw ("Missing {0}: {1}" -f $Label, $Path)
    }
}

$pathologicalManifestPath = Resolve-LocalPath -PathInput $PathologicalManifest
$advancedManifestPath = Resolve-LocalPath -PathInput $AdvancedManifest
$realGoldenManifestPath = Resolve-LocalPath -PathInput $RealGoldenManifest
$dosRecoveryManifestPath = Resolve-LocalPath -PathInput $DosRecoveryManifest
$externalManifestPath = Resolve-LocalPath -PathInput $ExternalManifest

Assert-PathExists -Path $pathologicalManifestPath -Label "pathological manifest"
Assert-PathExists -Path $advancedManifestPath -Label "advanced manifest"
Assert-PathExists -Path $realGoldenManifestPath -Label "real golden manifest"
Assert-PathExists -Path $dosRecoveryManifestPath -Label "dos-recovery manifest"
Assert-PathExists -Path $externalManifestPath -Label "external manifest"

$runCtx = New-RunContext -RepoPath $repo -OutputDir $OutputDir
$outputDirFull = [string]$runCtx.OutputDir

$reportPath = Resolve-LocalPath -PathInput $ReportCsv
$kpiPath = Resolve-LocalPath -PathInput $KpiJson
if ($runCtx.Scoped) {
    if (-not [System.IO.Path]::IsPathRooted($ReportCsv)) {
        $reportPath = Join-Path -Path $outputDirFull -ChildPath $ReportCsv
    }
    if (-not [System.IO.Path]::IsPathRooted($KpiJson)) {
        $kpiPath = Join-Path -Path $outputDirFull -ChildPath $KpiJson
    }
}

function Resolve-RunOutputPath {
    param(
        [string]$OutputDir,
        [string]$FileName
    )
    if ([string]::IsNullOrWhiteSpace($OutputDir)) {
        return (Join-Path -Path $repo -ChildPath $FileName)
    }
    return (Join-Path -Path $OutputDir -ChildPath $FileName)
}

$savedProfile = [Environment]::GetEnvironmentVariable("C64_DRIVE_PROFILE", "Process")
try {
    [Environment]::SetEnvironmentVariable("C64_DRIVE_PROFILE", "level4-accuracy", "Process")

    & "$repo\run_advanced_real_corpus_gate.ps1" -Manifest $pathologicalManifestPath -Mode pure -DefaultMaxHalfCycles 700000 -ReportCsv "level4_pathological_runtime.csv" -OutputDir $outputDirFull
    if ($LASTEXITCODE -ne 0) {
        throw "Pathological corpus gate failed"
    }

    & "$repo\run_advanced_real_corpus_gate.ps1" -Manifest $advancedManifestPath -Mode pure -DefaultMaxHalfCycles 700000 -ReportCsv "advanced_real_corpus_runtime.csv" -OutputDir $outputDirFull
    if ($LASTEXITCODE -ne 0) {
        throw "Advanced real corpus gate failed"
    }

    & "$repo\run_real_golden_gate.ps1" -Manifest $realGoldenManifestPath -Mode pure -DefaultMaxHalfCycles 700000 -ReportCsv "real_golden_gate_runtime.csv" -OutputDir $outputDirFull
    if ($LASTEXITCODE -ne 0) {
        throw "Real golden gate failed"
    }

    & "$repo\run_advanced_dos_recovery_gate.ps1" -Manifest $dosRecoveryManifestPath -Profile fast -ExternalManifest $externalManifestPath -ReportCsv "advanced_dos_recovery_runtime.csv" -OutputDir $outputDirFull
    if ($LASTEXITCODE -ne 0) {
        throw "DOS recovery gate failed"
    }

    $pathologicalReport = Resolve-RunOutputPath -OutputDir $outputDirFull -FileName "level4_pathological_runtime.csv"
    $advancedReport = Resolve-RunOutputPath -OutputDir $outputDirFull -FileName "advanced_real_corpus_runtime.csv"
    $goldenReport = Resolve-RunOutputPath -OutputDir $outputDirFull -FileName "real_golden_gate_runtime.csv"
    $dosReport = Resolve-RunOutputPath -OutputDir $outputDirFull -FileName "advanced_dos_recovery_runtime.csv"
    Assert-PathExists -Path $pathologicalReport -Label "pathological report"
    Assert-PathExists -Path $advancedReport -Label "advanced report"
    Assert-PathExists -Path $goldenReport -Label "golden report"
    Assert-PathExists -Path $dosReport -Label "dos report"

    $pathologicalRows = @(Import-Csv -LiteralPath $pathologicalReport)
    $advancedRows = @(Import-Csv -LiteralPath $advancedReport)
    $goldenRows = @(Import-Csv -LiteralPath $goldenReport)
    $dosRows = @(Import-Csv -LiteralPath $dosReport)

    if ($pathologicalRows.Count -lt 1) { throw "Pathological report is empty" }
    if ($advancedRows.Count -lt 1) { throw "Advanced report is empty" }
    if ($goldenRows.Count -lt 1) { throw "Golden report is empty" }
    if ($dosRows.Count -lt 1) { throw "DOS report is empty" }

    $pathologicalPass = @($pathologicalRows | Where-Object { [string]$_.overall_pass -eq "True" }).Count
    $advancedPass = @($advancedRows | Where-Object { [string]$_.overall_pass -eq "True" }).Count
    $goldenPass = @($goldenRows | Where-Object { [string]$_.overall_pass -eq "True" }).Count
    $dosPass = @($dosRows | Where-Object { [int]$_.pass -eq 1 }).Count

    $pathologicalRate = [double]$pathologicalPass / [double]$pathologicalRows.Count
    $advancedRate = [double]$advancedPass / [double]$advancedRows.Count
    $goldenRate = [double]$goldenPass / [double]$goldenRows.Count
    $dosRate = [double]$dosPass / [double]$dosRows.Count

    $rawRows = @($pathologicalRows | Where-Object { [string]$_.format -eq "raw" })
    $rawRate = 0.0
    if ($rawRows.Count -gt 0) {
        $rawPass = @($rawRows | Where-Object { [string]$_.overall_pass -eq "True" }).Count
        $rawRate = [double]$rawPass / [double]$rawRows.Count
    }

    $oracleSpread = [Math]::Abs($pathologicalRate - $goldenRate)
    $overallPass = ($pathologicalRate -ge 1.0 -and $advancedRate -ge 1.0 -and $goldenRate -ge 1.0 -and $dosRate -ge 1.0 -and $rawRate -ge 1.0 -and $oracleSpread -le 0.0)

    $kpiObj = [ordered]@{
        generated_at_utc = [DateTime]::UtcNow.ToString("o")
        level4_profile = "level4-accuracy"
        metrics = [ordered]@{
            pathological_total = $pathologicalRows.Count
            pathological_pass = $pathologicalPass
            pathological_pass_rate = $pathologicalRate
            advanced_total = $advancedRows.Count
            advanced_pass = $advancedPass
            advanced_pass_rate = $advancedRate
            golden_total = $goldenRows.Count
            golden_pass = $goldenPass
            golden_pass_rate = $goldenRate
            dos_total = $dosRows.Count
            dos_pass = $dosPass
            dos_pass_rate = $dosRate
            pathological_raw_pass_rate = $rawRate
            differential_oracle_spread = $oracleSpread
        }
        budgets = [ordered]@{
            pathological_min_pass_rate = 1.0
            advanced_min_pass_rate = 1.0
            golden_min_pass_rate = 1.0
            dos_min_pass_rate = 1.0
            pathological_raw_min_pass_rate = 1.0
            differential_oracle_spread_max = 0.0
        }
        overall_pass = $overallPass
    }

    ($kpiObj | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $kpiPath -Encoding ASCII

    $csvRows = @(
        [pscustomobject]@{ metric = "pathological_pass_rate"; value = $pathologicalRate; budget = 1.0; pass = [int]($pathologicalRate -ge 1.0) },
        [pscustomobject]@{ metric = "advanced_pass_rate"; value = $advancedRate; budget = 1.0; pass = [int]($advancedRate -ge 1.0) },
        [pscustomobject]@{ metric = "golden_pass_rate"; value = $goldenRate; budget = 1.0; pass = [int]($goldenRate -ge 1.0) },
        [pscustomobject]@{ metric = "dos_pass_rate"; value = $dosRate; budget = 1.0; pass = [int]($dosRate -ge 1.0) },
        [pscustomobject]@{ metric = "pathological_raw_pass_rate"; value = $rawRate; budget = 1.0; pass = [int]($rawRate -ge 1.0) },
        [pscustomobject]@{ metric = "differential_oracle_spread"; value = $oracleSpread; budget = 0.0; pass = [int]($oracleSpread -le 0.0) }
    )
    $csvRows | Export-Csv -LiteralPath $reportPath -NoTypeInformation -Encoding ASCII

    "[L4-KPI] PASS: pathological=$pathologicalPass/$($pathologicalRows.Count) advanced=$advancedPass/$($advancedRows.Count) golden=$goldenPass/$($goldenRows.Count) dos=$dosPass/$($dosRows.Count) raw_rate=$('{0:N3}' -f $rawRate) spread=$('{0:N3}' -f $oracleSpread)"
    if (-not $overallPass) {
        exit 1
    }
    exit 0
}
finally {
    [Environment]::SetEnvironmentVariable("C64_DRIVE_PROFILE", $savedProfile, "Process")
}
