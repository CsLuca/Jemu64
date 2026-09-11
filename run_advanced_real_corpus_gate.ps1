param(
    [string]$Manifest = "advanced_real_corpus_manifest.json",
    [ValidateSet('pure', 'compat')]
    [string]$Mode = 'pure',
    [int]$DefaultMaxHalfCycles = 700000,
    [string]$ReportCsv = "advanced_real_corpus_runtime.csv"
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot

function Resolve-RepoPath {
    param([string]$InputPath)
    if ([System.IO.Path]::IsPathRooted($InputPath)) {
        return $InputPath
    }
    return (Join-Path -Path $repo -ChildPath $InputPath)
}

function Get-PropOrDefault {
    param($Obj, [string]$Name, $Default)
    if ($null -eq $Obj) { return $Default }
    $p = $Obj.PSObject.Properties[$Name]
    if ($null -eq $p) { return $Default }
    if ($null -eq $p.Value) { return $Default }
    return $p.Value
}

function Invoke-KernelRunner {
    param(
        [string]$RunnerPath,
        [string]$RunMode,
        [int]$MaxHalfCycles
    )

    $savedEap = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        if ($RunMode -eq 'compat') {
            $output = @(
                & $RunnerPath -Mode $RunMode -MaxHalfCycles $MaxHalfCycles -Repeat 1 -Quiet `
                    -EnableCompatClockAssist -EnableCompatRamSinkInject -EnableCompatRamSinkBulk `
                    -EnableReplayCiaLog -EnableDd00Trace -EnableDriveAutoTalkDir -EnableDriveAutoDirOnTalk0 `
                    -EnableDriveForceTalkOnDd0d8 -IecPolarity '0,0,0,0,0,0,1,0,1,1' 2>&1 | ForEach-Object { "$_" }
            )
        } else {
            $output = @(
                & $RunnerPath -Mode $RunMode -MaxHalfCycles $MaxHalfCycles -Repeat 1 -Quiet 2>&1 | ForEach-Object { "$_" }
            )
        }
    }
    finally {
        $ErrorActionPreference = $savedEap
    }
    $exitCode = $LASTEXITCODE
    $text = ($output | Out-String)

    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = $output
        Pass = ($exitCode -eq 0 -and $text -match 'pass=True')
        HostFallbackNo = ($text -match 'host_fallback_no=True')
    }
}

$manifestPath = Resolve-RepoPath -InputPath $Manifest
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Missing manifest: $manifestPath"
}

$runnerPath = Join-Path -Path $repo -ChildPath "run_kernel_iec_e2e.ps1"
if (-not (Test-Path -LiteralPath $runnerPath)) {
    throw "Missing runner script: $runnerPath"
}

$manifestObj = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($null -eq $manifestObj -or $null -eq $manifestObj.titles -or $manifestObj.titles.Count -lt 1) {
    throw "Invalid manifest (titles missing): $manifestPath"
}

$globalOracle = $manifestObj.global_oracle
$requireNoFallback = [bool](Get-PropOrDefault -Obj $globalOracle -Name "require_no_host_fallback" -Default $true)

$week81Path = Join-Path -Path $repo -ChildPath "week81_flux_behavior_parity_runtime.csv"
$week81Rows = @()
if (Test-Path -LiteralPath $week81Path) {
    $week81Rows = @(Import-Csv -LiteralPath $week81Path)
}

$results = @()

foreach ($title in $manifestObj.titles) {
    $titleId = [string](Get-PropOrDefault -Obj $title -Name "id" -Default "unknown")
    $format = ([string](Get-PropOrDefault -Obj $title -Name "format" -Default "")).ToLowerInvariant()
    $pathRaw = [string](Get-PropOrDefault -Obj $title -Name "path" -Default "")
    $titlePath = Resolve-RepoPath -InputPath $pathRaw
    $pathExists = Test-Path -LiteralPath $titlePath
    $oracle = $title.oracle

    $oracleOk = $true
    $note = ""

    if (-not $pathExists) {
        $oracleOk = $false
        $note = "path_missing"
    }

    if ($oracleOk -and $format -eq 'raw') {
        $sampleFloor = [int](Get-PropOrDefault -Obj $oracle -Name "sample_floor" -Default 0)
        $expectedSamples = [int](Get-PropOrDefault -Obj $oracle -Name "expected_sample_count" -Default 0)
        $rawCount = 0
        if (Test-Path -LiteralPath $titlePath -PathType Container) {
            $rawCount = @(
                Get-ChildItem -LiteralPath $titlePath -Filter "*.raw" -File -ErrorAction SilentlyContinue
            ).Count
        }

        if ($sampleFloor -gt 0 -and $rawCount -lt $sampleFloor) {
            $oracleOk = $false
            $note = "raw_floor_failed:$rawCount<$sampleFloor"
        }
        if ($oracleOk -and $expectedSamples -gt 0 -and $rawCount -ne $expectedSamples) {
            $oracleOk = $false
            $note = "raw_expected_failed:$rawCount!=$expectedSamples"
        }

        if ($oracleOk) {
            if ($week81Rows.Count -lt 1) {
                $oracleOk = $false
                $note = "missing_week81_runtime"
            } else {
                $maxFlux = 0
                $maxWeak = 0
                $maxSync = 0
                $maxParityRuns = 0
                $maxParityMismatch = 0
                foreach ($r in $week81Rows) {
                    $maxFlux = [Math]::Max($maxFlux, [int]$r.w81_flux_samples)
                    $maxWeak = [Math]::Max($maxWeak, [int]$r.w81_weak_halftrack_observed_rows)
                    $maxSync = [Math]::Max($maxSync, [int]$r.w81_syncloss_recovery_rows)
                    $maxParityRuns = [Math]::Max($maxParityRuns, [int]$r.w81_loader_timing_parity_runs)
                    $maxParityMismatch = [Math]::Max($maxParityMismatch, [int]$r.w81_loader_timing_parity_mismatch_rows)
                }

                $minWeak = [int](Get-PropOrDefault -Obj (Get-PropOrDefault -Obj $oracle -Name "weak_halftrack_observed" -Default $null) -Name "min_rows" -Default 0)
                $minSync = [int](Get-PropOrDefault -Obj (Get-PropOrDefault -Obj $oracle -Name "sync_loss_recovery" -Default $null) -Name "min_rows" -Default 0)
                $minParityRuns = [int](Get-PropOrDefault -Obj (Get-PropOrDefault -Obj $oracle -Name "loader_timing_parity" -Default $null) -Name "min_runs" -Default 0)
                $maxParityAllowed = [int](Get-PropOrDefault -Obj (Get-PropOrDefault -Obj $oracle -Name "loader_timing_parity" -Default $null) -Name "max_mismatches" -Default 0)

                if ($maxFlux -lt $sampleFloor) {
                    $oracleOk = $false
                    $note = "w81_flux_failed:$maxFlux<$sampleFloor"
                } elseif ($minWeak -gt 0 -and $maxWeak -lt $minWeak) {
                    $oracleOk = $false
                    $note = "w81_weak_failed:$maxWeak<$minWeak"
                } elseif ($minSync -gt 0 -and $maxSync -lt $minSync) {
                    $oracleOk = $false
                    $note = "w81_sync_failed:$maxSync<$minSync"
                } elseif ($minParityRuns -gt 0 -and $maxParityRuns -lt $minParityRuns) {
                    $oracleOk = $false
                    $note = "w81_parity_runs_failed:$maxParityRuns<$minParityRuns"
                } elseif ($maxParityMismatch -gt $maxParityAllowed) {
                    $oracleOk = $false
                    $note = "w81_parity_mismatch_failed:$maxParityMismatch>$maxParityAllowed"
                }
            }
        }
    }

    $maxHalfCycles = [int](Get-PropOrDefault -Obj (Get-PropOrDefault -Obj $oracle -Name "timing_window" -Default $null) -Name "max_halfcycles" -Default $DefaultMaxHalfCycles)
    if ($maxHalfCycles -le 0) {
        $maxHalfCycles = $DefaultMaxHalfCycles
    }

    $runner = Invoke-KernelRunner -RunnerPath $runnerPath -RunMode $Mode -MaxHalfCycles $maxHalfCycles
    $runnerOk = $runner.Pass
    if ($requireNoFallback -and -not $runner.HostFallbackNo) {
        $runnerOk = $false
        if ([string]::IsNullOrWhiteSpace($note)) {
            $note = "host_fallback_detected"
        }
    }

    $overall = ($pathExists -and $oracleOk -and $runnerOk)
    if ([string]::IsNullOrWhiteSpace($note)) {
        $note = if ($overall) { "ok" } else { "unknown_failure" }
    }

    $results += [pscustomobject]@{
        title_id = $titleId
        format = $format
        path = $titlePath
        path_exists = $pathExists
        oracle_ok = $oracleOk
        runner_pass = $runner.Pass
        runner_host_fallback_no = $runner.HostFallbackNo
        overall_pass = $overall
        note = $note
    }

    "[ADV-REAL-CORPUS] title=$titleId format=$format pass=$overall note=$note"
}

$reportPath = Resolve-RepoPath -InputPath $ReportCsv
$results | Export-Csv -LiteralPath $reportPath -NoTypeInformation -Encoding ASCII

$passCount = @($results | Where-Object { $_.overall_pass }).Count
$totalCount = $results.Count
"[ADV-REAL-CORPUS] summary pass=$passCount/$totalCount report=$reportPath"

if ($passCount -ne $totalCount) {
    exit 1
}

exit 0
