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
    week23_cia_irq_nmi_rows = 0
    week23_cia_irq_nmi_dual_assert_rows = 0
    week24_irq_latch_rows = 0
    week24_irq_latch_irq_sampled_rows = 0
    week25_cia_serial_rows = 0
    week25_cia_serial_rx_bytes = 0
    week25_cia_serial_tx_bytes = 0
    week26_drive_iec_rows = 0
    week26_drive_iec_rx_processed_max = 0
    week26_drive_iec_atn_ack_active_rows = 0
    week27_drive_cmdphase_rows = 0
    week27_drive_cmdphase_talk_sa0_rows = 0
    week28_drive_eoi_rows = 0
    week28_drive_eoi_ack_count_max = 0
    week28_drive_eoi_pending_rows = 0
    week29_drive_timeout_rows = 0
    week29_drive_rx_timeout_max = 0
    week29_drive_tx_timeout_max = 0
    week29_drive_eoi_timeout_max = 0
    week30_drive_cmdch_rows = 0
    week30_drive_cmdch_syntax_err_max = 0
    week30_drive_cmdch_dispatch_max = 0
    week31_drive_status_rows = 0
    week31_drive_status_talking_rows = 0
    week31_drive_status_txq_max = 0
    week32_drive_dir_rows = 0
    week32_drive_dir_blockbuf_rows = 0
    week32_drive_dir_txq_max = 0
    week33_drive_dir_filter_rows = 0
    week33_drive_dir_filter_negated_rows = 0
    week33_drive_dir_filter_txq_max = 0
    week34_drive_alloc_rows = 0
    week34_drive_alloc_fail_rows = 0
    week34_drive_blocks_free_min = 0
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
    $week23Ref = Join-Path -Path $repo -ChildPath "reference\edge\week23_cia_irq_nmi_trace.csv"
    if (Test-Path -LiteralPath $week23Ref) {
        $rows23 = @(Get-Content -LiteralPath $week23Ref)
        if ($rows23.Count -gt 1) {
            $metrics.week23_cia_irq_nmi_rows = $rows23.Count - 1
            $dualAssert = 0
            foreach ($line in $rows23) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 9) {
                    if (($parts[7] -eq '0') -and ($parts[8] -eq '0')) {
                        $dualAssert++
                    }
                }
            }
            $metrics.week23_cia_irq_nmi_dual_assert_rows = $dualAssert
        }
    }
    $week24Ref = Join-Path -Path $repo -ChildPath "reference\edge\week24_irq_latch_trace.csv"
    if (Test-Path -LiteralPath $week24Ref) {
        $rows24 = @(Get-Content -LiteralPath $week24Ref)
        if ($rows24.Count -gt 1) {
            $metrics.week24_irq_latch_rows = $rows24.Count - 1
            $irqSampledRows = 0
            foreach ($line in $rows24) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 11) {
                    if ($parts[6] -eq '1') {
                        $irqSampledRows++
                    }
                }
            }
            $metrics.week24_irq_latch_irq_sampled_rows = $irqSampledRows
        }
    }
    $week25Ref = Join-Path -Path $repo -ChildPath "reference\edge\week25_cia_serial_trace.csv"
    if (Test-Path -LiteralPath $week25Ref) {
        $rows25 = @(Get-Content -LiteralPath $week25Ref)
        if ($rows25.Count -gt 1) {
            $metrics.week25_cia_serial_rows = $rows25.Count - 1
            $rxBytes = 0
            $txBytes = 0
            foreach ($line in $rows25) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 11) {
                    $rxVal = 0
                    $txVal = 0
                    if ([int]::TryParse($parts[7], [ref]$rxVal)) { if ($rxVal -gt $rxBytes) { $rxBytes = $rxVal } }
                    if ([int]::TryParse($parts[8], [ref]$txVal)) { if ($txVal -gt $txBytes) { $txBytes = $txVal } }
                }
            }
            $metrics.week25_cia_serial_rx_bytes = $rxBytes
            $metrics.week25_cia_serial_tx_bytes = $txBytes
        }
    }
    $week26Ref = Join-Path -Path $repo -ChildPath "reference\edge\week26_drive_iec_trace.csv"
    if (Test-Path -LiteralPath $week26Ref) {
        $rows26 = @(Get-Content -LiteralPath $week26Ref)
        if ($rows26.Count -gt 1) {
            $metrics.week26_drive_iec_rows = $rows26.Count - 1
            $rxProcessedMax = 0
            $ackActiveRows = 0
            foreach ($line in $rows26) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 12) {
                    $rxVal = 0
                    if ([int]::TryParse($parts[9], [ref]$rxVal)) { if ($rxVal -gt $rxProcessedMax) { $rxProcessedMax = $rxVal } }
                    if ($parts[5] -eq '1') { $ackActiveRows++ }
                }
            }
            $metrics.week26_drive_iec_rx_processed_max = $rxProcessedMax
            $metrics.week26_drive_iec_atn_ack_active_rows = $ackActiveRows
        }
    }
    $week27Ref = Join-Path -Path $repo -ChildPath "reference\edge\week27_drive_cmdphase_trace.csv"
    if (Test-Path -LiteralPath $week27Ref) {
        $rows27 = @(Get-Content -LiteralPath $week27Ref)
        if ($rows27.Count -gt 1) {
            $metrics.week27_drive_cmdphase_rows = $rows27.Count - 1
            $talkSa0Rows = 0
            foreach ($line in $rows27) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 13) {
                    if ($parts[9] -eq '1') { $talkSa0Rows++ }
                }
            }
            $metrics.week27_drive_cmdphase_talk_sa0_rows = $talkSa0Rows
        }
    }
    $week28Ref = Join-Path -Path $repo -ChildPath "reference\edge\week28_drive_eoi_atn_trace.csv"
    if (Test-Path -LiteralPath $week28Ref) {
        $rows28 = @(Get-Content -LiteralPath $week28Ref)
        if ($rows28.Count -gt 1) {
            $metrics.week28_drive_eoi_rows = $rows28.Count - 1
            $ackMax = 0
            $eoiPendingRows = 0
            foreach ($line in $rows28) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 14) {
                    $ackVal = 0
                    if ([int]::TryParse($parts[9], [ref]$ackVal)) { if ($ackVal -gt $ackMax) { $ackMax = $ackVal } }
                    if ($parts[6] -eq '1') { $eoiPendingRows++ }
                }
            }
            $metrics.week28_drive_eoi_ack_count_max = $ackMax
            $metrics.week28_drive_eoi_pending_rows = $eoiPendingRows
        }
    }
    $week29Ref = Join-Path -Path $repo -ChildPath "reference\edge\week29_drive_timeout_trace.csv"
    if (Test-Path -LiteralPath $week29Ref) {
        $rows29 = @(Get-Content -LiteralPath $week29Ref)
        if ($rows29.Count -gt 1) {
            $metrics.week29_drive_timeout_rows = $rows29.Count - 1
            $rxTimeoutMax = 0
            $txTimeoutMax = 0
            $eoiTimeoutMax = 0
            foreach ($line in $rows29) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 16) {
                    $rxVal = 0
                    $txVal = 0
                    $eoiVal = 0
                    if ([int]::TryParse($parts[6], [ref]$rxVal)) { if ($rxVal -gt $rxTimeoutMax) { $rxTimeoutMax = $rxVal } }
                    if ([int]::TryParse($parts[9], [ref]$txVal)) { if ($txVal -gt $txTimeoutMax) { $txTimeoutMax = $txVal } }
                    if ([int]::TryParse($parts[11], [ref]$eoiVal)) { if ($eoiVal -gt $eoiTimeoutMax) { $eoiTimeoutMax = $eoiVal } }
                }
            }
            $metrics.week29_drive_rx_timeout_max = $rxTimeoutMax
            $metrics.week29_drive_tx_timeout_max = $txTimeoutMax
            $metrics.week29_drive_eoi_timeout_max = $eoiTimeoutMax
        }
    }
    $week30Ref = Join-Path -Path $repo -ChildPath "reference\edge\week30_drive_cmdch_trace.csv"
    if (Test-Path -LiteralPath $week30Ref) {
        $rows30 = @(Get-Content -LiteralPath $week30Ref)
        if ($rows30.Count -gt 1) {
            $metrics.week30_drive_cmdch_rows = $rows30.Count - 1
            $syntaxMax = 0
            $dispatchMax = 0
            foreach ($line in $rows30) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 11) {
                    $cmdVal = 0
                    $syntaxVal = 0
                    if ([int]::TryParse($parts[3], [ref]$cmdVal)) { if ($cmdVal -gt $dispatchMax) { $dispatchMax = $cmdVal } }
                    if ([int]::TryParse($parts[5], [ref]$syntaxVal)) { if ($syntaxVal -gt $syntaxMax) { $syntaxMax = $syntaxVal } }
                }
            }
            $metrics.week30_drive_cmdch_syntax_err_max = $syntaxMax
            $metrics.week30_drive_cmdch_dispatch_max = $dispatchMax
        }
    }
    $week31Ref = Join-Path -Path $repo -ChildPath "reference\edge\week31_drive_status_talk_trace.csv"
    if (Test-Path -LiteralPath $week31Ref) {
        $rows31 = @(Get-Content -LiteralPath $week31Ref)
        if ($rows31.Count -gt 1) {
            $metrics.week31_drive_status_rows = $rows31.Count - 1
            $talkingRows = 0
            $txqMax = 0
            foreach ($line in $rows31) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 10) {
                    if ($parts[3] -eq '1') { $talkingRows++ }
                    $txqVal = 0
                    if ([int]::TryParse($parts[7], [ref]$txqVal)) { if ($txqVal -gt $txqMax) { $txqMax = $txqVal } }
                }
            }
            $metrics.week31_drive_status_talking_rows = $talkingRows
            $metrics.week31_drive_status_txq_max = $txqMax
        }
    }
    $week32Ref = Join-Path -Path $repo -ChildPath "reference\edge\week32_drive_dir_stream_trace.csv"
    if (Test-Path -LiteralPath $week32Ref) {
        $rows32 = @(Get-Content -LiteralPath $week32Ref)
        if ($rows32.Count -gt 1) {
            $metrics.week32_drive_dir_rows = $rows32.Count - 1
            $blockBufRows = 0
            $txqMax = 0
            foreach ($line in $rows32) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 11) {
                    if ($parts[6] -eq '1') { $blockBufRows++ }
                    $txqVal = 0
                    if ([int]::TryParse($parts[3], [ref]$txqVal)) { if ($txqVal -gt $txqMax) { $txqMax = $txqVal } }
                }
            }
            $metrics.week32_drive_dir_blockbuf_rows = $blockBufRows
            $metrics.week32_drive_dir_txq_max = $txqMax
        }
    }
    $week33Ref = Join-Path -Path $repo -ChildPath "reference\edge\week33_drive_dir_filter_trace.csv"
    if (Test-Path -LiteralPath $week33Ref) {
        $rows33 = @(Get-Content -LiteralPath $week33Ref)
        if ($rows33.Count -gt 1) {
            $metrics.week33_drive_dir_filter_rows = $rows33.Count - 1
            $negRows = 0
            $txqMax = 0
            foreach ($line in $rows33) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 11) {
                    if ($parts[10] -eq '1') { $negRows++ }
                    $txqVal = 0
                    if ([int]::TryParse($parts[3], [ref]$txqVal)) { if ($txqVal -gt $txqMax) { $txqMax = $txqVal } }
                }
            }
            $metrics.week33_drive_dir_filter_negated_rows = $negRows
            $metrics.week33_drive_dir_filter_txq_max = $txqMax
        }
    }
    $week34Ref = Join-Path -Path $repo -ChildPath "reference\edge\week34_drive_alloc_map_trace.csv"
    if (Test-Path -LiteralPath $week34Ref) {
        $rows34 = @(Get-Content -LiteralPath $week34Ref)
        if ($rows34.Count -gt 1) {
            $metrics.week34_drive_alloc_rows = $rows34.Count - 1
            $failRows = 0
            $blocksFreeMin = 65535
            foreach ($line in $rows34) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 11) {
                    if ($parts[6] -eq '0') { $failRows++ }
                    $freeVal = 0
                    if ([int]::TryParse($parts[8], [ref]$freeVal)) { if ($freeVal -lt $blocksFreeMin) { $blocksFreeMin = $freeVal } }
                }
            }
            if ($blocksFreeMin -eq 65535) { $blocksFreeMin = 0 }
            $metrics.week34_drive_alloc_fail_rows = $failRows
            $metrics.week34_drive_blocks_free_min = $blocksFreeMin
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
