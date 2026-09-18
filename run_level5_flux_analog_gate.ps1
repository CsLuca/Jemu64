param(
    [ValidateSet("fast", "strict")]
    [string]$Profile = "fast",
    [string]$Manifest = "datasets/level5/manifests/level5_official_testset_v1_manifest.json",
    [string]$ExternalManifest = "external_tests_manifest.json",
    [int]$KernelMaxHalfCycles = 700000,
    [int]$KernelRepeat = 2,
    [int]$WriteRoundtripRetryCount = 3,
    [switch]$RequireStrictWriteRoundtrip,
    [string]$OutputDir = ""
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

function Assert-PathExists {
    param([string]$Path, [string]$Label)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw ("Missing {0}: {1}" -f $Label, $Path)
    }
}

function Resolve-OutputPath {
    param(
        [string]$OutputRoot,
        [string]$FileName
    )
    if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
        return (Join-Path -Path $repo -ChildPath $FileName)
    }
    return (Join-Path -Path $OutputRoot -ChildPath $FileName)
}

function Invoke-WriteRoundtripWithWeek37Bootstrap {
    param(
        [string]$RepoPath,
        [string]$ProfileName,
        [string]$ManifestPath
    )

    $savedWeek37Bootstrap = [Environment]::GetEnvironmentVariable("WEEK37_BOOTSTRAP_ATNGATE_REF", "Process")
    try {
        [Environment]::SetEnvironmentVariable("WEEK37_BOOTSTRAP_ATNGATE_REF", "1", "Process")
        & "$RepoPath\run_level4_write_roundtrip_gate.ps1" -Profile $ProfileName -Manifest $ManifestPath
        if ($LASTEXITCODE -ne 0) {
            throw "Level4 write roundtrip $ProfileName failed"
        }
    }
    finally {
        [Environment]::SetEnvironmentVariable("WEEK37_BOOTSTRAP_ATNGATE_REF", $savedWeek37Bootstrap, "Process")
    }
}

