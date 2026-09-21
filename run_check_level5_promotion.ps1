param(
    [string]$Manifest = "datasets/level5/manifests/level5_official_testset_v1_manifest.json",
    [string]$ExternalManifest = "external_tests_manifest.json",
    [double]$SpreadBudget = 0.0,
    [int]$ChaosRuns = 6,
    [double]$MaxFlakeRate = 0.05,
    [bool]$RequirePowerMatrixGateInChaos = $true,
    [string]$OutputDir = "datasets/level5/quality_reports",
    [string]$ReportCsv = "level5_promotion_gate_runtime.csv",
    [string]$MetricsJson = "level5_promotion_gate_metrics.json"
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

function Invoke-GateStep {
    param(
        [string]$Name,
        [string]$ScriptPath,
        [hashtable]$NamedArgs
    )

    $issue = "ok"
    $pass = $false
    $exitCode = 0
    try {
        $gateOutput = @(& $ScriptPath @NamedArgs 2>&1 | ForEach-Object { "$_" })
        $exitCode = $LASTEXITCODE
        $pass = ($exitCode -eq 0)
        if (-not $pass) {
            $issue = "exit=$exitCode"
        }
    }
    catch {
        $pass = $false
        $exitCode = 1
        $issue = $_.Exception.Message
    }

    return [pscustomobject]@{
        gate = $Name
        pass = if ($pass) { 1 } else { 0 }
        exit_code = $exitCode
        issue = $issue
    }
}

if ($ChaosRuns -lt 1) {
    throw "ChaosRuns must be >= 1"
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

$reportPath = Resolve-OutputPath -OutputRoot $outputRoot -FileName $ReportCsv
$metricsPath = Resolve-OutputPath -OutputRoot $outputRoot -FileName $MetricsJson

$rows = @()
$firstFailedGate = ""

$pmResult = Invoke-GateStep -Name "level5_power_matrix_gate" -ScriptPath "$repo\run_level5_power_matrix_gate.ps1" -NamedArgs @{
    OutputDir = $outputRoot
    ReportCsv = "level5_power_matrix_gate_runtime.csv"
    MetricsJson = "level5_power_matrix_gate_metrics.json"
}
$rows += $pmResult
if ($pmResult.pass -ne 1) {
    $firstFailedGate = "level5_power_matrix_gate"
}

if ([string]::IsNullOrWhiteSpace($firstFailedGate)) {
    $diffResult = Invoke-GateStep -Name "level5_diff_oracle_gate" -ScriptPath "$repo\run_level5_diff_oracle_gate.ps1" -NamedArgs @{
        Manifest = $manifestPath
        ExternalManifest = $externalManifestPath
        SpreadBudget = $SpreadBudget
        OutputDir = $outputRoot
    }
    $rows += $diffResult
    if ($diffResult.pass -ne 1) {
        $firstFailedGate = "level5_diff_oracle_gate"
    }
}

if ([string]::IsNullOrWhiteSpace($firstFailedGate)) {
    $chaosResult = Invoke-GateStep -Name "level5_chaos_soak_gate" -ScriptPath "$repo\run_level5_chaos_soak.ps1" -NamedArgs @{
        Profile = "fast"
        Manifest = $manifestPath
        ExternalManifest = $externalManifestPath
        Runs = $ChaosRuns
        EnableWarmup = $true
        WarmupKernelRepeat = 1
        RequirePowerMatrixGate = $RequirePowerMatrixGateInChaos
        MaxFlakeRate = $MaxFlakeRate
        SpreadBudget = $SpreadBudget
        OutputDir = $outputRoot
        ReportCsv = "level5_chaos_soak_runtime.csv"
        MetricsJson = "level5_chaos_soak_metrics.json"
    }
    $rows += $chaosResult
    if ($chaosResult.pass -ne 1) {
        $firstFailedGate = "level5_chaos_soak_gate"
    }
}

$rows | Export-Csv -LiteralPath $reportPath -NoTypeInformation -Encoding ASCII

$allPass = ([string]::IsNullOrWhiteSpace($firstFailedGate))
$pmMetricsPath = Join-Path -Path $outputRoot -ChildPath "level5_power_matrix_gate_metrics.json"
$diffMetricsPath = Join-Path -Path $outputRoot -ChildPath "level5_diff_oracle_gate_metrics.json"
$chaosMetricsPath = Join-Path -Path $outputRoot -ChildPath "level5_chaos_soak_metrics.json"

$pmPass = if (Test-Path -LiteralPath $pmMetricsPath) { [bool]((Get-Content -LiteralPath $pmMetricsPath -Raw | ConvertFrom-Json).overall_pass) } else { $false }
$diffPass = if (Test-Path -LiteralPath $diffMetricsPath) { [bool]((Get-Content -LiteralPath $diffMetricsPath -Raw | ConvertFrom-Json).overall_pass) } else { $false }
$chaosPass = if (Test-Path -LiteralPath $chaosMetricsPath) { [bool]((Get-Content -LiteralPath $chaosMetricsPath -Raw | ConvertFrom-Json).overall_pass) } else { $false }

$metrics = [ordered]@{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    manifest = $manifestPath
    external_manifest = $externalManifestPath
    metrics = [ordered]@{
        level5_power_matrix_pass = if ($pmPass) { 1 } else { 0 }
        level5_diff_oracle_pass = if ($diffPass) { 1 } else { 0 }
        level5_chaos_soak_pass = if ($chaosPass) { 1 } else { 0 }
        first_failed_gate = if ([string]::IsNullOrWhiteSpace($firstFailedGate)) { "none" } else { $firstFailedGate }
    }
    budgets = [ordered]@{
        differential_oracle_spread_max = $SpreadBudget
        chaos_max_flake_rate = $MaxFlakeRate
        all_gates_required = 1
    }
    promoted_state = if ($allPass) { "level5_software_ready" } else { "level5_blocked" }
    overall_pass = $allPass
}

($metrics | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $metricsPath -Encoding ASCII

$firstFailedLabel = if ([string]::IsNullOrWhiteSpace($firstFailedGate)) { "none" } else { $firstFailedGate }
"[L5-PROMOTION] PASS=$allPass first_failed_gate=$firstFailedLabel"
if (-not $allPass) {
    exit 1
}
exit 0
