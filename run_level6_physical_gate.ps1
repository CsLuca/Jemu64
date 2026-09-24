param(
    [string]$Manifest = "datasets/level5/manifests/level5_official_testset_v1_manifest.json",
    [string]$OutputDir = "datasets/level6/quality_reports",
    [double]$MaxRuntimeMultiplier = 2.0,
    [int]$WarmupCycles = 1,
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

function Read-Json {
    param([string]$Path)
    return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json)
}

function Run-CycleGateProfile {
    param(
        [string]$ProfileName,
        [string]$ManifestPath,
        [string]$OutDir
    )

    Ensure-Directory -Path $OutDir

    & "$repo\run_level5_cycle_coupling_gate.ps1" -Profile $ProfileName -Manifest $ManifestPath -EnableKernelWarmup -KernelWarmupRepeat $WarmupCycles -OutputDir $OutDir
    if ($LASTEXITCODE -ne 0) {
        throw "Level5 cycle coupling gate failed for profile=$ProfileName"
    }

    $metricsPath = Join-Path -Path $OutDir -ChildPath "level5_cycle_coupling_gate_metrics.json"
    if (-not (Test-Path -LiteralPath $metricsPath)) {
        throw "Missing metrics json: $metricsPath"
    }
    return (Read-Json -Path $metricsPath)
}

$manifestPath = Resolve-LocalPath -PathInput $Manifest
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Missing manifest: $manifestPath"
}

$outputRoot = Resolve-LocalPath -PathInput $OutputDir
Ensure-Directory -Path $outputRoot

$savedMode = [Environment]::GetEnvironmentVariable("IEC_MODEL_MODE", "Process")
$savedProfile = [Environment]::GetEnvironmentVariable("IEC_PROFILE", "Process")

try {
    [Environment]::SetEnvironmentVariable("IEC_MODEL_MODE", "physical-l6", "Process")
    [Environment]::SetEnvironmentVariable("IEC_PROFILE", "baseline_1541", "Process")

    $fastDir = Join-Path -Path $outputRoot -ChildPath "l6_cycle_fast"
    $fast = Run-CycleGateProfile -ProfileName "fast" -ManifestPath $manifestPath -OutDir $fastDir

    $strict = $null
    if (-not $SkipStrict) {
        $strictDir = Join-Path -Path $outputRoot -ChildPath "l6_cycle_strict"
        $strict = Run-CycleGateProfile -ProfileName "strict" -ManifestPath $manifestPath -OutDir $strictDir
    }

    $fastRepeat = [double]$fast.metrics.kernel_repeat
    $fastMaxHalf = [double]$fast.metrics.kernel_max_halfcycles
    $fastWork = $fastRepeat * $fastMaxHalf

    $strictWorkRatio = 1.0
    if ($null -ne $strict) {
        $strictRepeat = [double]$strict.metrics.kernel_repeat
        $strictMaxHalf = [double]$strict.metrics.kernel_max_halfcycles
        $strictWork = $strictRepeat * $strictMaxHalf
        if ($fastWork -gt 0) {
            $strictWorkRatio = $strictWork / $fastWork
        }
    }

    $overallPass = ([double]$fast.metrics.dataset_pass_rate -ge 1.0) -and ($strictWorkRatio -le $MaxRuntimeMultiplier)

    $metricsPath = Join-Path -Path $outputRoot -ChildPath "level6_physical_gate_metrics.json"
    $runtimeCsv = Join-Path -Path $outputRoot -ChildPath "level6_physical_gate_runtime.csv"

    $metrics = [ordered]@{
        generated_at_utc = [DateTime]::UtcNow.ToString("o")
        manifest = $manifestPath
        iec_model_mode = "physical-l6"
        iec_profile = "baseline_1541"
        metrics = [ordered]@{
            cycle_fast_dataset_pass_rate = [double]$fast.metrics.dataset_pass_rate
            cycle_fast_kernel_repeat = [int]$fast.metrics.kernel_repeat
            cycle_fast_kernel_max_halfcycles = [int]$fast.metrics.kernel_max_halfcycles
            cycle_strict_runtime_work_ratio_vs_fast = [double]$strictWorkRatio
        }
        budgets = [ordered]@{
            cycle_fast_dataset_min_pass_rate = 1.0
            max_runtime_multiplier_vs_fast = $MaxRuntimeMultiplier
        }
        overall_pass = $overallPass
    }

    ($metrics | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $metricsPath -Encoding ASCII

    @(
        [pscustomobject]@{ metric = "cycle_fast_dataset_pass_rate"; value = [double]$fast.metrics.dataset_pass_rate; budget = 1.0; pass = [int]([double]$fast.metrics.dataset_pass_rate -ge 1.0) },
        [pscustomobject]@{ metric = "runtime_multiplier_vs_fast"; value = [double]$strictWorkRatio; budget = $MaxRuntimeMultiplier; pass = [int]($strictWorkRatio -le $MaxRuntimeMultiplier) }
    ) | Export-Csv -LiteralPath $runtimeCsv -NoTypeInformation -Encoding ASCII

    "[L6-PHYSICAL] PASS=$overallPass mode=physical-l6 profile=baseline_1541 runtime_ratio=$('{0:N3}' -f $strictWorkRatio) max_ratio=$MaxRuntimeMultiplier"
    if (-not $overallPass) {
        exit 1
    }
    exit 0
}
finally {
    [Environment]::SetEnvironmentVariable("IEC_MODEL_MODE", $savedMode, "Process")
    [Environment]::SetEnvironmentVariable("IEC_PROFILE", $savedProfile, "Process")
}
