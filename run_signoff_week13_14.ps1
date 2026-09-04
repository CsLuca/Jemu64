param(
    [string]$Manifest = "external_tests_golden_corpus.json",
    [string]$FastManifest = "external_tests_golden_corpus_fast.json",
    [string]$Manifest6510 = "external_tests_golden_corpus_6510.json",
    [string]$Manifest8500 = "external_tests_golden_corpus_8500.json",
    [string]$FastManifest6510 = "external_tests_golden_corpus_6510_fast.json",
    [string]$FastManifest8500 = "external_tests_golden_corpus_8500_fast.json",
    [ValidateSet("all", "6510", "8500")]
    [string]$RevisionSlot = "all",
    [int]$KernelMaxHalfCycles = 700000,
    [switch]$SkipFastExternal
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot
$gxx = "C:\msys64\ucrt64\bin\g++.exe"

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Action,
        [scriptblock]$Assert
    )

    $savedEapStep = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = @(& $Action 2>&1 | ForEach-Object { "$_" })
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedEapStep
    }
    $ok = $true
    if ($Assert -ne $null) {
        $ok = (& $Assert $output $exitCode)
    } else {
        $ok = ($exitCode -eq 0)
    }

    if ($ok) {
        "[SIGNOFF] PASS $Name"
    } else {
        "[SIGNOFF] FAIL $Name (exit=$exitCode)"
        foreach ($line in $output) {
            $line
        }
        throw "Step failed: $Name"
    }

    return [pscustomobject]@{
        Name = $Name
        ExitCode = $exitCode
        Output = $output
        Ok = $ok
    }
}

function Build-Profile {
    param(
        [string]$Macro,
        [string]$OutFile
    )

    & $gxx -std=c++17 -O2 "-DRUN_PROFILE=$Macro" "$repo\c64_11.cpp" -o "$repo\$OutFile"
}