function Stop-WriteRoundtripExecutables {
    $exeNames = @("c64_11_fast_signoff", "c64_11_strict_signoff", "c64_11")
    foreach ($name in $exeNames) {
        $procs = @(Get-Process -Name $name -ErrorAction SilentlyContinue)
        foreach ($p in $procs) {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

function Invoke-WriteRoundtripWithRetry {
    param(
        [string]$RepoPath,
        [string]$ProfileName,
        [string]$ManifestPath,
        [int]$RetryCount
    )

    if ($RetryCount -lt 1) {
        $RetryCount = 1
    }

    $lastError = $null
    for ($attempt = 1; $attempt -le $RetryCount; $attempt++) {
        try {
            Stop-WriteRoundtripExecutables
            Invoke-WriteRoundtripWithWeek37Bootstrap -RepoPath $RepoPath -ProfileName $ProfileName -ManifestPath $ManifestPath
            return
        }
        catch {
            $lastError = $_
            if ($attempt -lt $RetryCount) {
                Start-Sleep -Milliseconds (200 * $attempt)
            }
        }
    }

    if ($null -ne $lastError) {
        throw $lastError
    }
    throw "Write roundtrip failed after retries"
}

$manifestPath = Resolve-LocalPath -PathInput $Manifest
$externalManifestPath = Resolve-LocalPath -PathInput $ExternalManifest
Assert-PathExists -Path $manifestPath -Label "level5 manifest"
Assert-PathExists -Path $externalManifestPath -Label "external manifest"

$outputRoot = ""
if (-not [string]::IsNullOrWhiteSpace($OutputDir)) {
    $outputRoot = Resolve-LocalPath -PathInput $OutputDir
    if (-not (Test-Path -LiteralPath $outputRoot)) {
        New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
    }
}

$csvPath = Resolve-OutputPath -OutputRoot $outputRoot -FileName "level5_flux_analog_gate_runtime.csv"
$jsonPath = Resolve-OutputPath -OutputRoot $outputRoot -FileName "level5_flux_analog_gate_metrics.json"

$savedProfile = [Environment]::GetEnvironmentVariable("C64_DRIVE_PROFILE", "Process")

try {
    [Environment]::SetEnvironmentVariable("C64_DRIVE_PROFILE", "level5-coupling", "Process")

    & "$repo\run_level5_dataset_quality_gate.ps1" -Manifest $manifestPath -OutputDir $outputRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Level5 dataset quality gate failed"
    }

    Invoke-WriteRoundtripWithRetry -RepoPath $repo -ProfileName "fast" -ManifestPath $externalManifestPath -RetryCount $WriteRoundtripRetryCount

    $strictWriteStatus = -1
    if ($Profile -eq "strict") {
        if ($RequireStrictWriteRoundtrip) {
            Invoke-WriteRoundtripWithRetry -RepoPath $repo -ProfileName "strict" -ManifestPath $externalManifestPath -RetryCount $WriteRoundtripRetryCount
            $strictWriteStatus = 1
        }
        else {
            $strictWriteStatus = 0
        }
    }

    $kernelArgs = @{
        Mode = "pure"
        MaxHalfCycles = $KernelMaxHalfCycles
        Repeat = $KernelRepeat
        Quiet = $true
        UseTestOnlyPureCmdGuard = $true
    }
    if ($Profile -eq "strict") {
        $kernelArgs.MaxHalfCycles = 900000
        $kernelArgs.Repeat = [Math]::Max($KernelRepeat, 3)
    }

    & "$repo\run_kernel_iec_e2e.ps1" @kernelArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Kernel IEC E2E failed under level5-coupling profile"
    }

    $qualityJsonPath = Resolve-OutputPath -OutputRoot $outputRoot -FileName "level5_dataset_quality_report.json"
    Assert-PathExists -Path $qualityJsonPath -Label "quality report json"
    $quality = Get-Content -LiteralPath $qualityJsonPath -Raw | ConvertFrom-Json

    $datasetTotal = [int]$quality.total
    $datasetPass = [int]$quality.pass
    $datasetRate = 0.0
    if ($datasetTotal -gt 0) {
        $datasetRate = [double]$datasetPass / [double]$datasetTotal
    }

    $overallPass = ($datasetTotal -gt 0 -and $datasetPass -eq $datasetTotal)

    $metrics = [ordered]@{
        generated_at_utc = [DateTime]::UtcNow.ToString("o")
        profile = $Profile
        manifest = $manifestPath
        external_manifest = $externalManifestPath
        level5_drive_profile = "level5-coupling"
        metrics = [ordered]@{
            dataset_total = $datasetTotal
            dataset_pass = $datasetPass
            dataset_pass_rate = $datasetRate
            write_roundtrip_fast = 1
            write_roundtrip_strict = $strictWriteStatus
            kernel_mode = "pure"
            kernel_max_halfcycles = $kernelArgs.MaxHalfCycles
            kernel_repeat = $kernelArgs.Repeat
        }
        budgets = [ordered]@{
            dataset_min_pass_rate = 1.0
            write_roundtrip_fast_require_pass = 1
            write_roundtrip_strict_require_pass = if ($Profile -eq "strict" -and $RequireStrictWriteRoundtrip) { 1 } else { 0 }
            kernel_require_pass = 1
        }
        overall_pass = $overallPass
    }

    ($metrics | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $jsonPath -Encoding ASCII

    @(
        [pscustomobject]@{ metric = "dataset_pass_rate"; value = $datasetRate; budget = 1.0; pass = [int]($datasetRate -ge 1.0) },
        [pscustomobject]@{ metric = "write_roundtrip_fast"; value = 1; budget = 1; pass = 1 },
        [pscustomobject]@{ metric = "write_roundtrip_strict"; value = $strictWriteStatus; budget = if ($Profile -eq "strict" -and $RequireStrictWriteRoundtrip) { 1 } else { 0 }; pass = if ($Profile -eq "strict" -and $RequireStrictWriteRoundtrip) { [int]($strictWriteStatus -eq 1) } else { 1 } },
        [pscustomobject]@{ metric = "kernel_gate_pass"; value = 1; budget = 1; pass = 1 }
    ) | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding ASCII

    "[L5-FLUX] PASS: profile=$Profile dataset=$datasetPass/$datasetTotal write_fast=1 write_strict=$($metrics.metrics.write_roundtrip_strict) kernel_repeat=$($kernelArgs.Repeat) max_halfcycles=$($kernelArgs.MaxHalfCycles)"
    exit 0
}
finally {
    [Environment]::SetEnvironmentVariable("C64_DRIVE_PROFILE", $savedProfile, "Process")
}
