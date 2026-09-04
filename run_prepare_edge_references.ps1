param(
    [string]$Manifest = "external_tests_golden_corpus.json",
    [switch]$RebuildStrict
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot
$gxx = "C:\msys64\ucrt64\bin\g++.exe"
$py = "python"
$pcTool = Join-Path $repo "tools\make_pc_only_reference.py"
$strictExe = Join-Path $repo "c64_11_strict_edge_ref.exe"
$savedPath = $env:PATH

try {
    $env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $env:PATH

if (-not (Test-Path -LiteralPath $pcTool)) {
    throw "Missing tool: $pcTool"
}

    if ($RebuildStrict -or -not (Test-Path -LiteralPath $strictExe)) {
        & $gxx -std=c++17 -O2 "-DRUN_PROFILE=RUN_PROFILE_STRICT" (Join-Path $repo "c64_11.cpp") -o $strictExe
        if ($LASTEXITCODE -ne 0) {
            throw "Strict build failed"
        }
    }

$savedManifest = [Environment]::GetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", "Process")
$savedGuard = [Environment]::GetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", "Process")
$savedWeek15 = [Environment]::GetEnvironmentVariable("WEEK15_BOOTSTRAP_BAAEC_REF", "Process")
$savedWeek16 = [Environment]::GetEnvironmentVariable("WEEK16_BOOTSTRAP_CROSS_REF", "Process")
$savedWeek18 = [Environment]::GetEnvironmentVariable("WEEK18_BOOTSTRAP_OPENBUS_REF", "Process")
$savedWeek19 = [Environment]::GetEnvironmentVariable("WEEK19_BOOTSTRAP_CIA_REF", "Process")
$savedWeek20 = [Environment]::GetEnvironmentVariable("WEEK20_BOOTSTRAP_VIC_REF", "Process")
$savedWeek21 = [Environment]::GetEnvironmentVariable("WEEK21_BOOTSTRAP_BUS_REF", "Process")
$savedWeek22 = [Environment]::GetEnvironmentVariable("WEEK22_BOOTSTRAP_PORTMAP_REF", "Process")
$savedWeek23 = [Environment]::GetEnvironmentVariable("WEEK23_BOOTSTRAP_IRQNMI_REF", "Process")
$savedWeek24 = [Environment]::GetEnvironmentVariable("WEEK24_BOOTSTRAP_IRQLATCH_REF", "Process")
$savedWeek25 = [Environment]::GetEnvironmentVariable("WEEK25_BOOTSTRAP_CIASERIAL_REF", "Process")
$savedWeek26 = [Environment]::GetEnvironmentVariable("WEEK26_BOOTSTRAP_DRIVEIEC_REF", "Process")
$savedWeek27 = [Environment]::GetEnvironmentVariable("WEEK27_BOOTSTRAP_DRIVECMD_REF", "Process")

try {
    [Environment]::SetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", $Manifest, "Process")
    [Environment]::SetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK15_BOOTSTRAP_BAAEC_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK16_BOOTSTRAP_CROSS_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK18_BOOTSTRAP_OPENBUS_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK19_BOOTSTRAP_CIA_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK20_BOOTSTRAP_VIC_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK21_BOOTSTRAP_BUS_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK22_BOOTSTRAP_PORTMAP_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK23_BOOTSTRAP_IRQNMI_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK24_BOOTSTRAP_IRQLATCH_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK25_BOOTSTRAP_CIASERIAL_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK26_BOOTSTRAP_DRIVEIEC_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK27_BOOTSTRAP_DRIVECMD_REF", "1", "Process")

    & $strictExe
    if ($LASTEXITCODE -ne 0) {
        throw "Strict run failed during edge reference bootstrap"
    }
}
finally {
    [Environment]::SetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", $savedManifest, "Process")
    [Environment]::SetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", $savedGuard, "Process")
    [Environment]::SetEnvironmentVariable("WEEK15_BOOTSTRAP_BAAEC_REF", $savedWeek15, "Process")
    [Environment]::SetEnvironmentVariable("WEEK16_BOOTSTRAP_CROSS_REF", $savedWeek16, "Process")
    [Environment]::SetEnvironmentVariable("WEEK18_BOOTSTRAP_OPENBUS_REF", $savedWeek18, "Process")
    [Environment]::SetEnvironmentVariable("WEEK19_BOOTSTRAP_CIA_REF", $savedWeek19, "Process")
    [Environment]::SetEnvironmentVariable("WEEK20_BOOTSTRAP_VIC_REF", $savedWeek20, "Process")
    [Environment]::SetEnvironmentVariable("WEEK21_BOOTSTRAP_BUS_REF", $savedWeek21, "Process")
    [Environment]::SetEnvironmentVariable("WEEK22_BOOTSTRAP_PORTMAP_REF", $savedWeek22, "Process")
    [Environment]::SetEnvironmentVariable("WEEK23_BOOTSTRAP_IRQNMI_REF", $savedWeek23, "Process")
    [Environment]::SetEnvironmentVariable("WEEK24_BOOTSTRAP_IRQLATCH_REF", $savedWeek24, "Process")
    [Environment]::SetEnvironmentVariable("WEEK25_BOOTSTRAP_CIASERIAL_REF", $savedWeek25, "Process")
    [Environment]::SetEnvironmentVariable("WEEK26_BOOTSTRAP_DRIVEIEC_REF", $savedWeek26, "Process")
    [Environment]::SetEnvironmentVariable("WEEK27_BOOTSTRAP_DRIVECMD_REF", $savedWeek27, "Process")
}

$week15Runtime = Join-Path $repo "week15_baaec_handoff_runtime.csv"
$week16Runtime = Join-Path $repo "week16_cross_domain_runtime.csv"
$week18Runtime = Join-Path $repo "week18_openbus_revision_runtime.csv"
$week19Runtime = Join-Path $repo "week19_cia_dense_runtime.csv"
$week20Runtime = Join-Path $repo "week20_vic_pathological_runtime.csv"
$week21Runtime = Join-Path $repo "week21_bus_corner_runtime.csv"
$week22Runtime = Join-Path $repo "week22_port_map_runtime.csv"
$week23Runtime = Join-Path $repo "week23_cia_irq_nmi_runtime.csv"
$week24Runtime = Join-Path $repo "week24_irq_latch_runtime.csv"
$week25Runtime = Join-Path $repo "week25_cia_serial_runtime.csv"
$week26Runtime = Join-Path $repo "week26_drive_iec_runtime.csv"
$week27Runtime = Join-Path $repo "week27_drive_cmdphase_runtime.csv"
$brknRuntime = Join-Path $repo "c64_lorenz_brkn_edge_ref.trace.csv"

if (-not (Test-Path -LiteralPath $week15Runtime)) {
    throw "Missing runtime edge trace: $week15Runtime"
}
if (-not (Test-Path -LiteralPath $week16Runtime)) {
    throw "Missing runtime edge trace: $week16Runtime"
}
if (-not (Test-Path -LiteralPath $week18Runtime)) {
    throw "Missing runtime edge trace: $week18Runtime"
}
if (-not (Test-Path -LiteralPath $week19Runtime)) {
    throw "Missing runtime edge trace: $week19Runtime"
}
if (-not (Test-Path -LiteralPath $week20Runtime)) {
    throw "Missing runtime edge trace: $week20Runtime"
}
if (-not (Test-Path -LiteralPath $week21Runtime)) {
    throw "Missing runtime edge trace: $week21Runtime"
}
if (-not (Test-Path -LiteralPath $week22Runtime)) {
    throw "Missing runtime edge trace: $week22Runtime"
}
if (-not (Test-Path -LiteralPath $week23Runtime)) {
    throw "Missing runtime edge trace: $week23Runtime"
}
if (-not (Test-Path -LiteralPath $week24Runtime)) {
    throw "Missing runtime edge trace: $week24Runtime"
}
if (-not (Test-Path -LiteralPath $week25Runtime)) {
    throw "Missing runtime edge trace: $week25Runtime"
}
if (-not (Test-Path -LiteralPath $week26Runtime)) {
    throw "Missing runtime edge trace: $week26Runtime"
}
if (-not (Test-Path -LiteralPath $week27Runtime)) {
    throw "Missing runtime edge trace: $week27Runtime"
}
if (-not (Test-Path -LiteralPath $brknRuntime)) {
    throw "Missing runtime trace: $brknRuntime"
}

$brknRef = Join-Path $repo "reference\vice\c64_lorenz_brkn_edge_ref.trace.csv"
& $py $pcTool --source $brknRuntime --dest $brknRef
if ($LASTEXITCODE -ne 0) {
    throw "Failed building pc_only reference for c64_lorenz_brkn_edge_ref"
}

"[EDGE-REF] PASS: refreshed week15/week16/week18/week19/week20/week21/week22/week23/week24/week25/week26/week27 edge references and c64_lorenz_brkn_edge_ref pc_only reference."
}
finally {
    $env:PATH = $savedPath
}
