param(
    [string]$MatrixPath = "copier_matrix.json",
    [ValidateSet("fast", "strict")]
    [string]$Profile = "fast",
    [string]$Manifest = "external_tests_manifest.json",
    [string]$ReportJson = "copier_matrix_report.json",
    [string]$ReportCsv = "copier_matrix_report.csv"
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot
$gxx = "C:\msys64\ucrt64\bin\g++.exe"

function Resolve-PathLocal {
    param([string]$PathInput)
    if ([System.IO.Path]::IsPathRooted($PathInput)) {
        return $PathInput
    }
    return (Join-Path -Path $repo -ChildPath $PathInput)
}

function Build-Profile {
    param([string]$Macro, [string]$OutFile)
    & $gxx -std=c++17 -O2 "-DRUN_PROFILE=$Macro" "$repo\c64_11.cpp" -o "$repo\$OutFile"
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed: $OutFile"
    }
}

$savedPath = $env:PATH
$savedManifest = [Environment]::GetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", "Process")
$savedPureGuard = [Environment]::GetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", "Process")

try {
    $env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $env:PATH

    $matrixFull = Resolve-PathLocal -PathInput $MatrixPath
    $manifestFull = Resolve-PathLocal -PathInput $Manifest
    if (-not (Test-Path -LiteralPath $matrixFull)) {
        throw "Missing copier matrix: $matrixFull"
    }
    if (-not (Test-Path -LiteralPath $manifestFull)) {
        throw "Missing manifest: $manifestFull"
    }

    $matrix = Get-Content -LiteralPath $matrixFull -Raw | ConvertFrom-Json
    if ($null -eq $matrix -or $null -eq $matrix.scenarios -or $matrix.scenarios.Count -lt 1) {
        throw "Invalid copier matrix JSON: $matrixFull"
    }

    $exe = "c64_11_fast_signoff.exe"
    $macro = "RUN_PROFILE_FAST"
    if ($Profile -eq "strict") {
        $exe = "c64_11_strict_signoff.exe"
        $macro = "RUN_PROFILE_STRICT"
    }

    Build-Profile -Macro $macro -OutFile $exe

    [Environment]::SetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", $manifestFull, "Process")
    [Environment]::SetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", "1", "Process")

    $savedEapRun = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $runOutput = @(& "$repo\$exe" 2>&1 | ForEach-Object { "$_" })
        $runExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedEapRun
    }
    $text = ($runOutput | Out-String)

    $manifestCsvPath = Resolve-PathLocal -PathInput ([string]$matrix.defaults.artifacts.manifest_csv)
    $manifestRows = @()
    if (Test-Path -LiteralPath $manifestCsvPath) {
        $manifestRows = @(Import-Csv -LiteralPath $manifestCsvPath)
    }

    $rows = @()
    foreach ($scenario in $matrix.scenarios) {
        $requiredMarkers = @($scenario.gates.required_markers)
        $markerPass = $true
        foreach ($m in $requiredMarkers) {
            if ($text -notmatch [regex]::Escape([string]$m)) {
                $markerPass = $false
                break
            }
        }

        $manifestPass = $true
        $manifestReason = "not_required"
        if ([bool]$scenario.gates.require_manifest_match) {
            if (-not (Test-Path -LiteralPath $manifestCsvPath)) {
                $manifestPass = $false
                $manifestReason = "missing_manifest"
            } else {
                $manifestReason = "checked"
                $expectedCols = @($scenario.gates.expected_manifest_columns)
                if ($manifestRows.Count -lt 1) {
                    $manifestPass = $false
                    $manifestReason = "empty_manifest"
                } else {
                    $actualCols = @($manifestRows[0].PSObject.Properties | ForEach-Object { $_.Name })
                    foreach ($c in $expectedCols) {
                        if ($actualCols -notcontains [string]$c) {
                            $manifestPass = $false
                            $manifestReason = "missing_column:$c"
                            break
                        }
                    }
                    if ($manifestPass) {
                        $matchOk = $false
                        foreach ($r in $manifestRows) {
                            if ([string]$r.match -eq "1") {
                                $matchOk = $true
                                break
                            }
                        }
                        if (-not $matchOk) {
                            $manifestPass = $false
                            $manifestReason = "match_not_1"
                        }
                    }
                }
            }
        }

        $runtimePass = ($runExitCode -eq 0)
        $overall = $runtimePass -and $markerPass -and $manifestPass

        $rows += [pscustomobject]@{
            scenario_id = [string]$scenario.id
            level = [string]$scenario.level
            mode = [string]$scenario.mode
            runtime_exit_ok = if ($runtimePass) { 1 } else { 0 }
            marker_pass = if ($markerPass) { 1 } else { 0 }
            manifest_pass = if ($manifestPass) { 1 } else { 0 }
            manifest_reason = $manifestReason
            overall_pass = if ($overall) { 1 } else { 0 }
        }
    }

    $reportObj = [pscustomobject]@{
        version = [string]$matrix.version
        profile = $Profile
        runtime_exit_code = $runExitCode
        matrix_path = $matrixFull
        manifest_csv_path = $manifestCsvPath
        generated_at_utc = [DateTime]::UtcNow.ToString("o")
        scenarios = $rows
    }

    $reportJsonPath = Resolve-PathLocal -PathInput $ReportJson
    $reportCsvPath = Resolve-PathLocal -PathInput $ReportCsv
    ($reportObj | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $reportJsonPath -Encoding UTF8
    $rows | Export-Csv -LiteralPath $reportCsvPath -NoTypeInformation -Encoding UTF8

    $passCount = @($rows | Where-Object { $_.overall_pass -eq 1 }).Count
    $totalCount = @($rows).Count
    "[COPIER-MATRIX] profile=$Profile pass=$passCount/$totalCount report_json=$reportJsonPath report_csv=$reportCsvPath"
    if ($passCount -ne $totalCount) {
        exit 2
    }

    exit 0
}
finally {
    [Environment]::SetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", $savedManifest, "Process")
    [Environment]::SetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", $savedPureGuard, "Process")
    $env:PATH = $savedPath
}
