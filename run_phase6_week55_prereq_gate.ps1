param(
    [string]$Manifest = "external_tests_manifest.json",
    [int]$KernelMaxHalfCycles = 700000,
    [int]$PureStabilityRuns = 12,
    [switch]$RebuildStrict
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot

function Invoke-External {
    param(
        [scriptblock]$Action,
        [string]$FailureMessage
    )

    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $Action
        if ($LASTEXITCODE -ne 0) {
            throw $FailureMessage
        }
    }
    finally {
        $ErrorActionPreference = $savedEap
    }
}

"[PHASE6-W55] STEP 1/4 prepare edge references"
if ($RebuildStrict) {
    Invoke-External -Action {
        & "$repo\run_prepare_edge_references.ps1" -Manifest $Manifest -RebuildStrict
    } -FailureMessage "Phase6 Week55 gate failed at prepare step"
} else {
    Invoke-External -Action {
        & "$repo\run_prepare_edge_references.ps1" -Manifest $Manifest
    } -FailureMessage "Phase6 Week55 gate failed at prepare step"
}

"[PHASE6-W55] STEP 2/4 signoff"
Invoke-External -Action {
    & "$repo\run_signoff_week13_14.ps1" `
        -Manifest $Manifest `
        -FastManifest $Manifest `
        -Manifest6510 $Manifest `
        -Manifest8500 $Manifest `
        -FastManifest6510 $Manifest `
        -FastManifest8500 $Manifest `
        -KernelMaxHalfCycles $KernelMaxHalfCycles `
        -PureStabilityRuns $PureStabilityRuns
} -FailureMessage "Phase6 Week55 gate failed at signoff step"

"[PHASE6-W55] STEP 3/4 tolerance"
Invoke-External -Action {
    & "$repo\run_check_revision_tolerance.ps1"
} -FailureMessage "Phase6 Week55 gate failed at tolerance step"

"[PHASE6-W55] STEP 4/4 e2e compat"
Invoke-External -Action {
    & "$repo\run_kernel_iec_e2e.ps1" -Mode compat -MaxHalfCycles $KernelMaxHalfCycles -Repeat 1 -Quiet -EnableCompatClockAssist -EnableCompatRamSinkInject -EnableCompatRamSinkBulk -EnableReplayCiaLog -EnableDd00Trace -EnableDriveAutoTalkDir -EnableDriveAutoDirOnTalk0 -EnableDriveForceTalkOnDd0d8 -IecPolarity "0,0,0,0,0,0,1,0,1,1"
} -FailureMessage "Phase6 Week55 gate failed at compat e2e step"

"[PHASE6-W55] PASS: prepare + signoff + tolerance + e2e are all green in sequence."
