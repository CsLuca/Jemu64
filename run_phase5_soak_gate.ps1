param(
    [string]$Manifest = "external_tests_manifest.json",
    [int]$Runs = 6,
    [double]$MaxFlakeRate = 0.05,
    [string]$ReportCsv = "phase5_soak_runtime.csv",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot
$lockHelpersPath = Join-Path -Path $repo -ChildPath "tools\runner_lock_hardening.ps1"
$artifactsHelpersPath = Join-Path -Path $repo -ChildPath "tools\artifacts.ps1"
. $lockHelpersPath
. $artifactsHelpersPath

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

$outputDirFull = ""
$runCtx = New-RunContext -RepoPath $repo -OutputDir $OutputDir
$outputDirFull = [string]$runCtx.OutputDir
if ($runCtx.Scoped) {
    if (-not [System.IO.Path]::IsPathRooted($ReportCsv)) {
        $reportFull = Join-Path -Path $outputDirFull -ChildPath $ReportCsv
    }
}

$rows = @()
$passRuns = 0
for ($i = 1; $i -le $Runs; ++$i) {
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = @()
        $okRetry = Invoke-WithRetry -RetryCount 2 -RetryDelayMs 400 -Action {
            $prefix = "copier_matrix_soak_run$($i)"
            $script:output = @(& "$repo\run_copier_matrix.ps1" -Profile fast -Manifest $manifestFull -OutputDir $outputDirFull -ReportPrefix $prefix 2>&1 | ForEach-Object { "$_" })
            if ($LASTEXITCODE -ne 0) {
                $global:LASTEXITCODE = $LASTEXITCODE
            }
        }
        $exitCode = if ($okRetry) { 0 } else { if ($LASTEXITCODE) { [int]$LASTEXITCODE } else { 1 } }
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
