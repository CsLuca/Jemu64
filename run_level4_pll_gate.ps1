param(
    [ValidateSet("fast", "strict")]
    [string]$Profile = "fast",
    [string]$Manifest = "external_tests_manifest.json"
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot
$gxx = "C:\msys64\ucrt64\bin\g++.exe"
$lockHelpersPath = Join-Path -Path $repo -ChildPath "tools\runner_lock_hardening.ps1"
. $lockHelpersPath

function Resolve-LocalPath {
    param([string]$PathInput)
    if ([System.IO.Path]::IsPathRooted($PathInput)) {
        return $PathInput
    }
    return (Join-Path -Path $repo -ChildPath $PathInput)
}

$manifestPath = Resolve-LocalPath -PathInput $Manifest
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Missing manifest: $manifestPath"
}

$savedProfile = [Environment]::GetEnvironmentVariable("C64_DRIVE_PROFILE", "Process")
$savedManifest = [Environment]::GetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", "Process")
$savedGuard = [Environment]::GetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", "Process")
try {
    [Environment]::SetEnvironmentVariable("C64_DRIVE_PROFILE", "level4-accuracy", "Process")
    [Environment]::SetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", $manifestPath, "Process")
    [Environment]::SetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", "1", "Process")

    $exe = "c64_11_fast_signoff.exe"
    $macro = "RUN_PROFILE_FAST"
    if ($Profile -eq "strict") {
        $exe = "c64_11_strict_signoff.exe"
        $macro = "RUN_PROFILE_STRICT"
    }

    $code = Build-Profile-Retry -Macro $macro -OutFile $exe -CompilerPath $gxx -RepoPath $repo -SourceFile "c64_11.cpp" -RetryCount 4
    if ($code -ne 0) {
        throw "Build failed for $exe"
    }

    $run = Invoke-BinaryWithLockRetry -ExePath "$repo\$exe" -RetryCount 3
    $out = @($run[0])
    $exitCode = [int]$run[1]

    if ($exitCode -ne 0) {
        foreach ($line in $out) { $line }
        throw "Level4 PLL runtime failed (exit=$exitCode)"
    }

    $txt = ($out | Out-String)
    if ($txt -notmatch "\[1541 PLL\] PASS") {
        foreach ($line in $out) { $line }
        throw "Missing [1541 PLL] PASS marker"
    }
    if ($txt -notmatch "\[IEC COPY E2E\] PASS: copy_8_to_9_disk_e2e") {
        foreach ($line in $out) { $line }
        throw "Missing copier disk E2E marker under level4 profile"
    }

    "[L4-PLL] PASS: profile=$Profile markers=[1541 PLL] PASS + copier_e2e"
    exit 0
}
finally {
    [Environment]::SetEnvironmentVariable("C64_DRIVE_PROFILE", $savedProfile, "Process")
    [Environment]::SetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", $savedManifest, "Process")
    [Environment]::SetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", $savedGuard, "Process")
}
