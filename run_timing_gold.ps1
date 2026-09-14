param(
    [string]$Manifest = "external_tests_timing_gold.json"
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot
$gxx = "C:\msys64\ucrt64\bin\g++.exe"
$lockHelpersPath = Join-Path -Path $repo -ChildPath "tools\runner_lock_hardening.ps1"
. $lockHelpersPath

$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $env:PATH

$buildCode = Build-Profile-Retry -Macro "RUN_PROFILE_STRICT" -OutFile "c64_11_strict.exe" -CompilerPath $gxx -RepoPath $repo -SourceFile "c64_11.cpp" -RetryCount 4
if ($buildCode -ne 0) {
    throw "Build failed"
}

$env:EXTERNAL_TEST_MANIFEST = $Manifest

$run = Invoke-BinaryWithLockRetry -ExePath (Join-Path $repo "c64_11_strict.exe") -RetryCount 3
if ([int]$run[1] -ne 0) {
    exit ([int]$run[1])
}
