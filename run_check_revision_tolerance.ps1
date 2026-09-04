param(
    [string]$Policy = "reference/edge/revision_tolerance_policy.json",
    [string]$Metrics = "reference/edge/revision_tolerance_metrics.json"
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot
$py = "python"
$tool = Join-Path $repo "tools\check_revision_tolerance.py"

if (-not (Test-Path -LiteralPath $tool)) {
    throw "Missing tool: $tool"
}

$policyPath = $Policy
if (-not [System.IO.Path]::IsPathRooted($policyPath)) {
    $policyPath = Join-Path -Path $repo -ChildPath $policyPath
}
$metricsPath = $Metrics
if (-not [System.IO.Path]::IsPathRooted($metricsPath)) {
    $metricsPath = Join-Path -Path $repo -ChildPath $metricsPath
}

if (-not (Test-Path -LiteralPath $policyPath)) {
    throw "Missing policy file: $policyPath"
}
if (-not (Test-Path -LiteralPath $metricsPath)) {
    throw "Missing metrics file: $metricsPath"
}

& $py $tool --policy $policyPath --metrics $metricsPath
if ($LASTEXITCODE -ne 0) {
    throw "Revision tolerance check failed."
}

"[REV-TOL] PASS: revision tolerance policy respected."
