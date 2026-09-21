param(
    [ValidateSet("fast", "strict")]
    [string]$Profile = "fast",
    [string]$Manifest = "datasets/level5/manifests/level5_official_testset_v1_manifest.json",
    [string]$ExternalManifest = "external_tests_manifest.json",
    [int]$Runs = 12,
    [int]$GateRetryCount = 3,
    [int]$RetryDelayMs = 250,
    [double]$MaxFlakeRate = 0.05,
    [double]$SpreadBudget = 0.0,
    [string]$ReportCsv = "level5_chaos_soak_runtime.csv",
    [string]$MetricsJson = "level5_chaos_soak_metrics.json",
    [string]$OutputDir = "datasets/level5/quality_reports"
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

function Stop-L5GateExecutables {
    $exeNames = @("c64_11", "c64_11_fast_signoff", "c64_11_strict_signoff")
    foreach ($name in $exeNames) {
        $procs = @(Get-Process -Name $name -ErrorAction SilentlyContinue)
        foreach ($p in $procs) {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

function Invoke-L5GateWithRetry {
    param(
        [string]$RepoPath,
        [string]$RunProfile,
        [string]$ManifestPath,
        [string]$ExternalManifestPath,
        [double]$Spread,
        [string]$RunOutputDir,
        [int]$RetryCount,
        [int]$DelayMs
    )

    if ($RetryCount -lt 1) {
        $RetryCount = 1
    }
    if ($DelayMs -lt 0) {
        $DelayMs = 0
    }

    $lastIssue = "unknown_failure"
    for ($attempt = 1; $attempt -le $RetryCount; ++$attempt) {
        Stop-L5GateExecutables
        try {
            if ($RunProfile -eq "strict") {
                & "$RepoPath\run_level5_diff_oracle_gate.ps1" -Manifest $ManifestPath -ExternalManifest $ExternalManifestPath -SpreadBudget $Spread -OutputDir $RunOutputDir
            }
            else {
                & "$RepoPath\run_level5_cycle_coupling_gate.ps1" -Profile "fast" -Manifest $ManifestPath -OutputDir $RunOutputDir
            }

            if ($LASTEXITCODE -ne 0) {
                $lastIssue = "gate_exit=$LASTEXITCODE"
            }
            else {
                return [pscustomobject]@{
                    pass = $true
                    issue = "ok"
                    attempts = $attempt
                }
            }
        }
        catch {
            $lastIssue = $_.Exception.Message
        }

        if ($attempt -lt $RetryCount) {
            Start-Sleep -Milliseconds ($DelayMs * $attempt)
        }
    }

    return [pscustomobject]@{
        pass = $false
        issue = $lastIssue
        attempts = $RetryCount
    }
}

if ($Runs -lt 1) {
    throw "Runs must be >= 1"
}
if ($GateRetryCount -lt 1) {
    throw "GateRetryCount must be >= 1"
}

$manifestPath = Resolve-LocalPath -PathInput $Manifest
$externalManifestPath = Resolve-LocalPath -PathInput $ExternalManifest
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Missing manifest: $manifestPath"
}
if (-not (Test-Path -LiteralPath $externalManifestPath)) {
    throw "Missing external manifest: $externalManifestPath"
}

$outputRoot = Resolve-LocalPath -PathInput $OutputDir
Ensure-Directory -Path $outputRoot

$reportPath = if ([System.IO.Path]::IsPathRooted($ReportCsv)) { $ReportCsv } else { Join-Path -Path $outputRoot -ChildPath $ReportCsv }
$metricsPath = if ([System.IO.Path]::IsPathRooted($MetricsJson)) { $MetricsJson } else { Join-Path -Path $outputRoot -ChildPath $MetricsJson }

$rows = @()
$passRuns = 0
$firstFailedRun = 0

for ($i = 1; $i -le $Runs; ++$i) {
    $runOutDir = Join-Path -Path $outputRoot -ChildPath ("l5_chaos_run{0}" -f $i)
    Ensure-Directory -Path $runOutDir

    $runResult = Invoke-L5GateWithRetry -RepoPath $repo -RunProfile $Profile -ManifestPath $manifestPath -ExternalManifestPath $externalManifestPath -Spread $SpreadBudget -RunOutputDir $runOutDir -RetryCount $GateRetryCount -DelayMs $RetryDelayMs
    $runPass = [bool]$runResult.pass
    $runIssue = [string]$runResult.issue
    $runAttempts = [int]$runResult.attempts

    if ($runPass) {
        $passRuns++
    }
    elseif ($firstFailedRun -eq 0) {
        $firstFailedRun = $i
    }

    $rows += [pscustomobject]@{
        run = $i
        profile = $Profile
        attempts = $runAttempts
        pass = if ($runPass) { 1 } else { 0 }
        issue = $runIssue
    }

    "[L5-CHAOS] run=$i profile=$Profile attempts=$runAttempts pass=$runPass issue=$runIssue"
}

$rows | Export-Csv -LiteralPath $reportPath -NoTypeInformation -Encoding ASCII

$failRuns = $Runs - $passRuns
$flakeRate = [double]$failRuns / [double]$Runs
$overallPass = ($flakeRate -le $MaxFlakeRate)

$metrics = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    profile = $Profile
    manifest = $manifestPath
    external_manifest = $externalManifestPath
    metrics = [ordered]@{
        runs_total = $Runs
        runs_pass = $passRuns
        runs_fail = $failRuns
        first_failed_run = if ($firstFailedRun -eq 0) { -1 } else { $firstFailedRun }
        flake_rate = $flakeRate
        gate_retry_count = $GateRetryCount
        differential_oracle_spread_budget = $SpreadBudget
    }
    budgets = [ordered]@{
        max_flake_rate = $MaxFlakeRate
        differential_oracle_spread_max = $SpreadBudget
    }
    overall_pass = $overallPass
}

($metrics | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $metricsPath -Encoding ASCII

"[L5-CHAOS] summary profile=$Profile pass=$passRuns/$Runs fail=$failRuns flake_rate=$('{0:N4}' -f $flakeRate) max_flake_rate=$MaxFlakeRate first_failed_run=$firstFailedRun"
if (-not $overallPass) {
    exit 1
}
exit 0
