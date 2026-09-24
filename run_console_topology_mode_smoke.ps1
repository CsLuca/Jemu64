param(
    [string]$OutputDir = "datasets/level6/quality_reports/console_smoke",
    [string]$ReportCsv = "console_topology_mode_smoke_runtime.csv",
    [string]$MetricsJson = "console_topology_mode_smoke_metrics.json",
    [switch]$Rebuild
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot
$lockHelpersPath = Join-Path -Path $repo -ChildPath "tools\runner_lock_hardening.ps1"
. $lockHelpersPath

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

function Resolve-OutputPath {
    param(
        [string]$OutputRoot,
        [string]$FileName
    )
    if ([System.IO.Path]::IsPathRooted($FileName)) {
        return $FileName
    }
    return (Join-Path -Path $OutputRoot -ChildPath $FileName)
}

function Ensure-C64Executable {
    param(
        [string]$RepoPath,
        [switch]$ForceRebuild
    )

    $exePath = Join-Path -Path $RepoPath -ChildPath "c64_11.exe"
    if ((-not $ForceRebuild) -and (Test-Path -LiteralPath $exePath)) {
        return $exePath
    }

    $gxx = "C:\msys64\ucrt64\bin\g++.exe"
    if (-not (Test-Path -LiteralPath $gxx)) {
        throw "Missing compiler: $gxx"
    }

    $code = Build-Profile-Retry -Macro "RUN_PROFILE_FAST" -OutFile "c64_11.exe" -CompilerPath $gxx -RepoPath $RepoPath -SourceFile "c64_11.cpp" -RetryCount 4
    if ($code -ne 0 -or -not (Test-Path -LiteralPath $exePath)) {
        throw "Failed to build c64_11.exe for console smoke"
    }
    return $exePath
}

function Invoke-ConsoleScript {
    param(
        [string]$ExePath,
        [string[]]$Commands,
        [int]$TimeoutMs = 30000
    )

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ExePath
    $psi.WorkingDirectory = $repo
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true

    $msysUcrt = "C:\msys64\ucrt64\bin"
    $msysUsr = "C:\msys64\usr\bin"
    $procPath = [Environment]::GetEnvironmentVariable("PATH", "Process")
    if ((Test-Path -LiteralPath $msysUcrt) -and (Test-Path -LiteralPath $msysUsr)) {
        $psi.EnvironmentVariables["PATH"] = "$msysUcrt;$msysUsr;$procPath"
    }

    $psi.EnvironmentVariables["JEMU_EMULATOR_CONSOLE"] = "1"
    $psi.EnvironmentVariables["IEC_MODEL_MODE"] = "physical-l6"
    $psi.EnvironmentVariables["IEC_PROFILE"] = "baseline_1541"
    $psi.EnvironmentVariables["IEC_TOPOLOGY_MODE"] = "force_detailed"

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi

    if (-not $proc.Start()) {
        throw "Failed to start emulator console process"
    }

    foreach ($cmd in $Commands) {
        $proc.StandardInput.WriteLine($cmd)
    }
    $proc.StandardInput.Close()

    if (-not $proc.WaitForExit($TimeoutMs)) {
        try { $proc.Kill() } catch {}
        throw "Console process timeout after $TimeoutMs ms"
    }

    $stdout = $proc.StandardOutput.ReadToEnd()
    $stderr = $proc.StandardError.ReadToEnd()
    $exitCode = $proc.ExitCode

    return [pscustomobject]@{
        stdout = $stdout
        stderr = $stderr
        exit_code = $exitCode
    }
}

$outputRoot = Resolve-LocalPath -PathInput $OutputDir
Ensure-Directory -Path $outputRoot

$reportPath = Resolve-OutputPath -OutputRoot $outputRoot -FileName $ReportCsv
$metricsPath = Resolve-OutputPath -OutputRoot $outputRoot -FileName $MetricsJson

$exe = Ensure-C64Executable -RepoPath $repo -ForceRebuild:$Rebuild

$commands = @(
    "IEC STATE",
    "IEC TOPOLOGY_MODE FORCE_SIMPLE",
    "IEC STATE",
    "IEC TOPOLOGY_MODE FORCE_DETAILED",
    "IEC STATE",
    "QUIT"
)

$start = [DateTime]::UtcNow
$result = Invoke-ConsoleScript -ExePath $exe -Commands $commands
$elapsed = [Math]::Round(([DateTime]::UtcNow - $start).TotalSeconds, 3)

$text = [string]::Concat($result.stdout, "`n", $result.stderr)

$checks = @(
    [pscustomobject]@{ name = "console_banner"; pass = ($text -match "\[CONSOLE\] Emulator control console enabled") },
    [pscustomobject]@{ name = "startup_options"; pass = ($text -match "IEC_OPTIONS .*model_mode=physical-l6.*profile=baseline_1541.*topology_override=FORCE_DETAILED") },
    [pscustomobject]@{ name = "force_simple_ack"; pass = ($text -match "OK: IEC TOPOLOGY_MODE FORCE_SIMPLE effective=SIMPLE") },
    [pscustomobject]@{ name = "force_detailed_ack"; pass = ($text -match "OK: IEC TOPOLOGY_MODE FORCE_DETAILED effective=DETAILED") },
    [pscustomobject]@{ name = "state_after_simple"; pass = ($text -match "IEC_OPTIONS .*topology_override=FORCE_SIMPLE.*topology_source=CONSOLE.*topology_effective=SIMPLE") },
    [pscustomobject]@{ name = "state_after_detailed"; pass = ($text -match "IEC_OPTIONS .*topology_override=FORCE_DETAILED.*topology_source=CONSOLE.*topology_effective=DETAILED") },
    [pscustomobject]@{ name = "clean_exit"; pass = ($result.exit_code -eq 0 -and $text -match "\[CONSOLE\] exit") }
)

$rows = @(
    [pscustomobject]@{
        generated_at_utc = [DateTime]::UtcNow.ToString("o")
        elapsed_s = $elapsed
        exit_code = [int]$result.exit_code
        checks_total = [int]$checks.Count
        checks_pass = [int](($checks | Where-Object { $_.pass }).Count)
    }
)
$rows | Export-Csv -LiteralPath $reportPath -NoTypeInformation -Encoding ASCII

$overallPass = (($checks | Where-Object { $_.pass }).Count -eq $checks.Count)
$failedChecks = @($checks | Where-Object { -not $_.pass } | ForEach-Object { $_.name })

$metrics = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    metrics = [ordered]@{
        checks_total = [int]$checks.Count
        checks_pass = [int](($checks | Where-Object { $_.pass }).Count)
        elapsed_s = $elapsed
        exit_code = [int]$result.exit_code
    }
    checks = @($checks | ForEach-Object {
        [ordered]@{
            name = $_.name
            pass = [bool]$_.pass
        }
    })
    failed_checks = $failedChecks
    overall_pass = $overallPass
}

($metrics | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $metricsPath -Encoding ASCII

"[CONSOLE-TOPOLOGY-SMOKE] PASS=$overallPass checks=$((($checks | Where-Object { $_.pass }).Count))/$($checks.Count) exit=$($result.exit_code) csv=$reportPath json=$metricsPath"
if (-not $overallPass) {
    "[CONSOLE-TOPOLOGY-SMOKE] failed_checks=$($failedChecks -join ',')"
    exit 1
}
exit 0
