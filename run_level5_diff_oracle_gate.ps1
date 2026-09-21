param(
    [string]$Manifest = "datasets/level5/manifests/level5_official_testset_v1_manifest.json",
    [string]$ExternalManifest = "external_tests_manifest.json",
    [double]$SpreadBudget = 0.0,
    [string]$OutputDir = "datasets/level5/quality_reports"
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

$manifestPath = Resolve-LocalPath -PathInput $Manifest
$externalManifestPath = Resolve-LocalPath -PathInput $ExternalManifest
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Missing manifest: $manifestPath"
}
if (-not (Test-Path -LiteralPath $externalManifestPath)) {
    throw "Missing external manifest: $externalManifestPath"
}

$outputRoot = Resolve-LocalPath -PathInput $OutputDir
Ensure-Directory -Path $outputRoot

$runFastDir = Join-Path -Path $outputRoot -ChildPath "l5_diff_oracle_fast"
$runStrictDir = Join-Path -Path $outputRoot -ChildPath "l5_diff_oracle_strict"
$runFluxDir = Join-Path -Path $outputRoot -ChildPath "l5_diff_oracle_flux"
Ensure-Directory -Path $runFastDir
Ensure-Directory -Path $runStrictDir
Ensure-Directory -Path $runFluxDir

& "$repo\run_level5_cycle_coupling_gate.ps1" -Profile "fast" -Manifest $manifestPath -EnableKernelWarmup -KernelWarmupRepeat 1 -OutputDir $runFastDir
if ($LASTEXITCODE -ne 0) {
    throw "L5 cycle coupling fast failed"
}

& "$repo\run_level5_cycle_coupling_gate.ps1" -Profile "strict" -Manifest $manifestPath -EnableKernelWarmup -KernelWarmupRepeat 1 -OutputDir $runStrictDir
if ($LASTEXITCODE -ne 0) {
    throw "L5 cycle coupling strict failed"
}

& "$repo\run_level5_flux_analog_gate.ps1" -Profile "fast" -Manifest $manifestPath -ExternalManifest $externalManifestPath -OutputDir $runFluxDir
if ($LASTEXITCODE -ne 0) {
    throw "L5 flux analog fast failed"
}

$fastMetrics = Read-Json -Path (Join-Path -Path $runFastDir -ChildPath "level5_cycle_coupling_gate_metrics.json")
$strictMetrics = Read-Json -Path (Join-Path -Path $runStrictDir -ChildPath "level5_cycle_coupling_gate_metrics.json")
$fluxMetrics = Read-Json -Path (Join-Path -Path $runFluxDir -ChildPath "level5_flux_analog_gate_metrics.json")

$rates = @(
    [double]$fastMetrics.metrics.dataset_pass_rate,
    [double]$strictMetrics.metrics.dataset_pass_rate,
    [double]$fluxMetrics.metrics.dataset_pass_rate
)

$minRate = ($rates | Measure-Object -Minimum).Minimum
$maxRate = ($rates | Measure-Object -Maximum).Maximum
$spread = [Math]::Abs([double]$maxRate - [double]$minRate)
$overallPass = ($spread -le $SpreadBudget)

$reportJson = Join-Path -Path $outputRoot -ChildPath "level5_diff_oracle_gate_metrics.json"
$reportCsv = Join-Path -Path $outputRoot -ChildPath "level5_diff_oracle_gate_runtime.csv"

$report = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    manifest = $manifestPath
    external_manifest = $externalManifestPath
    metrics = [ordered]@{
        cycle_fast_dataset_pass_rate = [double]$fastMetrics.metrics.dataset_pass_rate
        cycle_strict_dataset_pass_rate = [double]$strictMetrics.metrics.dataset_pass_rate
        flux_fast_dataset_pass_rate = [double]$fluxMetrics.metrics.dataset_pass_rate
        differential_oracle_spread = $spread
    }
    budgets = [ordered]@{
        differential_oracle_spread_max = $SpreadBudget
    }
    overall_pass = $overallPass
}

($report | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $reportJson -Encoding ASCII

@(
    [pscustomobject]@{ metric = "cycle_fast_dataset_pass_rate"; value = [double]$fastMetrics.metrics.dataset_pass_rate; budget = 1.0; pass = [int]([double]$fastMetrics.metrics.dataset_pass_rate -ge 1.0) },
    [pscustomobject]@{ metric = "cycle_strict_dataset_pass_rate"; value = [double]$strictMetrics.metrics.dataset_pass_rate; budget = 1.0; pass = [int]([double]$strictMetrics.metrics.dataset_pass_rate -ge 1.0) },
    [pscustomobject]@{ metric = "flux_fast_dataset_pass_rate"; value = [double]$fluxMetrics.metrics.dataset_pass_rate; budget = 1.0; pass = [int]([double]$fluxMetrics.metrics.dataset_pass_rate -ge 1.0) },
    [pscustomobject]@{ metric = "differential_oracle_spread"; value = $spread; budget = $SpreadBudget; pass = [int]($spread -le $SpreadBudget) }
) | Export-Csv -LiteralPath $reportCsv -NoTypeInformation -Encoding ASCII

"[L5-DIFF] PASS=$overallPass spread=$('{0:N6}' -f $spread) budget=$('{0:N6}' -f $SpreadBudget)"
if (-not $overallPass) {
    exit 1
}
exit 0
