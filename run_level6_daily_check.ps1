param(
    [string]$Level5Manifest = "datasets/level5/manifests/level5_official_testset_v1_manifest.json",
    [string]$SyntheticManifest = "datasets/level6/manifests/level6_synthetic_profiles_manifest.json",
    [string]$OutputDir = "datasets/level6/quality_reports/daily_check",
    [double]$MaxRuntimeMultiplier = 2.0,
    [switch]$SkipStrict
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

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Action
    )
    $start = [DateTime]::UtcNow
    try {
        $stepOutput = @(& $Action 2>&1)
        foreach ($line in $stepOutput) {
            "$line"
        }
        $exitCode = $LASTEXITCODE
        $pass = ($exitCode -eq 0)
        if (-not $pass) {
            return [pscustomobject]@{ step = $Name; pass = 0; exit_code = $exitCode; issue = "exit=$exitCode"; elapsed_s = [Math]::Round(([DateTime]::UtcNow - $start).TotalSeconds, 3) }
        }
        return [pscustomobject]@{ step = $Name; pass = 1; exit_code = 0; issue = "ok"; elapsed_s = [Math]::Round(([DateTime]::UtcNow - $start).TotalSeconds, 3) }
    }
    catch {
        return [pscustomobject]@{ step = $Name; pass = 0; exit_code = 1; issue = $_.Exception.Message; elapsed_s = [Math]::Round(([DateTime]::UtcNow - $start).TotalSeconds, 3) }
    }
}

$level5ManifestPath = Resolve-LocalPath -PathInput $Level5Manifest
$syntheticManifestPath = Resolve-LocalPath -PathInput $SyntheticManifest
if (-not (Test-Path -LiteralPath $level5ManifestPath)) {
    throw "Missing Level5 manifest: $level5ManifestPath"
}
if (-not (Test-Path -LiteralPath $syntheticManifestPath)) {
    throw "Missing synthetic manifest: $syntheticManifestPath"
}

$outputRoot = Resolve-LocalPath -PathInput $OutputDir
Ensure-Directory -Path $outputRoot

$rows = @()

$l5Out = Join-Path -Path $outputRoot -ChildPath "l5_fast"
Ensure-Directory -Path $l5Out
$rows += Invoke-Step -Name "l5_cycle_fast" -Action {
    & "$repo\run_level5_cycle_coupling_gate.ps1" -Profile fast -Manifest $level5ManifestPath -OutputDir $l5Out
}

$l6PhysicalOut = Join-Path -Path $outputRoot -ChildPath "l6_physical"
Ensure-Directory -Path $l6PhysicalOut
$rows += Invoke-Step -Name "l6_physical_gate" -Action {
    if ($SkipStrict) {
        & "$repo\run_level6_physical_gate.ps1" -Manifest $level5ManifestPath -OutputDir $l6PhysicalOut -MaxRuntimeMultiplier $MaxRuntimeMultiplier -SkipStrict
    } else {
        & "$repo\run_level6_physical_gate.ps1" -Manifest $level5ManifestPath -OutputDir $l6PhysicalOut -MaxRuntimeMultiplier $MaxRuntimeMultiplier
    }
}

$l6SynthOut = Join-Path -Path $outputRoot -ChildPath "l6_synthetic"
Ensure-Directory -Path $l6SynthOut
$rows += Invoke-Step -Name "l6_synthetic_envelope_gate" -Action {
    if ($SkipStrict) {
        & "$repo\run_level6_synthetic_envelope_gate.ps1" -Manifest $syntheticManifestPath -Level5Manifest $level5ManifestPath -OutputDir $l6SynthOut -MaxRuntimeMultiplier $MaxRuntimeMultiplier -SkipStrict
    } else {
        & "$repo\run_level6_synthetic_envelope_gate.ps1" -Manifest $syntheticManifestPath -Level5Manifest $level5ManifestPath -OutputDir $l6SynthOut -MaxRuntimeMultiplier $MaxRuntimeMultiplier
    }
}

$csvPath = Join-Path -Path $outputRoot -ChildPath "level6_daily_check_runtime.csv"
$jsonPath = Join-Path -Path $outputRoot -ChildPath "level6_daily_check_metrics.json"

$rows = @($rows | Where-Object { $_ -is [psobject] -and ($_.PSObject.Properties.Name -contains "step") })

$rows | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding ASCII

$allPass = (($rows | Measure-Object -Property pass -Sum).Sum -eq $rows.Count)
$metrics = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    level5_manifest = $level5ManifestPath
    synthetic_manifest = $syntheticManifestPath
    metrics = [ordered]@{
        steps_total = [int]$rows.Count
        steps_pass = [int](($rows | Measure-Object -Property pass -Sum).Sum)
        max_runtime_multiplier = $MaxRuntimeMultiplier
    }
    overall_pass = $allPass
}

($metrics | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $jsonPath -Encoding ASCII

"[L6-DAILY] PASS=$allPass steps=$((($rows | Measure-Object -Property pass -Sum).Sum))/$($rows.Count) budget=$MaxRuntimeMultiplier"
if (-not $allPass) {
    exit 1
}
exit 0
