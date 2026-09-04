param(
    [ValidateSet('pure', 'compat')]
    [string]$Mode = 'pure',
    [int]$MaxHalfCycles = 700000,
    [switch]$NoBulk,
    [switch]$UseTestOnlyPureCmdGuard,
    [switch]$EnableCompatClockAssist,
    [switch]$EnableCompatRamSinkInject,
    [switch]$EnableCompatRamSinkBulk,
    [switch]$EnableReplayCiaLog,
    [switch]$EnableDd00Trace,
    [switch]$EnableDriveAutoTalkDir,
    [switch]$EnableDriveAutoDirOnTalk0,
    [switch]$EnableDriveForceTalkOnDd0d8,
    [string]$IecPolarity,
    [switch]$Quiet
)

$msysUcrt = 'C:\msys64\ucrt64\bin'
$msysUsr = 'C:\msys64\usr\bin'
if ((Test-Path -LiteralPath $msysUcrt) -and (Test-Path -LiteralPath $msysUsr)) {
    $env:PATH = "$msysUcrt;$msysUsr;" + $env:PATH
}

$exe = Join-Path $PSScriptRoot 'c64_11.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Missing executable: $exe"
}

$saved = @{}
function Set-EnvVar([string]$Name, [string]$Value) {
    $saved[$Name] = [Environment]::GetEnvironmentVariable($Name, 'Process')
    [Environment]::SetEnvironmentVariable($Name, $Value, 'Process')
}

function Clear-EnvVar([string]$Name) {
    $saved[$Name] = [Environment]::GetEnvironmentVariable($Name, 'Process')
    [Environment]::SetEnvironmentVariable($Name, $null, 'Process')
}

$common = @{
    RUN_ONLY_KERNEL_IEC_E2E = '1'
    KERNAL_MAX_HALF_CYCLES = [string]$MaxHalfCycles
}

if ($EnableReplayCiaLog) { $common.KERNAL_REPLAY_CIA_LOG = '1' }
if ($EnableDd00Trace) { $common.KERNAL_DD00_TRACE = '1' }
if ($EnableDriveAutoTalkDir) { $common.KERNAL_DRIVE_AUTO_TALK_DIR = '1' }
if ($EnableDriveAutoDirOnTalk0) { $common.KERNAL_DRIVE_AUTO_DIR_ON_TALK0 = '1' }
if ($EnableDriveForceTalkOnDd0d8) { $common.KERNAL_DRIVE_FORCE_TALK_ON_DD0D8 = '1' }
if ($IecPolarity) { $common.KERNAL_IEC_POLARITY = $IecPolarity }

try {
    foreach ($k in $common.Keys) {
        Set-EnvVar -Name $k -Value $common[$k]
    }

    if ($Mode -eq 'compat') {
        Clear-EnvVar -Name 'KERNAL_TEST_ONLY_PURE_CMD_GUARD'
        if ($EnableCompatClockAssist) {
            Set-EnvVar -Name 'KERNAL_COMPAT_CLOCK_ASSIST' -Value '1'
        } else {
            Clear-EnvVar -Name 'KERNAL_COMPAT_CLOCK_ASSIST'
        }

        if ($EnableCompatRamSinkInject) {
            Set-EnvVar -Name 'KERNAL_COMPAT_RAM_SINK_INJECT' -Value '1'
        } else {
            Clear-EnvVar -Name 'KERNAL_COMPAT_RAM_SINK_INJECT'
        }

        if ($EnableCompatRamSinkBulk -and -not $NoBulk) {
            Set-EnvVar -Name 'KERNAL_COMPAT_RAM_SINK_BULK' -Value '1'
        } else {
            Clear-EnvVar -Name 'KERNAL_COMPAT_RAM_SINK_BULK'
        }
    } else {
        Clear-EnvVar -Name 'KERNAL_COMPAT_CLOCK_ASSIST'
        Clear-EnvVar -Name 'KERNAL_COMPAT_RAM_SINK_INJECT'
        Clear-EnvVar -Name 'KERNAL_COMPAT_RAM_SINK_BULK'
        if ($UseTestOnlyPureCmdGuard) {
            Set-EnvVar -Name 'KERNAL_TEST_ONLY_PURE_CMD_GUARD' -Value '1'
        } else {
            Clear-EnvVar -Name 'KERNAL_TEST_ONLY_PURE_CMD_GUARD'
        }
    }

    $output = & $exe 2>&1 | ForEach-Object { "$_" }
    $exitCode = $LASTEXITCODE

    if (-not $Quiet) {
        foreach ($line in $output) {
            $line
        }
    }

    $text = ($output | Out-String)
    $pass = ($text -match '\[KERNAL IEC E2E\] PASS')
    $tx = -1
    $rx = -1
    if ($text -match 'iec_tx=([0-9]+)') {
        $tx = [int]$matches[1]
    }
    if ($text -match 'iec_rx=([0-9]+)') {
        $rx = [int]$matches[1]
    }

    $bulkState = if ($Mode -eq 'compat' -and -not $NoBulk) { 'on' } elseif ($Mode -eq 'compat') { 'off' } else { 'n/a' }
    "[RUNNER] mode=$Mode bulk=$bulkState pass=$pass iec_tx=$tx iec_rx=$rx exit=$exitCode"

    if ($exitCode -ne 0) {
        exit $exitCode
    }
} finally {
    foreach ($k in $saved.Keys) {
        [Environment]::SetEnvironmentVariable($k, $saved[$k], 'Process')
    }
}
