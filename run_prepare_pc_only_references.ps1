param(
    [string]$TargetDir = "reference\vice"
)

$ErrorActionPreference = "Stop"

$py = "python"
$tool = Join-Path $PSScriptRoot "tools\make_pc_only_reference.py"
if (-not (Test-Path -LiteralPath $tool)) {
    throw "Missing tool: $tool"
}

$pairs = @(
    @{ src = "cpu_timing_branch_pagecross_proxy.trace.csv"; dst = (Join-Path $TargetDir "cpu_timing_branch_pagecross_proxy.trace.csv") },
    @{ src = "cpu_timing_rmw_dummy_reads_proxy.trace.csv"; dst = (Join-Path $TargetDir "cpu_timing_rmw_dummy_reads_proxy.trace.csv") },
    @{ src = "cpu_timing_irq_nmi_delay_proxy.trace.csv"; dst = (Join-Path $TargetDir "cpu_timing_irq_nmi_delay_proxy.trace.csv") },
    @{ src = "c64_vice_testprogs_cia.trace.csv"; dst = (Join-Path $TargetDir "c64_vice_testprogs_cia.trace.csv") },
    @{ src = "c64_vice_testprogs_vicii.trace.csv"; dst = (Join-Path $TargetDir "c64_vice_testprogs_vicii.trace.csv") }
)

foreach ($p in $pairs) {
    $src = Join-Path $PSScriptRoot $p.src
    $dst = Join-Path $PSScriptRoot $p.dst
    if (-not (Test-Path -LiteralPath $src)) {
        throw "Missing runtime trace: $src"
    }
    & $py $tool --source $src --dest $dst
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to build reference for $src"
    }
}

"[PC-REF] Completed reference/vice pc_only set."
