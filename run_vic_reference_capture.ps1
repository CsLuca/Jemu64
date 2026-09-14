param(
    [ValidateSet('full', 'strict')]
    [string]$Profile = 'full',
    [switch]$NoBuild,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

$repo = $PSScriptRoot
$lockHelpersPath = Join-Path -Path $repo -ChildPath 'tools\runner_lock_hardening.ps1'
. $lockHelpersPath

$msysUcrt = 'C:\msys64\ucrt64\bin'
$msysUsr = 'C:\msys64\usr\bin'
if ((Test-Path -LiteralPath $msysUcrt) -and (Test-Path -LiteralPath $msysUsr)) {
    $env:PATH = "$msysUcrt;$msysUsr;" + $env:PATH
}

$src = Join-Path $repo 'c64_11.cpp'
if (-not (Test-Path -LiteralPath $src)) {
    throw "Missing source: $src"
}

$runProfileMacro = if ($Profile -eq 'strict') { 'RUN_PROFILE_STRICT' } else { 'RUN_PROFILE_FULL' }
$exe = Join-Path $repo ("c64_11_vicref_{0}.exe" -f $Profile)

if (-not $NoBuild) {
    $gpp = 'C:\msys64\ucrt64\bin\g++.exe'
    if (-not (Test-Path -LiteralPath $gpp)) {
        throw "Missing compiler: $gpp"
    }
    $buildCode = Build-Profile-Retry -Macro $runProfileMacro -OutFile ([System.IO.Path]::GetFileName($exe)) -CompilerPath $gpp -RepoPath $repo -SourceFile 'c64_11.cpp' -RetryCount 4
    if ($buildCode -ne 0) {
        throw "Build failed (exit=$buildCode)."
    }
}

if (-not (Test-Path -LiteralPath $exe)) {
    throw "Missing executable: $exe"
}

$saved = @{}
function Set-EnvVar([string]$Name, [string]$Value) {
    $saved[$Name] = [Environment]::GetEnvironmentVariable($Name, 'Process')
    [Environment]::SetEnvironmentVariable($Name, $Value, 'Process')
}

try {
    # Keep run conditions aligned with current strict/full gates.
    Set-EnvVar -Name 'EXTERNAL_TEST_MANIFEST' -Value 'external_tests_timing_gold.json'
    Set-EnvVar -Name 'KERNAL_TEST_ONLY_PURE_CMD_GUARD' -Value '1'

    $run = Invoke-BinaryWithLockRetry -ExePath $exe -RetryCount 3
    $output = @($run[0])
    $exitCode = [int]$run[1]

    if (-not $Quiet) {
        foreach ($line in $output) {
            $line
        }
    }

    $text = ($output | Out-String)
    $m = [regex]::Match($text, 'Frame hash/event digest: PASS\s+frame=\$([0-9A-Fa-f]+)\s+events=\$([0-9A-Fa-f]+)')
    if (-not $m.Success) {
        throw 'Could not extract VIC reference frame hash/event digest from output.'
    }

    $frameHash = $m.Groups[1].Value.ToUpperInvariant()
    $eventDigest = $m.Groups[2].Value.ToUpperInvariant()

    "[VIC REF] profile=$Profile frame_hash=$frameHash event_digest=$eventDigest exit=$exitCode"
    "[VIC REF] PowerShell: `$env:VIC_REF_FRAME_HASH='$frameHash'; `$env:VIC_REF_EVENT_DIGEST='$eventDigest'"
    "[VIC REF] Bash: export VIC_REF_FRAME_HASH=$frameHash VIC_REF_EVENT_DIGEST=$eventDigest"

    if ($exitCode -ne 0) {
        exit $exitCode
    }
}
finally {
    foreach ($k in $saved.Keys) {
        [Environment]::SetEnvironmentVariable($k, $saved[$k], 'Process')
    }
}
