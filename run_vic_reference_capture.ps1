param(
    [ValidateSet('full', 'strict')]
    [string]$Profile = 'full',
    [switch]$NoBuild,
    [switch]$Quiet
)

$msysUcrt = 'C:\msys64\ucrt64\bin'
$msysUsr = 'C:\msys64\usr\bin'
if ((Test-Path -LiteralPath $msysUcrt) -and (Test-Path -LiteralPath $msysUsr)) {
    $env:PATH = "$msysUcrt;$msysUsr;" + $env:PATH
}

$src = Join-Path $PSScriptRoot 'c64_11.cpp'
if (-not (Test-Path -LiteralPath $src)) {
    throw "Missing source: $src"
}

$runProfileMacro = if ($Profile -eq 'strict') { 'RUN_PROFILE_STRICT' } else { 'RUN_PROFILE_FULL' }
$exe = Join-Path $PSScriptRoot ("c64_11_vicref_{0}.exe" -f $Profile)

if (-not $NoBuild) {
    $gpp = 'C:\msys64\ucrt64\bin\g++.exe'
    if (-not (Test-Path -LiteralPath $gpp)) {
        throw "Missing compiler: $gpp"
    }
    & $gpp -std=c++17 -O2 ("-DRUN_PROFILE={0}" -f $runProfileMacro) $src -o $exe
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed (exit=$LASTEXITCODE)."
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

    $output = & $exe 2>&1 | ForEach-Object { "$_" }
    $exitCode = $LASTEXITCODE

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
