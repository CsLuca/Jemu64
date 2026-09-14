param(
    [string]$Manifest = "external_tests_golden_corpus.json",
    [switch]$RebuildStrict
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot
$gxx = "C:\msys64\ucrt64\bin\g++.exe"
$lockHelpersPath = Join-Path -Path $repo -ChildPath "tools\runner_lock_hardening.ps1"
. $lockHelpersPath
$strictExe = Join-Path $repo "c64_11_strict_pla_ref.exe"

$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $env:PATH

if ($RebuildStrict -or -not (Test-Path -LiteralPath $strictExe)) {
    $buildCode = Build-Profile-Retry -Macro "RUN_PROFILE_STRICT" -OutFile ([System.IO.Path]::GetFileName($strictExe)) -CompilerPath $gxx -RepoPath $repo -SourceFile "c64_11.cpp" -RetryCount 4
    if ($buildCode -ne 0) {
        throw "Strict build failed"
    }
}

$savedManifest = [Environment]::GetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", "Process")
$savedGuard = [Environment]::GetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", "Process")
$savedPlaExport = [Environment]::GetEnvironmentVariable("VIC_EXPORT_PLA_SPEC", "Process")

try {
    [Environment]::SetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", $Manifest, "Process")
    [Environment]::SetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", "1", "Process")
    [Environment]::SetEnvironmentVariable("VIC_EXPORT_PLA_SPEC", "1", "Process")

    $run = Invoke-BinaryWithLockRetry -ExePath $strictExe -RetryCount 3
    if ([int]$run[1] -ne 0) {
        throw "Strict run failed while exporting PLA snapshot"
    }
}
finally {
    [Environment]::SetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", $savedManifest, "Process")
    [Environment]::SetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", $savedGuard, "Process")
    [Environment]::SetEnvironmentVariable("VIC_EXPORT_PLA_SPEC", $savedPlaExport, "Process")
}

$plaCsv = Join-Path $repo "reference\edge\vic_pla_spec.csv"
$plaSnapshot = Join-Path $repo "reference\edge\vic_pla_snapshot.md"

if (-not (Test-Path -LiteralPath $plaCsv)) {
    throw "Missing PLA spec CSV: $plaCsv"
}
if (-not (Test-Path -LiteralPath $plaSnapshot)) {
    throw "Missing PLA snapshot MD: $plaSnapshot"
}

"[PLA-REF] PASS: refreshed VIC PLA spec and snapshot."