function Run-Binary {
    param(
        [string]$ExePath,
        [string]$ManifestPath,
        [bool]$NeedWeek45,
        [bool]$NeedWeek12,
        [bool]$NeedExternal,
        [bool]$NeedNoFallback,
        [hashtable]$ExtraEnv
    )

    $savedManifest = [Environment]::GetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", "Process")
    $savedPureGuard = [Environment]::GetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", "Process")
    try {
        [Environment]::SetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", $ManifestPath, "Process")
        [Environment]::SetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", "1", "Process")
        $savedExtra = @{}
        if ($ExtraEnv -ne $null) {
            foreach ($k in $ExtraEnv.Keys) {
                $savedExtra[$k] = [Environment]::GetEnvironmentVariable($k, "Process")
                [Environment]::SetEnvironmentVariable($k, [string]$ExtraEnv[$k], "Process")
            }
        }
        $output = @(& $ExePath 2>&1 | ForEach-Object { "$_" })
        $exitCode = $LASTEXITCODE

        if ($exitCode -ne 0) {
            return ,@($false, $output, $exitCode)
        }

        $text = ($output | Out-String)
        if ($NeedWeek45 -and ($text -notmatch "\[WEEK45 TIME\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek12 -and ($text -notmatch "\[WEEK12\] PASS: interrupt-boundary suite mismatches=0")) { return ,@($false, $output, $exitCode) }
        if ($NeedExternal -and ($text -notmatch "\[EXT\] External validation PASSED\.")) { return ,@($false, $output, $exitCode) }
        if ($NeedNoFallback -and ($text -notmatch "host_fallback=no")) { return ,@($false, $output, $exitCode) }

        return ,@($true, $output, $exitCode)
    }
    finally {
        if ($savedExtra -ne $null) {
            foreach ($k in $savedExtra.Keys) {
                [Environment]::SetEnvironmentVariable($k, $savedExtra[$k], "Process")
            }
        }
        [Environment]::SetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", $savedManifest, "Process")
        [Environment]::SetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", $savedPureGuard, "Process")
    }
}

function Resolve-ManifestPath {
    param([string]$ManifestInput, [string]$FallbackManifestInput)

    $path = $ManifestInput
    if (-not [System.IO.Path]::IsPathRooted($path)) {
        $path = Join-Path -Path $repo -ChildPath $path
    }
    if (-not (Test-Path -LiteralPath $path)) {
        $path = $FallbackManifestInput
        if (-not [System.IO.Path]::IsPathRooted($path)) {
            $path = Join-Path -Path $repo -ChildPath $path
        }
    }
    return $path
}

$metrics = [ordered]@{
    strict_6510_exit = 0
    strict_8500_exit = 0
    fast_6510_exit = 0
    fast_8500_exit = 0
    week18_openbus_rows = 0
    week18_openbus_hmos_decay_threshold = 0
    week18_openbus_nmos_decay_threshold = 0
    week19_cia_dense_rows = 0
    week20_vic_path_rows = 0
    week20_vic_path_vsp_hits = 0
    week20_vic_path_fld_hits = 0
    week21_bus_corner_rows = 0
    week22_port_map_rows = 0
    week22_port_map_decay_observed = 0
}

$savedPath = $env:PATH
try {
    $env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $env:PATH

    $results = @()

    $results += Invoke-Step -Name "build-fast" -Action { Build-Profile -Macro "RUN_PROFILE_FAST" -OutFile "c64_11_fast_signoff.exe" } -Assert { param($o, $e) $e -eq 0 }
    $results += Invoke-Step -Name "build-strict" -Action { Build-Profile -Macro "RUN_PROFILE_STRICT" -OutFile "c64_11_strict_signoff.exe" } -Assert { param($o, $e) $e -eq 0 }
    $results += Invoke-Step -Name "build-full" -Action { Build-Profile -Macro "RUN_PROFILE_FULL" -OutFile "c64_11_full_signoff.exe" } -Assert { param($o, $e) $e -eq 0 }

    $results += Invoke-Step -Name "run-fast" -Action {
        if ($SkipFastExternal) {
            $savedManifest = [Environment]::GetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", "Process")
            $savedPureGuard = [Environment]::GetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", "Process")
            try {
                [Environment]::SetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", $null, "Process")
                [Environment]::SetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", "1", "Process")
                & "$repo\c64_11_fast_signoff.exe"
                if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            }
            finally {
                [Environment]::SetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", $savedManifest, "Process")
                [Environment]::SetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", $savedPureGuard, "Process")
            }
            return
        }
        $fastManifestPath = Resolve-ManifestPath -ManifestInput $FastManifest -FallbackManifestInput $Manifest
        $r = Run-Binary -ExePath "$repo\c64_11_fast_signoff.exe" -ManifestPath $fastManifestPath -NeedWeek45:$false -NeedWeek12:$false -NeedExternal:$true -NeedNoFallback:$true -ExtraEnv $null
        $script:__runFast = $r
        if (-not $r[0]) { foreach ($line in $r[1]) { $line }; exit $r[2] }

        if ($RevisionSlot -ne "8500") {
            $fast6510 = Resolve-ManifestPath -ManifestInput $FastManifest6510 -FallbackManifestInput $Manifest
            $r6510 = Run-Binary -ExePath "$repo\c64_11_fast_signoff.exe" -ManifestPath $fast6510 -NeedWeek45:$false -NeedWeek12:$false -NeedExternal:$true -NeedNoFallback:$true -ExtraEnv @{ C64_CPU_REVISION = '6510'; C64_VIC_REVISION = '6569'; C64_CIA_REVISION = '6526'; C64_OPENBUS_REVISION = 'nmos'; C64_DRIVE_REVISION = '1541' }
            $metrics.fast_6510_exit = [int]$r6510[2]
            if (-not $r6510[0]) { foreach ($line in $r6510[1]) { $line }; exit $r6510[2] }
        }

        if ($RevisionSlot -ne "6510") {
            $fast8500 = Resolve-ManifestPath -ManifestInput $FastManifest8500 -FallbackManifestInput $Manifest
            $r8500 = Run-Binary -ExePath "$repo\c64_11_fast_signoff.exe" -ManifestPath $fast8500 -NeedWeek45:$false -NeedWeek12:$false -NeedExternal:$true -NeedNoFallback:$true -ExtraEnv @{ C64_CPU_REVISION = '8500'; C64_VIC_REVISION = '8565'; C64_CIA_REVISION = '6526A'; C64_OPENBUS_REVISION = 'hmos'; C64_DRIVE_REVISION = '1541C' }
            $metrics.fast_8500_exit = [int]$r8500[2]
            if (-not $r8500[0]) { foreach ($line in $r8500[1]) { $line }; exit $r8500[2] }
        }
    } -Assert { param($o, $e) $e -eq 0 }

    $results += Invoke-Step -Name "run-strict" -Action {
        $r = Run-Binary -ExePath "$repo\c64_11_strict_signoff.exe" -ManifestPath $Manifest -NeedWeek45:$true -NeedWeek12:$true -NeedExternal:$true -NeedNoFallback:$true -ExtraEnv $null
        $script:__runStrict = $r
        if (-not $r[0]) { foreach ($line in $r[1]) { $line }; exit $r[2] }

        if ($RevisionSlot -ne "8500") {
            $strict6510 = Resolve-ManifestPath -ManifestInput $Manifest6510 -FallbackManifestInput $Manifest
            $r6510 = Run-Binary -ExePath "$repo\c64_11_strict_signoff.exe" -ManifestPath $strict6510 -NeedWeek45:$true -NeedWeek12:$true -NeedExternal:$true -NeedNoFallback:$true -ExtraEnv @{ C64_CPU_REVISION = '6510'; C64_VIC_REVISION = '6569'; C64_CIA_REVISION = '6526'; C64_OPENBUS_REVISION = 'nmos'; C64_DRIVE_REVISION = '1541' }
            $metrics.strict_6510_exit = [int]$r6510[2]
            if (-not $r6510[0]) { foreach ($line in $r6510[1]) { $line }; exit $r6510[2] }
        }

        if ($RevisionSlot -ne "6510") {
            $strict8500 = Resolve-ManifestPath -ManifestInput $Manifest8500 -FallbackManifestInput $Manifest
            $r8500 = Run-Binary -ExePath "$repo\c64_11_strict_signoff.exe" -ManifestPath $strict8500 -NeedWeek45:$true -NeedWeek12:$true -NeedExternal:$true -NeedNoFallback:$true -ExtraEnv @{ C64_CPU_REVISION = '8500'; C64_VIC_REVISION = '8565'; C64_CIA_REVISION = '6526A'; C64_OPENBUS_REVISION = 'hmos'; C64_DRIVE_REVISION = '1541C' }
            $metrics.strict_8500_exit = [int]$r8500[2]
            if (-not $r8500[0]) { foreach ($line in $r8500[1]) { $line }; exit $r8500[2] }
        }
    } -Assert { param($o, $e) $e -eq 0 }

    $results += Invoke-Step -Name "run-full" -Action {
        $r = Run-Binary -ExePath "$repo\c64_11_full_signoff.exe" -ManifestPath $Manifest -NeedWeek45:$true -NeedWeek12:$true -NeedExternal:$false -NeedNoFallback:$false -ExtraEnv $null
        $script:__runFull = $r
        if (-not $r[0]) { foreach ($line in $r[1]) { $line }; exit $r[2] }
    } -Assert { param($o, $e) $e -eq 0 }

    Copy-Item -LiteralPath "$repo\c64_11_fast_signoff.exe" -Destination "$repo\c64_11.exe" -Force

    $results += Invoke-Step -Name "run-pure" -Action {
        & "$repo\run_kernel_iec_e2e.ps1" -Mode pure -MaxHalfCycles $KernelMaxHalfCycles -Quiet -UseTestOnlyPureCmdGuard
    } -Assert {
        param($o, $e)
        if ($e -ne 0) { return $false }
        $txt = ($o | Out-String)
        return ($txt -match "\[RUNNER\] mode=pure" -and $txt -match "pass=True")
    }

    $results += Invoke-Step -Name "run-compat" -Action {
        & "$repo\run_kernel_iec_e2e.ps1" -Mode compat -MaxHalfCycles $KernelMaxHalfCycles -Quiet -EnableCompatClockAssist -EnableCompatRamSinkInject -EnableCompatRamSinkBulk -EnableReplayCiaLog -EnableDd00Trace -EnableDriveAutoTalkDir -EnableDriveAutoDirOnTalk0 -EnableDriveForceTalkOnDd0d8 -IecPolarity "0,0,0,0,0,0,1,0,1,1"
    } -Assert {
        param($o, $e)
        if ($e -ne 0) { return $false }
        $txt = ($o | Out-String)
        return ($txt -match "\[RUNNER\] mode=compat" -and $txt -match "pass=True")
    }

    "[SIGNOFF] ----------------------------------------"
    "[SIGNOFF] Week13-14 status: PASS"
    "[SIGNOFF] strict/full/fast: green"
    "[SIGNOFF] pure/compat: green"
    "[SIGNOFF] drift test: stable ([WEEK45 TIME] PASS)"
    "[SIGNOFF] interrupt boundary: zero mismatch ([WEEK12] PASS)"
    "[SIGNOFF] no hidden fallback: enforced (host_fallback=no)"
    "[SIGNOFF] strict/full manifest: $Manifest"
    $fastManifestReport = Resolve-ManifestPath -ManifestInput $FastManifest -FallbackManifestInput $Manifest
    "[SIGNOFF] fast manifest: $fastManifestReport"
    "[SIGNOFF] strict 6510 manifest: $(Resolve-ManifestPath -ManifestInput $Manifest6510 -FallbackManifestInput $Manifest)"
    "[SIGNOFF] strict 8500 manifest: $(Resolve-ManifestPath -ManifestInput $Manifest8500 -FallbackManifestInput $Manifest)"
    "[SIGNOFF] fast 6510 manifest: $(Resolve-ManifestPath -ManifestInput $FastManifest6510 -FallbackManifestInput $Manifest)"
    "[SIGNOFF] fast 8500 manifest: $(Resolve-ManifestPath -ManifestInput $FastManifest8500 -FallbackManifestInput $Manifest)"
    $week18Ref = Join-Path -Path $repo -ChildPath "reference\edge\week18_openbus_revision_trace.csv"
    if (Test-Path -LiteralPath $week18Ref) {
        $rows = @(Get-Content -LiteralPath $week18Ref)
        if ($rows.Count -gt 1) {
            $metrics.week18_openbus_rows = $rows.Count - 1
        }
        foreach ($line in $rows) {
            if ($line -like "nmos,0,*") {
                $parts = $line.Split(',')
                if ($parts.Count -ge 3) { $metrics.week18_openbus_nmos_decay_threshold = [int]$parts[2] }
            }
            if ($line -like "hmos,0,*") {
                $parts = $line.Split(',')
                if ($parts.Count -ge 3) { $metrics.week18_openbus_hmos_decay_threshold = [int]$parts[2] }
            }
        }
    }
    $week19Ref = Join-Path -Path $repo -ChildPath "reference\edge\week19_cia_dense_trace.csv"
    if (Test-Path -LiteralPath $week19Ref) {
        $rows19 = @(Get-Content -LiteralPath $week19Ref)
        if ($rows19.Count -gt 1) {
            $metrics.week19_cia_dense_rows = $rows19.Count - 1
        }
    }
    $week20Ref = Join-Path -Path $repo -ChildPath "reference\edge\week20_vic_pathological_trace.csv"
    if (Test-Path -LiteralPath $week20Ref) {
        $rows20 = @(Get-Content -LiteralPath $week20Ref)
        if ($rows20.Count -gt 1) {
            $metrics.week20_vic_path_rows = $rows20.Count - 1
            $vspHits = 0
            $fldHits = 0
            foreach ($line in $rows20) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 10) {
                    $rev = $parts[0]
                    if (($parts[7] -eq '1') -and ($rev -like '6569*')) { $vspHits++ }
                    if (($parts[8] -eq '1') -and ($rev -like '8565*')) { $fldHits++ }
                }
            }
            $metrics.week20_vic_path_vsp_hits = $vspHits
            $metrics.week20_vic_path_fld_hits = $fldHits
        }
    }
    $week21Ref = Join-Path -Path $repo -ChildPath "reference\edge\week21_bus_corner_trace.csv"
    if (Test-Path -LiteralPath $week21Ref) {
        $rows21 = @(Get-Content -LiteralPath $week21Ref)
        if ($rows21.Count -gt 1) {
            $metrics.week21_bus_corner_rows = $rows21.Count - 1
        }
    }
    $week22Ref = Join-Path -Path $repo -ChildPath "reference\edge\week22_port_map_trace.csv"
    if (Test-Path -LiteralPath $week22Ref) {
        $rows22 = @(Get-Content -LiteralPath $week22Ref)
        if ($rows22.Count -gt 1) {
            $metrics.week22_port_map_rows = $rows22.Count - 1
            $decayObserved = 0
            foreach ($line in $rows22) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 11) {
                    if (($parts[1] -eq 'floating_decay') -and ($parts[3] -eq $parts[4])) {
                        $decayObserved++
                    }
                }
            }
            $metrics.week22_port_map_decay_observed = $decayObserved
        }
    }
    $metricsPath = Join-Path -Path $repo -ChildPath "reference\edge\revision_tolerance_metrics.json"
    (@{ metrics = $metrics } | ConvertTo-Json -Depth 5) | Set-Content -LiteralPath $metricsPath -Encoding ASCII
    "[SIGNOFF] tolerance metrics: $metricsPath"
    "[SIGNOFF] ----------------------------------------"
}
finally {
    $env:PATH = $savedPath
}
