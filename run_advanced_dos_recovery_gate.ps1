param(
    [string]$Manifest = "advanced_dos_recovery_manifest.json",
    [ValidateSet("fast", "strict")]
    [string]$Profile = "fast",
    [string]$ExternalManifest = "external_tests_manifest.json",
    [string]$ReportCsv = "advanced_dos_recovery_runtime.csv"
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

$manifestPath = Resolve-LocalPath -PathInput $Manifest
$externalPath = Resolve-LocalPath -PathInput $ExternalManifest
$reportPath = Resolve-LocalPath -PathInput $ReportCsv

if (-not (Test-Path -LiteralPath $manifestPath)) { throw "Missing manifest: $manifestPath" }
if (-not (Test-Path -LiteralPath $externalPath)) { throw "Missing external manifest: $externalPath" }

$manifestObj = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($null -eq $manifestObj -or $null -eq $manifestObj.scenarios -or $manifestObj.scenarios.Count -lt 1) {
    throw "Invalid DOS recovery manifest: $manifestPath"
}

$savedEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    $runOutput = @(
        & "$repo\run_copier_matrix.ps1" -Profile $Profile -Manifest $externalPath 2>&1 | ForEach-Object { "$_" }
    )
    $runExitCode = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $savedEap
}

if ($runExitCode -ne 0) {
    foreach ($line in $runOutput) {
        $line
    }
    throw "run_copier_matrix failed in DOS recovery gate"
}

$reportJsonPath = Join-Path -Path $repo -ChildPath "copier_matrix_report.json"
if (-not (Test-Path -LiteralPath $reportJsonPath)) {
    throw "Missing copier matrix report: $reportJsonPath"
}
$reportObj = Get-Content -LiteralPath $reportJsonPath -Raw | ConvertFrom-Json
if ($null -eq $reportObj -or $null -eq $reportObj.scenarios -or $reportObj.scenarios.Count -lt 1) {
    throw "Invalid copier matrix report: $reportJsonPath"
}

$rows = @()
foreach ($scenario in $manifestObj.scenarios) {
    $id = [string]$scenario.id
    $marker = [string]$scenario.expected_marker
    $reportRow = $reportObj.scenarios | Where-Object { [string]$_.scenario_id -eq $id } | Select-Object -First 1
    $pass = ($null -ne $reportRow -and [int]$reportRow.overall_pass -eq 1)
    $rows += [pscustomobject]@{
        scenario_id = $id
        format = [string]$scenario.format
        marker = $marker
        pass = if ($pass) { 1 } else { 0 }
    }
    "[ADV-DOS-RECOVERY] scenario=$id pass=$pass"
}

$rows | Export-Csv -LiteralPath $reportPath -NoTypeInformation -Encoding ASCII

$passCount = @($rows | Where-Object { $_.pass -eq 1 }).Count
$total = $rows.Count
"[ADV-DOS-RECOVERY] summary pass=$passCount/$total report=$reportPath"
if ($passCount -ne $total) {
    exit 1
}
exit 0
