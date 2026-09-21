param(
    [string]$OutputDir = "datasets/level5/quality_reports",
    [string]$ReportCsv = "level5_power_matrix_gate_runtime.csv",
    [string]$MetricsJson = "level5_power_matrix_gate_metrics.json"
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot

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
    param([string]$RepoPath)
    $exePath = Join-Path -Path $RepoPath -ChildPath "c64_11.exe"
    if (Test-Path -LiteralPath $exePath) {
        return $exePath
    }

    $msys = "C:\msys64\msys2_shell.cmd"
    if (-not (Test-Path -LiteralPath $msys)) {
        throw "Missing MSYS shell: $msys"
    }

    & $msys -ucrt64 -defterm -no-start -here -c "g++ -std=c++17 -O2 c64_11.cpp -o c64_11.exe"
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $exePath)) {
        throw "Failed to build c64_11.exe"
    }

    return $exePath
}

$outputRoot = Resolve-LocalPath -PathInput $OutputDir
Ensure-Directory -Path $outputRoot

$reportPath = Resolve-OutputPath -OutputRoot $outputRoot -FileName $ReportCsv
$metricsPath = Resolve-OutputPath -OutputRoot $outputRoot -FileName $MetricsJson

$exe = Ensure-C64Executable -RepoPath $repo

$msysUcrt = 'C:\msys64\ucrt64\bin'
$msysUsr = 'C:\msys64\usr\bin'
if ((Test-Path -LiteralPath $msysUcrt) -and (Test-Path -LiteralPath $msysUsr)) {
    $env:PATH = "$msysUcrt;$msysUsr;" + $env:PATH
}

$savedFlag = [Environment]::GetEnvironmentVariable("RUN_ONLY_1541_POWER_MATRIX_GATE", "Process")
try {
    [Environment]::SetEnvironmentVariable("RUN_ONLY_1541_POWER_MATRIX_GATE", "1", "Process")
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = @(& $exe 2>&1 | ForEach-Object { "$_" })
        $exeExit = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedEap
    }
}
finally {
    [Environment]::SetEnvironmentVariable("RUN_ONLY_1541_POWER_MATRIX_GATE", $savedFlag, "Process")
}
$text = ($output | Out-String)

if ($exeExit -ne 0) {
    throw "Power matrix gate executable failed (exit=$exeExit)"
}

$cases = @(
    "c64_off_drive_off",
    "c64_off_drive_on",
    "c64_on_drive_off",
    "c64_on_drive_on"
)

$rows = @()
$allPass = $true

foreach ($caseName in $cases) {
    $pattern = "\[1541 PM-GATE\] case=$caseName.*pass=(True|False)"
    $match = [regex]::Match($text, $pattern)
    if (-not $match.Success) {
        $rows += [pscustomobject]@{ case = $caseName; observed = 0; pass = 0 }
        $allPass = $false
        continue
    }

    $passCase = ($match.Groups[1].Value -eq "True")
    $rows += [pscustomobject]@{ case = $caseName; observed = 1; pass = if ($passCase) { 1 } else { 0 } }
    if (-not $passCase) {
        $allPass = $false
    }
}

$summaryPassMatch = [regex]::Match($text, "\[1541 PM-GATE\] PASS: all power matrix scenarios matched")
if (-not $summaryPassMatch.Success) {
    $allPass = $false
}

$rows | Export-Csv -LiteralPath $reportPath -NoTypeInformation -Encoding ASCII

$metrics = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    metrics = [ordered]@{
        scenarios_total = $cases.Count
        scenarios_observed = [int](($rows | Measure-Object -Property observed -Sum).Sum)
        scenarios_pass = [int](($rows | Measure-Object -Property pass -Sum).Sum)
    }
    budgets = [ordered]@{
        scenarios_must_pass = $cases.Count
    }
    overall_pass = $allPass
}

($metrics | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $metricsPath -Encoding ASCII

"[L5-PMATRIX] PASS=$allPass scenarios=$((($rows | Measure-Object -Property pass -Sum).Sum))/$($cases.Count)"
if (-not $allPass) {
    exit 1
}
exit 0
