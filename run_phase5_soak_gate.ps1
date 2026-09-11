param(
    [string]$Manifest = "external_tests_manifest.json",
    [int]$Runs = 6,
    [double]$MaxFlakeRate = 0.05,
    [string]$ReportCsv = "phase5_soak_runtime.csv"
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

if ($Runs -lt 1) {
    throw "Runs must be >= 1"
}

$manifestFull = Resolve-LocalPath -PathInput $Manifest
if (-not (Test-Path -LiteralPath $manifestFull)) {
    throw "Missing manifest: $manifestFull"
}

$reportFull = Resolve-LocalPath -PathInput $ReportCsv

$rows = @()
$passRuns = 0
for ($i = 1; $i -le $Runs; ++$i) {
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = @(& "$repo\run_copier_matrix.ps1" -Profile fast -Manifest $manifestFull 2>&1 | ForEach-Object { "$_" })
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedEap
    }

    $ok = ($exitCode -eq 0)
    if ($ok) { $passRuns++ }
    $rows += [pscustomobject]@{
        run = $i
        exit_code = $exitCode
        pass = if ($ok) { 1 } else { 0 }
    }
    "[PHASE5-SOAK] run=$i pass=$ok exit=$exitCode"
}

$rows | Export-Csv -LiteralPath $reportFull -NoTypeInformation -Encoding ASCII

$failRuns = $Runs - $passRuns
$flakeRate = [double]$failRuns / [double]$Runs
"[PHASE5-SOAK] summary pass=$passRuns/$Runs fail=$failRuns flake_rate=$('{0:N4}' -f $flakeRate) max_flake_rate=$MaxFlakeRate report=$reportFull"

if ($flakeRate -gt $MaxFlakeRate) {
    exit 1
}
exit 0
