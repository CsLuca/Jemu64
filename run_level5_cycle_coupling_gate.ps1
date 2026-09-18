param(
    [ValidateSet("fast", "strict")]
    [string]$Profile = "fast",
    [string]$Manifest = "datasets/level5/manifests/level5_official_testset_v1_manifest.json",
    [int]$KernelMaxHalfCycles = 700000,
    [int]$KernelRepeat = 2,
    [int]$KernelRetryCount = 3,
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot
$gxx = "C:\msys64\ucrt64\bin\g++.exe"
$lockHelpersPath = Join-Path -Path $repo -ChildPath "tools\runner_lock_hardening.ps1"
. $lockHelpersPath

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

function Ensure-KernelExecutable {
    param(
        [string]$RepoPath,
        [string]$CompilerPath
    )

    $exePath = Join-Path -Path $RepoPath -ChildPath "c64_11.exe"
    if (Test-Path -LiteralPath $exePath) {
        return $exePath
    }

    if (-not (Test-Path -LiteralPath $CompilerPath)) {
        throw "Missing compiler: $CompilerPath"
    }

    $code = Build-Profile-Retry -Macro "RUN_PROFILE_FAST" -OutFile "c64_11.exe" -CompilerPath $CompilerPath -RepoPath $RepoPath -SourceFile "c64_11.cpp" -RetryCount 4
    if ($code -ne 0 -or -not (Test-Path -LiteralPath $exePath)) {
        throw "Failed to build c64_11.exe required by kernel IEC gate"
    }

    return $exePath
}

function Stop-KernelExecutables {
    $exeNames = @("c64_11", "c64_11_fast_signoff", "c64_11_strict_signoff")
    foreach ($name in $exeNames) {
        $procs = @(Get-Process -Name $name -ErrorAction SilentlyContinue)
        foreach ($p in $procs) {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

function Invoke-KernelE2EWithRetry {
    param(
        [string]$RepoPath,
        [hashtable]$KernelArgs,
        [int]$RetryCount
    )

    if ($RetryCount -lt 1) {
        $RetryCount = 1
    }

    $lastError = $null
    for ($attempt = 1; $attempt -le $RetryCount; $attempt++) {
        try {
            Stop-KernelExecutables
            & "$RepoPath\run_kernel_iec_e2e.ps1" @KernelArgs
            if ($LASTEXITCODE -ne 0) {
                throw "Kernel IEC E2E failed under level5-coupling profile"
            }
            return
        }
        catch {
            $lastError = $_
            if ($attempt -lt $RetryCount) {
                Start-Sleep -Milliseconds (250 * $attempt)
            }
        }
    }

    if ($null -ne $lastError) {
        throw $lastError
    }
    throw "Kernel IEC E2E failed after retries"
}

$manifestPath = Resolve-LocalPath -PathInput $Manifest
Assert-PathExists -Path $manifestPath -Label "level5 manifest"

$outputRoot = ""
if (-not [string]::IsNullOrWhiteSpace($OutputDir)) {
    $outputRoot = Resolve-LocalPath -PathInput $OutputDir
    if (-not (Test-Path -LiteralPath $outputRoot)) {
        New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
    }
}

$csvPath = Resolve-OutputPath -OutputRoot $outputRoot -FileName "level5_cycle_coupling_gate_runtime.csv"
$jsonPath = Resolve-OutputPath -OutputRoot $outputRoot -FileName "level5_cycle_coupling_gate_metrics.json"

$savedProfile = [Environment]::GetEnvironmentVariable("C64_DRIVE_PROFILE", "Process")

try {
    [Environment]::SetEnvironmentVariable("C64_DRIVE_PROFILE", "level5-coupling", "Process")

    & "$repo\run_level5_dataset_quality_gate.ps1" -Manifest $manifestPath -OutputDir $outputRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Level5 dataset quality gate failed"
    }

    Ensure-KernelExecutable -RepoPath $repo -CompilerPath $gxx | Out-Null

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

    Invoke-KernelE2EWithRetry -RepoPath $repo -KernelArgs $kernelArgs -RetryCount $KernelRetryCount

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
        level5_drive_profile = "level5-coupling"
        metrics = [ordered]@{
            dataset_total = $datasetTotal
            dataset_pass = $datasetPass
            dataset_pass_rate = $datasetRate
            kernel_mode = "pure"
            kernel_max_halfcycles = $kernelArgs.MaxHalfCycles
            kernel_repeat = $kernelArgs.Repeat
        }
        budgets = [ordered]@{
            dataset_min_pass_rate = 1.0
            kernel_require_pass = 1
        }
        overall_pass = $overallPass
    }

    ($metrics | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $jsonPath -Encoding ASCII

    @(
        [pscustomobject]@{ metric = "dataset_pass_rate"; value = $datasetRate; budget = 1.0; pass = [int]($datasetRate -ge 1.0) },
        [pscustomobject]@{ metric = "kernel_gate_pass"; value = 1; budget = 1; pass = 1 }
    ) | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding ASCII

    "[L5-CYCLE] PASS: profile=$Profile dataset=$datasetPass/$datasetTotal kernel_repeat=$($kernelArgs.Repeat) max_halfcycles=$($kernelArgs.MaxHalfCycles)"
    exit 0
}
finally {
    [Environment]::SetEnvironmentVariable("C64_DRIVE_PROFILE", $savedProfile, "Process")
}
