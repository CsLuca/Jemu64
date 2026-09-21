param(
    [ValidateSet("fast", "strict")]
    [string]$Profile = "fast",
    [string]$Manifest = "datasets/level5/manifests/level5_official_testset_v1_manifest.json",
    [string]$ExternalManifest = "external_tests_manifest.json",
    [int]$Runs = 12,
    [int]$GateRetryCount = 3,
    [int]$RetryDelayMs = 250,
    [int]$GateAttemptTimeoutSec = 900,
    [switch]$EnableWarmup,
    [int]$WarmupKernelRepeat = 1,
    [int]$WarmupTimeoutSec = 300,
    [int]$KernelOnlyRetryCount = 2,
    [int]$KernelRecoveryTimeoutSec = 300,
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

function Invoke-ChildScriptWithTimeout {
    param(
        [string]$ScriptPath,
        [hashtable]$NamedArgs,
        [int]$TimeoutSec
    )

    $argList = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $ScriptPath)
    foreach ($key in $NamedArgs.Keys) {
        $value = $NamedArgs[$key]
        if ($null -eq $value) {
            continue
        }
        if ($value -is [bool]) {
            if ($value) {
                $argList += "-$key"
            }
            continue
        }
        $argList += "-$key"
        $argList += [string]$value
    }

    $token = [Guid]::NewGuid().ToString("N")
    $stdoutPath = Join-Path -Path $env:TEMP -ChildPath ("l5_gate_stdout_{0}.log" -f $token)
    $stderrPath = Join-Path -Path $env:TEMP -ChildPath ("l5_gate_stderr_{0}.log" -f $token)

    try {
        $proc = Start-Process -FilePath "powershell.exe" -ArgumentList $argList -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
        $timedOut = $false
        try {
            Wait-Process -Id $proc.Id -Timeout $TimeoutSec -ErrorAction Stop
        }
        catch {
            $timedOut = $true
        }

        if ($timedOut) {
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
            Stop-L5GateExecutables
            return [pscustomobject]@{
                exit_code = -1
                timed_out = $true
                output = "gate_timeout"
            }
        }

        $stdOut = if (Test-Path -LiteralPath $stdoutPath) { [string](Get-Content -LiteralPath $stdoutPath -Raw) } else { "" }
        $stdErr = if (Test-Path -LiteralPath $stderrPath) { [string](Get-Content -LiteralPath $stderrPath -Raw) } else { "" }
        $combined = ($stdOut + "`n" + $stdErr).Trim()
        return [pscustomobject]@{
            exit_code = [int]$proc.ExitCode
            timed_out = $false
            output = $combined
        }
    }
    finally {
        Remove-Item -LiteralPath $stdoutPath -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue
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
        [int]$DelayMs,
        [int]$TimeoutSec
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
        if ($RunProfile -eq "strict") {
            $scriptPath = "$RepoPath\run_level5_diff_oracle_gate.ps1"
            $args = @{
                Manifest = $ManifestPath
                ExternalManifest = $ExternalManifestPath
                SpreadBudget = $Spread
                OutputDir = $RunOutputDir
            }
        }
        else {
            $scriptPath = "$RepoPath\run_level5_cycle_coupling_gate.ps1"
            $args = @{
                Profile = "fast"
                Manifest = $ManifestPath
                EnableKernelWarmup = $true
                KernelWarmupRepeat = 1
                OutputDir = $RunOutputDir
            }
        }

        $result = Invoke-ChildScriptWithTimeout -ScriptPath $scriptPath -NamedArgs $args -TimeoutSec $TimeoutSec
        if ($result.timed_out) {
            $lastIssue = "gate_timeout"
        }
        elseif ($result.exit_code -eq 0) {
            return [pscustomobject]@{
                pass = $true
                issue = "ok"
                attempts = $attempt
            }
        }
        elseif ([string]$result.output -match "Kernel IEC E2E failed") {
            $lastIssue = "Kernel IEC E2E failed under level5-coupling profile"
        }
        else {
            $lastIssue = "gate_exit=$($result.exit_code)"
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

function Invoke-WarmupRun {
    param(
        [string]$RepoPath,
        [int]$KernelRepeat,
        [int]$TimeoutSec
    )

    $repeat = [Math]::Max($KernelRepeat, 1)
    $result = Invoke-ChildScriptWithTimeout -ScriptPath "$RepoPath\run_kernel_iec_e2e.ps1" -NamedArgs @{
        Mode = "pure"
        MaxHalfCycles = 700000
        Repeat = $repeat
        Quiet = $true
        UseTestOnlyPureCmdGuard = $true
    } -TimeoutSec $TimeoutSec
    return ($result.exit_code -eq 0 -and -not $result.timed_out)
}

function Invoke-KernelOnlyRecovery {
    param(
        [string]$RepoPath,
        [int]$RetryCount,
        [int]$DelayMs,
        [int]$TimeoutSec
    )

    if ($RetryCount -lt 1) {
        return $false
    }

    for ($attempt = 1; $attempt -le $RetryCount; ++$attempt) {
        $result = Invoke-ChildScriptWithTimeout -ScriptPath "$RepoPath\run_kernel_iec_e2e.ps1" -NamedArgs @{
            Mode = "pure"
            MaxHalfCycles = 700000
            Repeat = 2
            Quiet = $true
            UseTestOnlyPureCmdGuard = $true
        } -TimeoutSec $TimeoutSec
        if ($result.exit_code -eq 0 -and -not $result.timed_out) {
            return $true
        }
        if ($attempt -lt $RetryCount) {
            Start-Sleep -Milliseconds ($DelayMs * $attempt)
        }
    }

    return $false
}

if ($Runs -lt 1) {
    throw "Runs must be >= 1"
}
if ($GateRetryCount -lt 1) {
    throw "GateRetryCount must be >= 1"
}
if ($GateAttemptTimeoutSec -lt 30) {
    throw "GateAttemptTimeoutSec must be >= 30"
}
if ($KernelOnlyRetryCount -lt 0) {
    throw "KernelOnlyRetryCount must be >= 0"
}
if ($WarmupTimeoutSec -lt 30) {
    throw "WarmupTimeoutSec must be >= 30"
}
if ($KernelRecoveryTimeoutSec -lt 30) {
    throw "KernelRecoveryTimeoutSec must be >= 30"
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

    $warmupOk = $true
    if ($EnableWarmup) {
        $warmupOk = Invoke-WarmupRun -RepoPath $repo -KernelRepeat $WarmupKernelRepeat -TimeoutSec $WarmupTimeoutSec
    }

    $runResult = Invoke-L5GateWithRetry -RepoPath $repo -RunProfile $Profile -ManifestPath $manifestPath -ExternalManifestPath $externalManifestPath -Spread $SpreadBudget -RunOutputDir $runOutDir -RetryCount $GateRetryCount -DelayMs $RetryDelayMs -TimeoutSec $GateAttemptTimeoutSec
    $runPass = ([bool]$runResult.pass -and $warmupOk)
    $runIssue = if (-not $warmupOk) { "warmup_failed" } else { [string]$runResult.issue }
    $runAttempts = [int]$runResult.attempts

    if (-not $runPass -and $KernelOnlyRetryCount -gt 0) {
        $kernelRecovered = Invoke-KernelOnlyRecovery -RepoPath $repo -RetryCount $KernelOnlyRetryCount -DelayMs $RetryDelayMs -TimeoutSec $KernelRecoveryTimeoutSec
        if ($kernelRecovered -and $runResult.issue -like "*Kernel IEC E2E*") {
            $runResult = Invoke-L5GateWithRetry -RepoPath $repo -RunProfile $Profile -ManifestPath $manifestPath -ExternalManifestPath $externalManifestPath -Spread $SpreadBudget -RunOutputDir $runOutDir -RetryCount 1 -DelayMs $RetryDelayMs -TimeoutSec $GateAttemptTimeoutSec
            $runPass = [bool]$runResult.pass
            $runIssue = [string]$runResult.issue
            $runAttempts = $runAttempts + [int]$runResult.attempts
            if (-not $runPass) {
                $runIssue = "post_kernel_recovery_failed:$runIssue"
            }
            else {
                $runIssue = "recovered"
            }
        }
    }

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
        warmup_ok = if ($warmupOk) { 1 } else { 0 }
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
        gate_attempt_timeout_sec = $GateAttemptTimeoutSec
        warmup_timeout_sec = $WarmupTimeoutSec
        kernel_only_retry_count = $KernelOnlyRetryCount
        kernel_recovery_timeout_sec = $KernelRecoveryTimeoutSec
        warmup_enabled = if ($EnableWarmup) { 1 } else { 0 }
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
