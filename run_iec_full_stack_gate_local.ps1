param(
    [ValidateSet("daily-fast", "nightly-strict")]
    [string]$OperationalProfile = "daily-fast",
    [string]$OutputRoot = ".local/quality_reports/full_stack_gate",
    [string]$CalibratedProfileOut = ".local/config/iec_profiles/calibrated_synthetic_1541.json",
    [string]$CalibratedProfileId = "calibrated_synthetic_1541_local"
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot

# Procedure: resolve local output paths under ignored .local/ workspace.
function Resolve-LocalPath {
    param([string]$PathInput)
    if ([System.IO.Path]::IsPathRooted($PathInput)) {
        return $PathInput
    }
    return (Join-Path -Path $repo -ChildPath $PathInput)
}

# Procedure: ensure output directories exist before invoking heavy gates.
function Ensure-Directory {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

$outRootPath = Resolve-LocalPath -PathInput $OutputRoot
$calibratedProfilePath = Resolve-LocalPath -PathInput $CalibratedProfileOut
$calProfileParent = Split-Path -Parent $calibratedProfilePath

Ensure-Directory -Path $outRootPath
if (-not [string]::IsNullOrWhiteSpace($calProfileParent)) {
    Ensure-Directory -Path $calProfileParent
}

& "$repo\run_iec_full_stack_gate.ps1" `
    -OperationalProfile $OperationalProfile `
    -OutputDir $outRootPath `
    -CalibratedProfileOut $calibratedProfilePath `
    -CalibratedProfileId $CalibratedProfileId

exit $LASTEXITCODE
