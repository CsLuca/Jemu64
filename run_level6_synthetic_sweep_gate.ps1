param(
    [string]$Level5Manifest = "datasets/level5/manifests/level5_official_testset_v1_manifest.json",
    [string]$OutputDir = ".local/quality_reports/l6_synthetic_sweep",
    [double]$MaxRuntimeMultiplier = 2.5
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot

# Procedure: resolve potentially relative path inputs against repository root.
function Resolve-LocalPath {
    param([string]$PathInput)
    if ([System.IO.Path]::IsPathRooted($PathInput)) {
        return $PathInput
    }
    return (Join-Path -Path $repo -ChildPath $PathInput)
}

# Procedure: ensure directory exists before writing output artifacts.
function Ensure-Directory {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

$manifestPath = Resolve-LocalPath -PathInput $Level5Manifest
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Missing level5 manifest: $manifestPath"
}

$outRoot = Resolve-LocalPath -PathInput $OutputDir
Ensure-Directory -Path $outRoot

$profiles = @(
    "baseline_1541",
    "baseline_1541c",
    "baseline_1541ii"
)

$savedProfile = [Environment]::GetEnvironmentVariable("IEC_PROFILE", "Process")
$savedMode = [Environment]::GetEnvironmentVariable("IEC_MODEL_MODE", "Process")
$rows = @()

try {
    [Environment]::SetEnvironmentVariable("IEC_MODEL_MODE", "physical-l6", "Process")
    foreach ($p in $profiles) {
        [Environment]::SetEnvironmentVariable("IEC_PROFILE", $p, "Process")
        $pOut = Join-Path -Path $outRoot -ChildPath ("synthetic_" + $p)
        Ensure-Directory -Path $pOut
        & "$repo\run_level6_physical_gate.ps1" -Manifest $manifestPath -OutputDir $pOut -MaxRuntimeMultiplier $MaxRuntimeMultiplier -SkipStrict
        $pass = ($LASTEXITCODE -eq 0)
        $rows += [pscustomobject]@{
            profile = $p
            pass = if ($pass) { 1 } else { 0 }
        }
        if (-not $pass) {
            break
        }
    }
}
finally {
    [Environment]::SetEnvironmentVariable("IEC_PROFILE", $savedProfile, "Process")
    [Environment]::SetEnvironmentVariable("IEC_MODEL_MODE", $savedMode, "Process")
}

$csv = Join-Path -Path $outRoot -ChildPath "level6_synthetic_sweep_runtime.csv"
$rows | Export-Csv -LiteralPath $csv -NoTypeInformation -Encoding ASCII

$allPass = (($rows | Measure-Object -Property pass -Sum).Sum -eq $rows.Count)
"[L6-SYNTH-SWEEP] PASS=$allPass profiles=$((($rows | Measure-Object -Property pass -Sum).Sum))/$($rows.Count)"
if (-not $allPass) { exit 1 }
exit 0
