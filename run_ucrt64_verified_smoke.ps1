param(
    [string]$ExeTag = "",
    [switch]$KeepLogs,
    [switch]$NoPureGuard
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot
$gxx = "C:\msys64\ucrt64\bin\g++.exe"

# Procedure: resolve deterministic output file names for this run.
if ([string]::IsNullOrWhiteSpace($ExeTag)) {
    $ExeTag = [DateTime]::UtcNow.ToString("yyyyMMdd_HHmmss")
}
$outDir = Join-Path -Path $repo -ChildPath "datasets\level6\quality_reports\verified_ucrt64"
if (-not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

$exeName = "c64_11_verified_$ExeTag.exe"
$exePath = Join-Path -Path $outDir -ChildPath $exeName
$compileOut = Join-Path -Path $outDir -ChildPath ("compile_" + $ExeTag + ".out.txt")
$compileErr = Join-Path -Path $outDir -ChildPath ("compile_" + $ExeTag + ".err.txt")
$runOut = Join-Path -Path $outDir -ChildPath ("run_" + $ExeTag + ".out.txt")
$runErr = Join-Path -Path $outDir -ChildPath ("run_" + $ExeTag + ".err.txt")
$metaJson = Join-Path -Path $outDir -ChildPath ("meta_" + $ExeTag + ".json")

# Procedure: compile with MSYS2 UCRT64 and capture compiler streams.
if (-not (Test-Path -LiteralPath $gxx)) {
    throw "Missing UCRT64 compiler: $gxx"
}

$cmd = "`"$gxx`" -std=c++17 -O2 -pipe -DRUN_PROFILE=RUN_PROFILE_FAST -o `"$exePath`" `"$repo\c64_11.cpp`" 1>`"$compileOut`" 2>`"$compileErr`""
& "C:\Windows\System32\cmd.exe" /c $cmd | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Compile failed (exit=$LASTEXITCODE). See $compileErr"
}
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Compile reported success but missing exe: $exePath"
}

# Procedure: capture executable identity before run.
$hash = Get-FileHash -LiteralPath $exePath -Algorithm SHA256
$exeInfo = Get-Item -LiteralPath $exePath

$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $env:PATH
[Environment]::SetEnvironmentVariable("IEC_PROFILE", $null, "Process")
[Environment]::SetEnvironmentVariable("IEC_MODEL_MODE", $null, "Process")
if ($NoPureGuard) {
    [Environment]::SetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", $null, "Process")
} else {
    [Environment]::SetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", "1", "Process")
}

# Procedure: run the exact built executable and capture output files.
$runCmd = "`"$exePath`" 1>`"$runOut`" 2>`"$runErr`""
& "C:\Windows\System32\cmd.exe" /c $runCmd | Out-Null
$runExit = $LASTEXITCODE

$meta = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    exe_path = $exePath
    exe_size = [int64]$exeInfo.Length
    exe_last_write_utc = $exeInfo.LastWriteTimeUtc.ToString("o")
    exe_sha256 = $hash.Hash
    compile_stdout = $compileOut
    compile_stderr = $compileErr
    run_stdout = $runOut
    run_stderr = $runErr
    run_exit_code = [int]$runExit
}
($meta | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $metaJson -Encoding ASCII

"[UCRT64-VERIFIED] exe=$exePath sha256=$($hash.Hash) exit=$runExit meta=$metaJson"

if (-not $KeepLogs) {
    # Keep metadata + run logs; compile logs are optional cleanup.
    if (Test-Path -LiteralPath $compileOut) { Remove-Item -LiteralPath $compileOut -Force }
    if (Test-Path -LiteralPath $compileErr) { Remove-Item -LiteralPath $compileErr -Force }
}

exit $runExit
