param(
    [string]$Manifest = "external_tests_timing_gold.json"
)

$ErrorActionPreference = "Stop"

$bashExe = "C:\msys64\usr\bin\bash.exe"
$src = "/c/Users/LBiondi/Downloads/c4emu/c64_11.cpp"
$out = "/c/Users/LBiondi/Downloads/c4emu/c64_11_strict.exe"

& $bashExe -lc "export PATH=/ucrt64/bin:/usr/bin:`$PATH; g++ -std=c++17 -O2 -DRUN_PROFILE=RUN_PROFILE_STRICT '$src' -o '$out'"
if (-not $?) {
    throw "Build failed"
}

$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $env:PATH
$env:EXTERNAL_TEST_MANIFEST = $Manifest

& ".\c64_11_strict.exe"
