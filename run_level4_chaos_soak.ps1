param(
    [string]$ExternalManifest = "external_tests_manifest.json",
    [int]$Runs = 12,
    [double]$MaxFlakeRate = 0.05,
    [string]$ReportCsv = "level4_chaos_soak_runtime.csv",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot
$artifactsHelpersPath = Join-Path -Path $repo -ChildPath "tools\artifacts.ps1"
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

$externalManifestPath = Resolve-LocalPath -PathInput $ExternalManifest
if (-not (Test-Path -LiteralPath $externalManifestPath)) {
    throw "Missing external manifest: $externalManifestPath"
}

$runCtx = New-RunContext -RepoPath $repo -OutputDir $OutputDir
$outputDirFull = [string]$runCtx.OutputDir

$reportPath = Resolve-LocalPath -PathInput $ReportCsv
if ($runCtx.Scoped -and -not [System.IO.Path]::IsPathRooted($ReportCsv)) {
    $reportPath = Join-Path -Path $outputDirFull -ChildPath $ReportCsv
}

$savedProfile = [Environment]::GetEnvironmentVariable("C64_DRIVE_PROFILE", "Process")
try {
    [Environment]::SetEnvironmentVariable("C64_DRIVE_PROFILE", "level4-accuracy", "Process")

    $rows = @()
    $passRuns = 0
    for ($i = 1; $i -le $Runs; ++$i) {
        $copierOutDir = if ([string]::IsNullOrWhiteSpace($outputDirFull)) { "" } else { Join-Path -Path $outputDirFull -ChildPath ("chaos_run{0}" -f $i) }
        $copierPrefix = "l4_chaos_run$($i)"

        & "$repo\run_copier_matrix.ps1" -Profile fast -Manifest $externalManifestPath -OutputDir $copierOutDir -ReportPrefix $copierPrefix
        $exitCode = [int]$LASTEXITCODE
        $pass = ($exitCode -eq 0)
        if ($pass) {
            $passRuns++
        }

        $rows += [pscustomobject]@{
            run = $i
            exit_code = $exitCode
            pass = if ($pass) { 1 } else { 0 }
        }

        "[L4-CHAOS] run=$i pass=$pass exit=$exitCode"
    }

    $rows | Export-Csv -LiteralPath $reportPath -NoTypeInformation -Encoding ASCII

    $failRuns = $Runs - $passRuns
    $flakeRate = [double]$failRuns / [double]$Runs
    "[L4-CHAOS] summary pass=$passRuns/$Runs fail=$failRuns flake_rate=$('{0:N4}' -f $flakeRate) max_flake_rate=$MaxFlakeRate report=$reportPath"

    if ($flakeRate -gt $MaxFlakeRate) {
        exit 1
    }
    exit 0
}
finally {
    [Environment]::SetEnvironmentVariable("C64_DRIVE_PROFILE", $savedProfile, "Process")
}
