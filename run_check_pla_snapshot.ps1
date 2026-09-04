$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot
$py = "python"
$tool = Join-Path $repo "tools\check_pla_snapshot.py"

if (-not (Test-Path -LiteralPath $tool)) {
    throw "Missing tool: $tool"
}

& $py $tool
if ($LASTEXITCODE -ne 0) {
    throw "PLA snapshot check failed"
}

"[PLA-CHECK] PASS: snapshot digest matches spec CSV."
