param(
    [string]$Manifest = "external_tests_golden_corpus.json",
    [string]$RealGoldenManifest = "real_golden_manifest.json",
    [string]$FastManifest = "external_tests_golden_corpus_fast.json",
    [string]$Manifest6510 = "external_tests_golden_corpus_6510.json",
    [string]$Manifest8500 = "external_tests_golden_corpus_8500.json",
    [string]$FastManifest6510 = "external_tests_golden_corpus_6510_fast.json",
    [string]$FastManifest8500 = "external_tests_golden_corpus_8500_fast.json",
    [ValidateSet("all", "6510", "8500")]
    [string]$RevisionSlot = "all",
    [int]$KernelMaxHalfCycles = 700000,
    [int]$PureStabilityRuns = 12,
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
        [bool]$NeedWeek46,
        [bool]$NeedWeek47,
        [bool]$NeedWeek48,
        [bool]$NeedWeek49,
        [bool]$NeedWeek50,
        [bool]$NeedWeek51,
        [bool]$NeedWeek52,
        [bool]$NeedWeek53,
        [bool]$NeedWeek54,
        [bool]$NeedWeek55,
        [bool]$NeedWeek56,
        [bool]$NeedWeek57,
        [bool]$NeedWeek58,
        [bool]$NeedWeek59,
        [bool]$NeedWeek60,
        [bool]$NeedWeek61,
        [bool]$NeedWeek62,
        [bool]$NeedWeek63,
        [bool]$NeedWeek64,
        [bool]$NeedWeek65,
        [bool]$NeedWeek66,
        [bool]$NeedWeek67,
        [bool]$NeedWeek68,
        [bool]$NeedWeek69,
        [bool]$NeedWeek70,
        [bool]$NeedWeek71,
        [bool]$NeedWeek72,
        [bool]$NeedWeek73,
        [bool]$NeedWeek74,
        [bool]$NeedWeek75,
        [bool]$NeedWeek76,
        [bool]$NeedWeek77,
        [bool]$NeedWeek78,
        [bool]$NeedWeek79,
        [bool]$NeedWeek80,
        [bool]$NeedWeek81,
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
        if ($NeedWeek46 -and ($text -notmatch "\[WEEK46-IEC\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek47 -and ($text -notmatch "\[WEEK47-HOST\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek48 -and ($text -notmatch "\[WEEK48-CORE\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek49 -and ($text -notmatch "\[WEEK49-CPU\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek50 -and ($text -notmatch "\[WEEK50-CPU\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek51 -and ($text -notmatch "\[WEEK51-VIA\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek52 -and ($text -notmatch "\[WEEK52-VIA\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek53 -and ($text -notmatch "\[WEEK53-INTEG\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek54 -and ($text -notmatch "\[WEEK54-INTEG\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek55 -and ($text -notmatch "\[WEEK55-INTEG\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek56 -and ($text -notmatch "\[WEEK56-PHASE\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek57 -and ($text -notmatch "\[WEEK57-SIGNAL\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek58 -and ($text -notmatch "\[WEEK58-ANALOG\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek59 -and ($text -notmatch "\[WEEK59-ANALOG\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek60 -and ($text -notmatch "\[WEEK60-CONTENTION\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek61 -and ($text -notmatch "\[WEEK61-DOS\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek62 -and ($text -notmatch "\[WEEK62-GCR\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek63 -and ($text -notmatch "\[WEEK63-GCR\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek64 -and ($text -notmatch "\[WEEK64-LAYOUT\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek65 -and ($text -notmatch "\[WEEK65-CRC\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek66 -and ($text -notmatch "\[WEEK66-CRC\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek67 -and ($text -notmatch "\[WEEK67-CRC\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek68 -and ($text -notmatch "\[WEEK68-OWNERSHIP\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek69 -and ($text -notmatch "\[WEEK69-VIA\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek70 -and ($text -notmatch "\[WEEK70-GCR\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek71 -and ($text -notmatch "\[WEEK71-WRITE\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek72 -and ($text -notmatch "\[WEEK72-PHYSICAL\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek73 -and ($text -notmatch "\[WEEK73-ERROR\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek74 -and ($text -notmatch "\[WEEK74-IMAGE\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek75 -and ($text -notmatch "\[WEEK75-COMPAT\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek76 -and ($text -notmatch "\[WEEK76-COMPAT\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek77 -and ($text -notmatch "\[WEEK77-CORPUS\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek78 -and ($text -notmatch "\[WEEK78-REAL\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek79 -and ($text -notmatch "\[WEEK79-HARD\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek80 -and ($text -notmatch "\[WEEK80-READY\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
        if ($NeedWeek81 -and ($text -notmatch "\[WEEK81-FLUX\]\[HARDREF\] PASS")) { return ,@($false, $output, $exitCode) }
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

function Resolve-PathOrThrow {
    param([string]$PathInput, [string]$Label)

    $path = $PathInput
    if (-not [System.IO.Path]::IsPathRooted($path)) {
        $path = Join-Path -Path $repo -ChildPath $path
    }
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing ${Label}: $path"
    }
    return $path
}

function Test-RealGoldenManifest {
    param([string]$ManifestPath)

    $jsonText = Get-Content -LiteralPath $ManifestPath -Raw
    $obj = $jsonText | ConvertFrom-Json
    if ($null -eq $obj) {
        throw "Invalid real golden manifest JSON: $ManifestPath"
    }
    if ($null -eq $obj.titles -or $obj.titles.Count -lt 1) {
        throw "Invalid real golden manifest (missing titles): $ManifestPath"
    }

    foreach ($title in $obj.titles) {
        if ($null -eq $title.path -or [string]::IsNullOrWhiteSpace([string]$title.path)) {
            throw "Invalid real golden manifest title with empty path: $ManifestPath"
        }
        $titlePath = [string]$title.path
        if (-not [System.IO.Path]::IsPathRooted($titlePath)) {
            $titlePath = Join-Path -Path $repo -ChildPath $titlePath
        }
        if (-not (Test-Path -LiteralPath $titlePath)) {
            throw "Real golden title path missing: $titlePath"
        }
    }

    return $obj.titles.Count
}

function Update-MetricsFromEdgeReferences {
    param(
        [System.Collections.IDictionary]$Metrics,
        [string]$RepoPath
    )

    $policyPath = Join-Path -Path $RepoPath -ChildPath "reference\edge\revision_tolerance_policy.json"
    if (-not (Test-Path -LiteralPath $policyPath)) {
        return
    }

    $policyObj = Get-Content -LiteralPath $policyPath -Raw | ConvertFrom-Json
    if ($null -eq $policyObj -or $null -eq $policyObj.metrics) {
        return
    }

    $policyMetricKeys = @($policyObj.metrics.PSObject.Properties | ForEach-Object { $_.Name })
    if ($policyMetricKeys.Count -eq 0) {
        return
    }

    # Ensure all policy-declared metrics are present in the emitted metrics map.
    # This makes new week metrics immediately available without requiring manual
    # hashtable surgery every time a new metric is introduced.
    foreach ($metricKey in $policyMetricKeys) {
        if (-not $Metrics.Contains($metricKey)) {
            $Metrics[$metricKey] = 0
        }
    }

    $edgeDir = Join-Path -Path $RepoPath -ChildPath "reference\edge"
    if (-not (Test-Path -LiteralPath $edgeDir)) {
        return
    }

    $refs = Get-ChildItem -LiteralPath $edgeDir -Filter "week*_*.csv" -File -ErrorAction SilentlyContinue
    foreach ($ref in $refs) {
        $rows = @(Get-Content -LiteralPath $ref.FullName)
        if ($rows.Count -le 1) {
            continue
        }

        $headers = $rows[0].Split(',')
        $headerIndexByMetric = @{}
        for ($i = 0; $i -lt $headers.Count; $i++) {
            $h = $headers[$i].Trim()
            if ($policyMetricKeys -contains $h) {
                $headerIndexByMetric[$h] = $i
            }
        }

        if ($headerIndexByMetric.Count -eq 0) {
            continue
        }

        $aggregates = @{}
        foreach ($metricKey in $headerIndexByMetric.Keys) {
            if ($metricKey -like "*_min") {
                $aggregates[$metricKey] = [int]::MaxValue
            } else {
                $aggregates[$metricKey] = [int]::MinValue
            }
        }

        foreach ($line in $rows[1..($rows.Count - 1)]) {
            $parts = $line.Split(',')
            foreach ($metricKey in $headerIndexByMetric.Keys) {
                $idx = [int]$headerIndexByMetric[$metricKey]
                if ($parts.Count -le $idx) {
                    continue
                }
                $value = 0
                if ([int]::TryParse($parts[$idx], [ref]$value)) {
                    if ($metricKey -like "*_min") {
                        if ($value -lt $aggregates[$metricKey]) {
                            $aggregates[$metricKey] = $value
                        }
                    } else {
                        if ($value -gt $aggregates[$metricKey]) {
                            $aggregates[$metricKey] = $value
                        }
                    }
                }
            }
        }

        foreach ($metricKey in $aggregates.Keys) {
            if ($metricKey -like "*_min") {
                if ($aggregates[$metricKey] -ne [int]::MaxValue) {
                    $Metrics[$metricKey] = $aggregates[$metricKey]
                }
            } else {
                if ($aggregates[$metricKey] -ne [int]::MinValue) {
                    $Metrics[$metricKey] = $aggregates[$metricKey]
                }
            }
        }
    }
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
    week35_drive_ptr_rows = 0
    week35_drive_ptr_blockbuf_rows = 0
    week35_drive_ptr_txq_min = 0
    week36_drive_catalog_rows = 0
    week36_drive_catalog_used_max = 0
    week36_drive_catalog_error_rows = 0
    week37_drive_atn_rows = 0
    week37_drive_atn_blocked_rows = 0
    week37_drive_atn_accept_rows = 0
    week38_drive_talkch_rows = 0
    week38_drive_talkch_close15_count = 0
    week38_drive_talkch_invalid_sa_rows = 0
    week39_drive_cmdresp_rows = 0
    week39_drive_cmdresp_payload_rows = 0
    week39_drive_cmdresp_status_rows = 0
    week40_drive_cmdbuf_rows = 0
    week40_drive_cmdbuf_commit_rows = 0
    week40_drive_cmdbuf_syntax_rows = 0
    week41_drive_close_drop_rows = 0
    week41_drive_close_drop_buffer_rows = 0
    week41_drive_close_drop_execute_rows = 0
    week42_drive_status_rows = 0
    week42_drive_status_74_rows = 0
    week42_drive_status_30_rows = 0
    week43_drive_dirmode_rows = 0
    week43_drive_dirmode_negated_rows = 0
    week43_drive_dirmode_all_mode_rows = 0
    week44_drive_cmdresp_term_rows = 0
    week44_drive_cmdresp_term_payload_rows = 0
    week44_drive_cmdresp_term_status_rows = 0
    week45_drive_final_rows = 0
    week45_drive_final_payload_rows = 0
    week45_drive_final_mem_written_rows = 0
    week46_drive_iec_timing_rows = 0
    week46_drive_iec_eoi_timeout_rows = 0
    week46_drive_iec_eoi_wait_200_guard_rows = 0
    week47_host_timing_rows = 0
    week47_host_aec_low_windows = 0
    week47_host_rdy_low_pulses = 0
    week47_host_dd00_edge_jitter_max = 0
    week48_drive_core_rows = 0
    week48_drive_core_cpu_step_max = 0
    week48_drive_core_via_access_rows = 0
    week48_drive_core_iec_effect_rows = 0
    week49_drive_cpu_rows = 0
    week49_drive_cpu_advance_events_max = 0
    week49_drive_cpu_pc_advance_events_max = 0
    week49_drive_cpu_cadence_gap_max = 0
    week50_drive_opcode_rows = 0
    week50_drive_opcode_branch_cross_rows = 0
    week50_drive_opcode_jsr_rts_stack_rows = 0
    week50_drive_opcode_cadence_gap_max = 0
    week51_via_timer_rows = 0
    week51_via_timer_underflow_rows = 0
    week51_via_timer_irq_assert_rows = 0
    week52_via_shift_rows = 0
    week52_via_shift_irq_rows = 0
    week52_via_shift_edge_count_max = 0
    week53_cpu_via_iec_rows = 0
    week53_cpu_via_iec_cpu_advanced_rows = 0
    week53_cpu_via_iec_irq_overlap_rows = 0
    week53_cpu_via_iec_cadence_gap_max = 0
    week53_drive_iec_phase_rows = 0
    week53_drive_iec_phase_edge_count = 0
    week53_drive_iec_phase_latency_min = 0
    week53_drive_iec_phase_latency_max = 0
    week53_drive_iec_phase_kernel_path_rows = 0
    week54_irq_bridge_rows = 0
    week54_irq_bridge_cpu_advanced_rows = 0
    week54_irq_bridge_via_irq_or_rows = 0
    week54_irq_bridge_iec_tx_served_max = 0
    week54_irq_bridge_cadence_gap_max = 0
    week55_timeout_bridge_rows = 0
    week55_timeout_bridge_eoi_timeout_max = 0
    week55_timeout_bridge_tx_timeout_max = 0
    week55_timeout_bridge_rx_timeout_max = 0
    week55_timeout_bridge_status74_rows = 0
    week55_timeout_bridge_cadence_gap_max = 0
    week56_drive_iec_phase_rows = 0
    week56_drive_iec_phase_edge_count = 0
    week56_drive_iec_phase_latency_min = 0
    week56_drive_iec_phase_latency_max = 0
    week56_drive_iec_phase_kernel_path_rows = 0
    week57_iec_signal_rows = 0
    week57_iec_signal_edge_slew_max = 0
    week57_iec_signal_polarity_mismatch_rows = 0
    week57_iec_signal_turnaround_max = 0
    week57_iec_signal_turnaround_min = 0
    week58_iec_analog_rows = 0
    week58_iec_analog_rise_ticks_max = 0
    week58_iec_analog_fall_ticks_max = 0
    week58_iec_analog_polarity_glitch_rows = 0
    week58_iec_analog_turnaround_max = 0
    week58_iec_analog_turnaround_min = 0
    week59_iec_analog_pulse_rows = 0
    week59_iec_analog_pulse_min_ticks = 0
    week59_iec_analog_pulse_max_ticks = 0
    week59_iec_analog_turnaround_jitter_max = 0
    week59_iec_analog_turnaround_max = 0
    week59_iec_analog_turnaround_min = 0
    week60_iec_contention_rows = 0
    week60_iec_release_latency_max = 0
    week60_iec_illegal_overlap_rows = 0
    week61_drive_dos_semantic_rows = 0
    week61_drive_status_code_mismatch_rows = 0
    week61_drive_cmd_retry_convergence_max = 0
    week62_gcr_sync_detect_rows = 0
    week62_gcr_read_window_jitter_max = 0
    week62_block_crc_error_rows = 0
    week63_gcr_decode_rows = 0
    week63_gcr_illegal_symbol_rows = 0
    week63_gcr_sync_lock_latency_max = 0
    week64_track_sync_density_rows = 0
    week64_gap_class_mismatch_rows = 0
    week64_header_data_boundary_errors = 0
    week65_crc_ok_rows = 0
    week65_crc_error_rows = 0
    week65_retry_recovery_convergence_max = 0
    week66_crc_status_latch_rows = 0
    week66_retry_backoff_span_max = 0
    week66_channel15_clear_latency_max = 0
    week67_crc_error_class_rows = 0
    week67_channel15_error_class_mismatch_rows = 0
    week67_load_recovery_profile_max = 0
    w68_cpu_owned_cmd_rows = 0
    w68_scaffold_fallback_rows = 0
    w68_cmd_status_divergence_rows = 0
    w70_gcr_header_decode_rows = 0
    w70_gcr_data_decode_rows = 0
    w70_gcr_chain_break_rows = 0
    w71_raw_roundtrip_ok_rows = 0
    w71_write_verify_fail_rows = 0
    w71_postwrite_retry_max = 0
    w72_zone_timing_span_max = 0
    w72_bitslip_events_rows = 0
    w72_weakbit_observed_rows = 0
    w73_errorclass_coverage_rows = 0
    w73_channel15_mapping_mismatch_rows = 0
    w73_recovery_profile_max = 0
    w74_g64_feature_parity_rows = 0
    w74_nib_parity_rows = 0
    w74_image_lossy_transform_rows = 0
    w75_real_corpus_pass_rate = 0
    w75_loader_timing_regressions = 0
    w75_host_fallback_rows = 0
    w76_cross_profile_stability_rate = 0
    w76_loader_drift_regressions = 0
    w76_host_fallback_rows = 0
    w77_real_corpus_coverage_rows = 0
    w77_loader_timing_regressions = 0
    w77_host_fallback_rows = 0
    w78_real_corpus_coverage_rows = 0
    w78_loader_timing_regressions = 0
    w78_host_fallback_rows = 0
    w78_format_coverage_rows = 0
    w79_real_hard_corpus_rows = 0
    w79_loader_timing_regressions = 0
    w79_host_fallback_rows = 0
    w79_hard_format_pass_rows = 0
    w79_raw_flux_coverage_rows = 0
    w80_mustpass_rows = 0
    w80_flux_ingest_behavior_rows = 0
    w80_flux_weak_halftrack_rows = 0
    w80_flux_syncloss_rows = 0
    w80_write_e2e_rows = 0
    w80_error_dos_parity_rows = 0
    w80_soak_batch_pass_rows = 0
    w80_release_checklist_rows = 0
    w80_host_fallback_rows = 0
    w81_flux_samples = 0
    w81_weak_halftrack_observed_rows = 0
    w81_syncloss_recovery_rows = 0
    w81_loader_timing_parity_runs = 0
    w81_loader_timing_parity_mismatch_rows = 0
    w81_flux_behavior_pass_rows = 0
    week54_pure_stability_runs = 0
    week54_pure_stability_pass_runs = 0
    week54_pure_stability_host_fallback_no = 0
}

$savedPath = $env:PATH
try {
    $env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $env:PATH

    $realGoldenManifestPath = Resolve-PathOrThrow -PathInput $RealGoldenManifest -Label "real golden manifest"
    $realGoldenTitles = Test-RealGoldenManifest -ManifestPath $realGoldenManifestPath

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
        $r = Run-Binary -ExePath "$repo\c64_11_fast_signoff.exe" -ManifestPath $fastManifestPath -NeedWeek45:$false -NeedWeek46:$false -NeedWeek47:$false -NeedWeek48:$false -NeedWeek49:$false -NeedWeek50:$false -NeedWeek51:$false -NeedWeek52:$false -NeedWeek53:$false -NeedWeek54:$false -NeedWeek55:$false -NeedWeek56:$false -NeedWeek57:$false -NeedWeek58:$false -NeedWeek59:$false -NeedWeek60:$false -NeedWeek61:$false -NeedWeek62:$false -NeedWeek63:$false -NeedWeek64:$false -NeedWeek65:$false -NeedWeek66:$false -NeedWeek67:$false -NeedWeek68:$false -NeedWeek69:$false -NeedWeek70:$false -NeedWeek71:$false -NeedWeek72:$false -NeedWeek73:$false -NeedWeek74:$false -NeedWeek75:$false -NeedWeek76:$false -NeedWeek77:$false -NeedWeek78:$false -NeedWeek79:$false -NeedWeek80:$false -NeedWeek81:$false -NeedWeek12:$false -NeedExternal:$true -NeedNoFallback:$true -ExtraEnv $null
        $script:__runFast = $r
        if (-not $r[0]) { foreach ($line in $r[1]) { $line }; exit $r[2] }

        if ($RevisionSlot -ne "8500") {
            $fast6510 = Resolve-ManifestPath -ManifestInput $FastManifest6510 -FallbackManifestInput $Manifest
            $r6510 = Run-Binary -ExePath "$repo\c64_11_fast_signoff.exe" -ManifestPath $fast6510 -NeedWeek45:$false -NeedWeek46:$false -NeedWeek47:$false -NeedWeek48:$false -NeedWeek49:$false -NeedWeek50:$false -NeedWeek51:$false -NeedWeek52:$false -NeedWeek53:$false -NeedWeek54:$false -NeedWeek55:$false -NeedWeek56:$false -NeedWeek57:$false -NeedWeek58:$false -NeedWeek59:$false -NeedWeek60:$false -NeedWeek61:$false -NeedWeek62:$false -NeedWeek63:$false -NeedWeek64:$false -NeedWeek65:$false -NeedWeek66:$false -NeedWeek67:$false -NeedWeek68:$false -NeedWeek69:$false -NeedWeek70:$false -NeedWeek71:$false -NeedWeek72:$false -NeedWeek73:$false -NeedWeek74:$false -NeedWeek75:$false -NeedWeek76:$false -NeedWeek77:$false -NeedWeek78:$false -NeedWeek79:$false -NeedWeek80:$false -NeedWeek81:$false -NeedWeek12:$false -NeedExternal:$true -NeedNoFallback:$true -ExtraEnv @{ C64_CPU_REVISION = '6510'; C64_VIC_REVISION = '6569'; C64_CIA_REVISION = '6526'; C64_OPENBUS_REVISION = 'nmos'; C64_DRIVE_REVISION = '1541' }
            $metrics.fast_6510_exit = [int]$r6510[2]
            if (-not $r6510[0]) { foreach ($line in $r6510[1]) { $line }; exit $r6510[2] }
        }

        if ($RevisionSlot -ne "6510") {
            $fast8500 = Resolve-ManifestPath -ManifestInput $FastManifest8500 -FallbackManifestInput $Manifest
            $r8500 = Run-Binary -ExePath "$repo\c64_11_fast_signoff.exe" -ManifestPath $fast8500 -NeedWeek45:$false -NeedWeek46:$false -NeedWeek47:$false -NeedWeek48:$false -NeedWeek49:$false -NeedWeek50:$false -NeedWeek51:$false -NeedWeek52:$false -NeedWeek53:$false -NeedWeek54:$false -NeedWeek55:$false -NeedWeek56:$false -NeedWeek57:$false -NeedWeek58:$false -NeedWeek59:$false -NeedWeek60:$false -NeedWeek61:$false -NeedWeek62:$false -NeedWeek63:$false -NeedWeek64:$false -NeedWeek65:$false -NeedWeek66:$false -NeedWeek67:$false -NeedWeek68:$false -NeedWeek69:$false -NeedWeek70:$false -NeedWeek71:$false -NeedWeek72:$false -NeedWeek73:$false -NeedWeek74:$false -NeedWeek75:$false -NeedWeek76:$false -NeedWeek77:$false -NeedWeek78:$false -NeedWeek79:$false -NeedWeek80:$false -NeedWeek81:$false -NeedWeek12:$false -NeedExternal:$true -NeedNoFallback:$true -ExtraEnv @{ C64_CPU_REVISION = '8500'; C64_VIC_REVISION = '8565'; C64_CIA_REVISION = '6526A'; C64_OPENBUS_REVISION = 'hmos'; C64_DRIVE_REVISION = '1541C' }
            $metrics.fast_8500_exit = [int]$r8500[2]
            if (-not $r8500[0]) { foreach ($line in $r8500[1]) { $line }; exit $r8500[2] }
        }
    } -Assert { param($o, $e) $e -eq 0 }

    $results += Invoke-Step -Name "run-strict" -Action {
        $r = Run-Binary -ExePath "$repo\c64_11_strict_signoff.exe" -ManifestPath $Manifest -NeedWeek45:$true -NeedWeek46:$true -NeedWeek47:$true -NeedWeek48:$true -NeedWeek49:$true -NeedWeek50:$true -NeedWeek51:$true -NeedWeek52:$true -NeedWeek53:$true -NeedWeek54:$true -NeedWeek55:$true -NeedWeek56:$true -NeedWeek57:$true -NeedWeek58:$true -NeedWeek59:$true -NeedWeek60:$true -NeedWeek61:$true -NeedWeek62:$true -NeedWeek63:$true -NeedWeek64:$true -NeedWeek65:$true -NeedWeek66:$true -NeedWeek67:$true -NeedWeek68:$true -NeedWeek69:$true -NeedWeek70:$true -NeedWeek71:$true -NeedWeek72:$true -NeedWeek73:$true -NeedWeek74:$true -NeedWeek75:$true -NeedWeek76:$true -NeedWeek77:$true -NeedWeek78:$true -NeedWeek79:$true -NeedWeek80:$true -NeedWeek81:$true -NeedWeek12:$true -NeedExternal:$true -NeedNoFallback:$true -ExtraEnv $null
        $script:__runStrict = $r
        if (-not $r[0]) { foreach ($line in $r[1]) { $line }; exit $r[2] }

        if ($RevisionSlot -ne "8500") {
            $strict6510 = Resolve-ManifestPath -ManifestInput $Manifest6510 -FallbackManifestInput $Manifest
            $r6510 = Run-Binary -ExePath "$repo\c64_11_strict_signoff.exe" -ManifestPath $strict6510 -NeedWeek45:$true -NeedWeek46:$true -NeedWeek47:$true -NeedWeek48:$true -NeedWeek49:$true -NeedWeek50:$true -NeedWeek51:$true -NeedWeek52:$true -NeedWeek53:$true -NeedWeek54:$true -NeedWeek55:$true -NeedWeek56:$true -NeedWeek57:$true -NeedWeek58:$true -NeedWeek59:$true -NeedWeek60:$true -NeedWeek61:$true -NeedWeek62:$true -NeedWeek63:$true -NeedWeek64:$true -NeedWeek65:$true -NeedWeek66:$true -NeedWeek67:$true -NeedWeek68:$true -NeedWeek69:$true -NeedWeek70:$true -NeedWeek71:$true -NeedWeek72:$true -NeedWeek73:$true -NeedWeek74:$true -NeedWeek75:$true -NeedWeek76:$true -NeedWeek77:$true -NeedWeek78:$true -NeedWeek79:$true -NeedWeek80:$true -NeedWeek81:$true -NeedWeek12:$true -NeedExternal:$true -NeedNoFallback:$true -ExtraEnv @{ C64_CPU_REVISION = '6510'; C64_VIC_REVISION = '6569'; C64_CIA_REVISION = '6526'; C64_OPENBUS_REVISION = 'nmos'; C64_DRIVE_REVISION = '1541' }
            $metrics.strict_6510_exit = [int]$r6510[2]
            if (-not $r6510[0]) { foreach ($line in $r6510[1]) { $line }; exit $r6510[2] }
        }

        if ($RevisionSlot -ne "6510") {
            $strict8500 = Resolve-ManifestPath -ManifestInput $Manifest8500 -FallbackManifestInput $Manifest
            $r8500 = Run-Binary -ExePath "$repo\c64_11_strict_signoff.exe" -ManifestPath $strict8500 -NeedWeek45:$true -NeedWeek46:$true -NeedWeek47:$true -NeedWeek48:$true -NeedWeek49:$true -NeedWeek50:$true -NeedWeek51:$true -NeedWeek52:$true -NeedWeek53:$true -NeedWeek54:$true -NeedWeek55:$true -NeedWeek56:$true -NeedWeek57:$true -NeedWeek58:$true -NeedWeek59:$true -NeedWeek60:$true -NeedWeek61:$true -NeedWeek62:$true -NeedWeek63:$true -NeedWeek64:$true -NeedWeek65:$true -NeedWeek66:$true -NeedWeek67:$true -NeedWeek68:$true -NeedWeek69:$true -NeedWeek70:$true -NeedWeek71:$true -NeedWeek72:$true -NeedWeek73:$true -NeedWeek74:$true -NeedWeek75:$true -NeedWeek76:$true -NeedWeek77:$true -NeedWeek78:$true -NeedWeek79:$true -NeedWeek80:$true -NeedWeek81:$true -NeedWeek12:$true -NeedExternal:$true -NeedNoFallback:$true -ExtraEnv @{ C64_CPU_REVISION = '8500'; C64_VIC_REVISION = '8565'; C64_CIA_REVISION = '6526A'; C64_OPENBUS_REVISION = 'hmos'; C64_DRIVE_REVISION = '1541C' }
            $metrics.strict_8500_exit = [int]$r8500[2]
            if (-not $r8500[0]) { foreach ($line in $r8500[1]) { $line }; exit $r8500[2] }
        }
    } -Assert { param($o, $e) $e -eq 0 }

    $results += Invoke-Step -Name "run-full" -Action {
        $r = Run-Binary -ExePath "$repo\c64_11_full_signoff.exe" -ManifestPath $Manifest -NeedWeek45:$true -NeedWeek46:$true -NeedWeek47:$true -NeedWeek48:$true -NeedWeek49:$true -NeedWeek50:$true -NeedWeek51:$true -NeedWeek52:$true -NeedWeek53:$true -NeedWeek54:$true -NeedWeek55:$true -NeedWeek56:$true -NeedWeek57:$true -NeedWeek58:$true -NeedWeek59:$true -NeedWeek60:$true -NeedWeek61:$true -NeedWeek62:$true -NeedWeek63:$true -NeedWeek64:$true -NeedWeek65:$true -NeedWeek66:$true -NeedWeek67:$true -NeedWeek68:$true -NeedWeek69:$true -NeedWeek70:$true -NeedWeek71:$true -NeedWeek72:$true -NeedWeek73:$true -NeedWeek74:$true -NeedWeek75:$true -NeedWeek76:$true -NeedWeek77:$true -NeedWeek78:$true -NeedWeek79:$true -NeedWeek80:$true -NeedWeek81:$true -NeedWeek12:$true -NeedExternal:$false -NeedNoFallback:$false -ExtraEnv $null
        $script:__runFull = $r
        if (-not $r[0]) { foreach ($line in $r[1]) { $line }; exit $r[2] }
    } -Assert { param($o, $e) $e -eq 0 }

    Copy-Item -LiteralPath "$repo\c64_11_fast_signoff.exe" -Destination "$repo\c64_11.exe" -Force

    $results += Invoke-Step -Name "run-pure" -Action {
        & "$repo\run_kernel_iec_e2e.ps1" -Mode pure -MaxHalfCycles $KernelMaxHalfCycles -Repeat $PureStabilityRuns -Quiet
    } -Assert {
        param($o, $e)
        if ($e -ne 0) { return $false }
        $txt = ($o | Out-String)
        return ($txt -match "\[RUNNER\] mode=pure" -and $txt -match "pass=True" -and $txt -match "host_fallback_no=True")
    }

    $results += Invoke-Step -Name "run-compat" -Action {
        & "$repo\run_kernel_iec_e2e.ps1" -Mode compat -MaxHalfCycles $KernelMaxHalfCycles -Quiet -EnableCompatClockAssist -EnableCompatRamSinkInject -EnableCompatRamSinkBulk -EnableReplayCiaLog -EnableDd00Trace -EnableDriveAutoTalkDir -EnableDriveAutoDirOnTalk0 -EnableDriveForceTalkOnDd0d8 -IecPolarity "0,0,0,0,0,0,1,0,1,1"
    } -Assert {
        param($o, $e)
        if ($e -ne 0) { return $false }
        $txt = ($o | Out-String)
        return ($txt -match "\[RUNNER\] mode=compat" -and $txt -match "pass=True")
    }

    $pureResult = $results | Where-Object { $_.Name -eq "run-pure" } | Select-Object -First 1
    if ($pureResult -ne $null) {
        $pureText = ($pureResult.Output | Out-String)
        $passRuns = 0
        $totalRuns = 0
        if ($pureText -match 'pass_runs=([0-9]+)/([0-9]+)') {
            $passRuns = [int]$matches[1]
            $totalRuns = [int]$matches[2]
        }
        $metrics.week54_pure_stability_runs = $totalRuns
        $metrics.week54_pure_stability_pass_runs = $passRuns
        if ($pureText -match 'host_fallback_no=True') {
            $metrics.week54_pure_stability_host_fallback_no = 1
        } else {
            $metrics.week54_pure_stability_host_fallback_no = 0
        }
    }

    "[SIGNOFF] ----------------------------------------"
    "[SIGNOFF] Week13-14 status: PASS"
    "[SIGNOFF] strict/full/fast: green"
    "[SIGNOFF] pure/compat: green"
    "[SIGNOFF] drift test: stable ([WEEK45 TIME] PASS)"
    "[SIGNOFF] week46 IEC timing-grade hard-ref: PASS"
    "[SIGNOFF] week47 host timing stabilization hard-ref: PASS"
    "[SIGNOFF] week48 drive core timing baseline hard-ref: PASS"
    "[SIGNOFF] week49 drive CPU cadence hard-ref: PASS"
    "[SIGNOFF] week50 drive CPU opcode timing hard-ref: PASS"
    "[SIGNOFF] week51 VIA timer/IRQ hard-ref: PASS"
    "[SIGNOFF] week52 VIA shift edge/latch hard-ref: PASS"
    "[SIGNOFF] week53 CPU<->VIA<->IEC integration hard-ref: PASS"
    "[SIGNOFF] week54 CPU<->VIA<->IEC IRQ bridge hard-ref: PASS"
    "[SIGNOFF] week55 CPU<->VIA<->IEC timeout bridge hard-ref: PASS"
    "[SIGNOFF] week56 drive IEC phase-map hard-ref: PASS"
    "[SIGNOFF] week57 IEC signal window hard-ref: PASS"
    "[SIGNOFF] week58 IEC analog-aware edge-model hard-ref: PASS"
    "[SIGNOFF] week59 IEC analog pulse-window hard-ref: PASS"
    "[SIGNOFF] week60 IEC contention/release hard-ref: PASS"
    "[SIGNOFF] week61 drive DOS semantic hard-ref: PASS"
    "[SIGNOFF] week62 disk-fidelity GCR bootstrap hard-ref: PASS"
    "[SIGNOFF] week63 GCR decode-path hard-ref: PASS"
    "[SIGNOFF] week64 track-layout realism hard-ref: PASS"
    "[SIGNOFF] week65 CRC/ECC behavior + error-map hard-ref: PASS"
    "[SIGNOFF] week66 CRC status-latch + clear-latency hard-ref: PASS"
    "[SIGNOFF] week67 CRC error-class map + recovery profile hard-ref: PASS"
    "[SIGNOFF] week68 drive CPU ownership cutover hard-ref: PASS"
    "[SIGNOFF] week69 VIA timing-grade stress hard-ref: PASS"
    "[SIGNOFF] week70 GCR read pipeline full-chain hard-ref: PASS"
    "[SIGNOFF] week71 GCR write path + read-after-write hard-ref: PASS"
    "[SIGNOFF] week72 physical disk effects model hard-ref: PASS"
    "[SIGNOFF] week73 error engine + DOS mapping hard-ref: PASS"
    "[SIGNOFF] week74 image fidelity parity (G64/NIB) hard-ref: PASS"
    "[SIGNOFF] week75 compatibility signoff (real software corpus) hard-ref: PASS"
    "[SIGNOFF] week76 compatibility drift envelope hard-ref: PASS"
    "[SIGNOFF] week77 real corpus bridge hard-ref: PASS"
    "[SIGNOFF] week78 real disk corpus hard-ref: PASS"
    "[SIGNOFF] week79 real hard corpus (G64/NIB/D64/RAW) hard-ref: PASS"
    "[SIGNOFF] week80 release-readiness closure hard-ref: PASS"
    "[SIGNOFF] week81 flux ingest/decode/replay behavior parity hard-ref: PASS"
    "[SIGNOFF] 1541 physical-grade beta: PASS"
    "[SIGNOFF] interrupt boundary: zero mismatch ([WEEK12] PASS)"
    "[SIGNOFF] no hidden fallback: enforced (host_fallback=no)"
    "[SIGNOFF] strict/full manifest: $Manifest"
    "[SIGNOFF] real golden manifest: $realGoldenManifestPath"
    "[SIGNOFF] real golden titles: $realGoldenTitles"
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
    $week35Ref = Join-Path -Path $repo -ChildPath "reference\edge\week35_drive_ptr_dir_trace.csv"
    if (Test-Path -LiteralPath $week35Ref) {
        $rows35 = @(Get-Content -LiteralPath $week35Ref)
        if ($rows35.Count -gt 1) {
            $metrics.week35_drive_ptr_rows = $rows35.Count - 1
            $blockBufRows = 0
            $txqMin = 65535
            foreach ($line in $rows35) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 11) {
                    if ($parts[8] -eq '1') { $blockBufRows++ }
                    $txqVal = 0
                    if ([int]::TryParse($parts[6], [ref]$txqVal)) { if ($txqVal -lt $txqMin) { $txqMin = $txqVal } }
                }
            }
            if ($txqMin -eq 65535) { $txqMin = 0 }
            $metrics.week35_drive_ptr_blockbuf_rows = $blockBufRows
            $metrics.week35_drive_ptr_txq_min = $txqMin
        }
    }
    $week36Ref = Join-Path -Path $repo -ChildPath "reference\edge\week36_drive_catalog_trace.csv"
    if (Test-Path -LiteralPath $week36Ref) {
        $rows36 = @(Get-Content -LiteralPath $week36Ref)
        if ($rows36.Count -gt 1) {
            $metrics.week36_drive_catalog_rows = $rows36.Count - 1
            $usedMax = 0
            $errorRows = 0
            foreach ($line in $rows36) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 11) {
                    $usedVal = 0
                    if ([int]::TryParse($parts[4], [ref]$usedVal)) { if ($usedVal -gt $usedMax) { $usedMax = $usedVal } }
                    if ($parts[10] -like '65,*') { $errorRows++ }
                }
            }
            $metrics.week36_drive_catalog_used_max = $usedMax
            $metrics.week36_drive_catalog_error_rows = $errorRows
        }
    }
    $week37Ref = Join-Path -Path $repo -ChildPath "reference\edge\week37_drive_atn_gate_trace.csv"
    if (Test-Path -LiteralPath $week37Ref) {
        $rows37 = @(Get-Content -LiteralPath $week37Ref)
        if ($rows37.Count -gt 1) {
            $metrics.week37_drive_atn_rows = $rows37.Count - 1
            $blockedRows = 0
            $acceptRows = 0
            foreach ($line in $rows37) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 11) {
                    $phase = $parts[1]
                    $ok = $parts[3]
                    if (($phase -like '*blocked*') -and ($ok -eq '0')) { $blockedRows++ }
                    if (($phase -like '*ok*' -or $phase -eq 'talk_sa0') -and ($ok -eq '1')) { $acceptRows++ }
                }
            }
            $metrics.week37_drive_atn_blocked_rows = $blockedRows
            $metrics.week37_drive_atn_accept_rows = $acceptRows
        }
    }
    $week38Ref = Join-Path -Path $repo -ChildPath "reference\edge\week38_drive_talkch_close_trace.csv"
    if (Test-Path -LiteralPath $week38Ref) {
        $rows38 = @(Get-Content -LiteralPath $week38Ref)
        if ($rows38.Count -gt 1) {
            $metrics.week38_drive_talkch_rows = $rows38.Count - 1
            $closeCountMax = 0
            $invalidSaRows = 0
            foreach ($line in $rows38) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 10) {
                    $phase = $parts[1]
                    $opOk = $parts[3]
                    $closeVal = 0
                    if ([int]::TryParse($parts[7], [ref]$closeVal)) { if ($closeVal -gt $closeCountMax) { $closeCountMax = $closeVal } }
                    if (($phase -eq 'talk_sa2_invalid') -and ($opOk -eq '1')) { $invalidSaRows++ }
                }
            }
            $metrics.week38_drive_talkch_close15_count = $closeCountMax
            $metrics.week38_drive_talkch_invalid_sa_rows = $invalidSaRows
        }
    }
    $week39Ref = Join-Path -Path $repo -ChildPath "reference\edge\week39_drive_cmdresp_fallback_trace.csv"
    if (Test-Path -LiteralPath $week39Ref) {
        $rows39 = @(Get-Content -LiteralPath $week39Ref)
        if ($rows39.Count -gt 1) {
            $metrics.week39_drive_cmdresp_rows = $rows39.Count - 1
            $payloadRows = 0
            $statusRows = 0
            foreach ($line in $rows39) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 11) {
                    $phase = $parts[1]
                    if ($phase -like '*response*') { $payloadRows++ }
                    if ($phase -like '*status*') { $statusRows++ }
                }
            }
            $metrics.week39_drive_cmdresp_payload_rows = $payloadRows
            $metrics.week39_drive_cmdresp_status_rows = $statusRows
        }
    }
    $week40Ref = Join-Path -Path $repo -ChildPath "reference\edge\week40_drive_cmdbuf_commit_trace.csv"
    if (Test-Path -LiteralPath $week40Ref) {
        $rows40 = @(Get-Content -LiteralPath $week40Ref)
        if ($rows40.Count -gt 1) {
            $metrics.week40_drive_cmdbuf_rows = $rows40.Count - 1
            $commitRows = 0
            $syntaxRows = 0
            foreach ($line in $rows40) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 14) {
                    $phase = $parts[1]
                    if ($phase -like 'execute_*') { $commitRows++ }
                    if (($phase -eq 'execute_invalid_unlisten') -and ($parts[10] -eq '0')) { $syntaxRows++ }
                }
            }
            $metrics.week40_drive_cmdbuf_commit_rows = $commitRows
            $metrics.week40_drive_cmdbuf_syntax_rows = $syntaxRows
        }
    }
    $week41Ref = Join-Path -Path $repo -ChildPath "reference\edge\week41_drive_close15_drop_trace.csv"
    if (Test-Path -LiteralPath $week41Ref) {
        $rows41 = @(Get-Content -LiteralPath $week41Ref)
        if ($rows41.Count -gt 1) {
            $metrics.week41_drive_close_drop_rows = $rows41.Count - 1
            $bufferRows = 0
            $executeRows = 0
            foreach ($line in $rows41) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 12) {
                    $phase = $parts[1]
                    if (($phase -like 'data_buffered_*') -and ($parts[6] -eq '17')) { $bufferRows++ }
                    if (($phase -eq 'unlisten_execute') -and ($parts[10] -eq '171')) { $executeRows++ }
                }
            }
            $metrics.week41_drive_close_drop_buffer_rows = $bufferRows
            $metrics.week41_drive_close_drop_execute_rows = $executeRows
        }
    }
    $week42Ref = Join-Path -Path $repo -ChildPath "reference\edge\week42_drive_status_rebuild_trace.csv"
    if (Test-Path -LiteralPath $week42Ref) {
        $rows42 = @(Get-Content -LiteralPath $week42Ref)
        if ($rows42.Count -gt 1) {
            $metrics.week42_drive_status_rows = $rows42.Count - 1
            $rows74 = 0
            $rows30 = 0
            foreach ($line in $rows42) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 9) {
                    $phase = $parts[1]
                    if ($phase -eq 'talk_sa15_status74') { $rows74++ }
                    if ($phase -eq 'talk_sa15_status30') { $rows30++ }
                }
            }
            $metrics.week42_drive_status_74_rows = $rows74
            $metrics.week42_drive_status_30_rows = $rows30
        }
    }
    $week43Ref = Join-Path -Path $repo -ChildPath "reference\edge\week43_drive_dirmode_trace.csv"
    if (Test-Path -LiteralPath $week43Ref) {
        $rows43 = @(Get-Content -LiteralPath $week43Ref)
        if ($rows43.Count -gt 1) {
            $metrics.week43_drive_dirmode_rows = $rows43.Count - 1
            $negRows = 0
            $allModeRows = 0
            foreach ($line in $rows43) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 10) {
                    $phase = $parts[1]
                    if (($phase -eq 'mode_not_w') -and ($parts[8] -eq '1')) { $negRows++ }
                    if (($phase -eq 'mode_all') -and ($parts[7] -eq '1')) { $allModeRows++ }
                }
            }
            $metrics.week43_drive_dirmode_negated_rows = $negRows
            $metrics.week43_drive_dirmode_all_mode_rows = $allModeRows
        }
    }
    $week44Ref = Join-Path -Path $repo -ChildPath "reference\edge\week44_drive_cmdresp_term_trace.csv"
    if (Test-Path -LiteralPath $week44Ref) {
        $rows44 = @(Get-Content -LiteralPath $week44Ref)
        if ($rows44.Count -gt 1) {
            $metrics.week44_drive_cmdresp_term_rows = $rows44.Count - 1
            $payloadRows = 0
            $statusRows = 0
            foreach ($line in $rows44) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 10) {
                    $phase = $parts[1]
                    if ($phase -eq 'talk_sa15_resp') { $payloadRows++ }
                    if ($phase -eq 'talk_sa15_again_status') { $statusRows++ }
                }
            }
            $metrics.week44_drive_cmdresp_term_payload_rows = $payloadRows
            $metrics.week44_drive_cmdresp_term_status_rows = $statusRows
        }
    }
    $week45Ref = Join-Path -Path $repo -ChildPath "reference\edge\week45_drive_final_freeze_trace.csv"
    if (Test-Path -LiteralPath $week45Ref) {
        $rows45 = @(Get-Content -LiteralPath $week45Ref)
        if ($rows45.Count -gt 1) {
            $metrics.week45_drive_final_rows = $rows45.Count - 1
            $payloadRows = 0
            $memRows = 0
            foreach ($line in $rows45) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 12) {
                    $phase = $parts[1]
                    if ($phase -eq 'talk_sa15_payload') { $payloadRows++ }
                    if ($parts[7] -eq '170' -and $parts[8] -eq '85') { $memRows++ }
                }
            }
            $metrics.week45_drive_final_payload_rows = $payloadRows
            $metrics.week45_drive_final_mem_written_rows = $memRows
        }
    }
    $week46Ref = Join-Path -Path $repo -ChildPath "reference\edge\week46_drive_iec_timing_grade_trace.csv"
    if (Test-Path -LiteralPath $week46Ref) {
        $rows46 = @(Get-Content -LiteralPath $week46Ref)
        if ($rows46.Count -gt 1) {
            $metrics.week46_drive_iec_timing_rows = $rows46.Count - 1
            $eoiTimeoutRows = 0
            $eoiWaitGuardRows = 0
            foreach ($line in $rows46) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 12) {
                    $phase = $parts[1]
                    if (($phase -eq 'eoi_timeout_cross') -and ($parts[6] -eq '1')) { $eoiTimeoutRows++ }
                    if (($phase -eq 'eoi_wait_200us_guard') -and ($parts[6] -eq '0')) { $eoiWaitGuardRows++ }
                }
            }
            $metrics.week46_drive_iec_eoi_timeout_rows = $eoiTimeoutRows
            $metrics.week46_drive_iec_eoi_wait_200_guard_rows = $eoiWaitGuardRows
        }
    }
    $week47Ref = Join-Path -Path $repo -ChildPath "reference\edge\week47_host_timing_trace.csv"
    if (Test-Path -LiteralPath $week47Ref) {
        $rows47 = @(Get-Content -LiteralPath $week47Ref)
        if ($rows47.Count -gt 1) {
            $metrics.week47_host_timing_rows = $rows47.Count - 1
            $aecWindows = 0
            $rdyPulses = 0
            $jitterMax = 0
            foreach ($line in $rows47) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 12) {
                    $aecVal = 0
                    $rdyVal = 0
                    $jitVal = 0
                    if ([int]::TryParse($parts[8], [ref]$aecVal)) { if ($aecVal -gt $aecWindows) { $aecWindows = $aecVal } }
                    if ([int]::TryParse($parts[9], [ref]$rdyVal)) { if ($rdyVal -gt $rdyPulses) { $rdyPulses = $rdyVal } }
                    if ([int]::TryParse($parts[7], [ref]$jitVal)) { if ($jitVal -gt $jitterMax) { $jitterMax = $jitVal } }
                }
            }
            $metrics.week47_host_aec_low_windows = $aecWindows
            $metrics.week47_host_rdy_low_pulses = $rdyPulses
            $metrics.week47_host_dd00_edge_jitter_max = $jitterMax
        }
    }
    $week48Ref = Join-Path -Path $repo -ChildPath "reference\edge\week48_drive_core_timing_trace.csv"
    if (Test-Path -LiteralPath $week48Ref) {
        $rows48 = @(Get-Content -LiteralPath $week48Ref)
        if ($rows48.Count -gt 1) {
            $metrics.week48_drive_core_rows = $rows48.Count - 1
            $cpuStepMax = 0
            $viaAccessRows = 0
            $iecEffectRows = 0
            foreach ($line in $rows48) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 11) {
                    $cpuVal = 0
                    if ([int]::TryParse($parts[3], [ref]$cpuVal)) { if ($cpuVal -gt $cpuStepMax) { $cpuStepMax = $cpuVal } }
                    if ($parts[6] -ne 'none') { $viaAccessRows++ }
                    if (($parts[9] -ne '0') -and ($parts[9] -ne '')) { $iecEffectRows++ }
                }
            }
            $metrics.week48_drive_core_cpu_step_max = $cpuStepMax
            $metrics.week48_drive_core_via_access_rows = $viaAccessRows
            $metrics.week48_drive_core_iec_effect_rows = $iecEffectRows
        }
    }
    $week49Ref = Join-Path -Path $repo -ChildPath "reference\edge\week49_drive_cpu_cadence_trace.csv"
    if (Test-Path -LiteralPath $week49Ref) {
        $rows49 = @(Get-Content -LiteralPath $week49Ref)
        if ($rows49.Count -gt 1) {
            $metrics.week49_drive_cpu_rows = $rows49.Count - 1
            $cpuAdvanceMax = 0
            $pcAdvanceMax = 0
            $cadenceGapMax = 0
            foreach ($line in $rows49) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 15) {
                    $cpuAdv = 0
                    $pcAdv = 0
                    $gap = 0
                    if ([int]::TryParse($parts[13], [ref]$cpuAdv)) { if ($cpuAdv -gt $cpuAdvanceMax) { $cpuAdvanceMax = $cpuAdv } }
                    if ([int]::TryParse($parts[14], [ref]$pcAdv)) { if ($pcAdv -gt $pcAdvanceMax) { $pcAdvanceMax = $pcAdv } }
                    if ([int]::TryParse($parts[12], [ref]$gap)) { if ($gap -gt $cadenceGapMax) { $cadenceGapMax = $gap } }
                }
            }
            $metrics.week49_drive_cpu_advance_events_max = $cpuAdvanceMax
            $metrics.week49_drive_cpu_pc_advance_events_max = $pcAdvanceMax
            $metrics.week49_drive_cpu_cadence_gap_max = $cadenceGapMax
        }
    }
    $week50Ref = Join-Path -Path $repo -ChildPath "reference\edge\week50_drive_cpu_opcode_timing_trace.csv"
    if (Test-Path -LiteralPath $week50Ref) {
        $rows50 = @(Get-Content -LiteralPath $week50Ref)
        if ($rows50.Count -gt 1) {
            $metrics.week50_drive_opcode_rows = $rows50.Count - 1
            $branchCrossRows = 0
            $jsrRtsRows = 0
            $gapMax = 0
            foreach ($line in $rows50) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 15) {
                    if ($parts[1] -eq 'branch_taken_cross') { $branchCrossRows++ }
                    if (($parts[1] -eq 'jsr_rts') -and (($parts[12] -ne '0') -or ($parts[13] -ne '0'))) { $jsrRtsRows++ }
                    $gap = 0
                    if ([int]::TryParse($parts[14], [ref]$gap)) { if ($gap -gt $gapMax) { $gapMax = $gap } }
                }
            }
            $metrics.week50_drive_opcode_branch_cross_rows = $branchCrossRows
            $metrics.week50_drive_opcode_jsr_rts_stack_rows = $jsrRtsRows
            $metrics.week50_drive_opcode_cadence_gap_max = $gapMax
        }
    }
    $week51Ref = Join-Path -Path $repo -ChildPath "reference\edge\week51_via_timer_irq_trace.csv"
    if (Test-Path -LiteralPath $week51Ref) {
        $rows51 = @(Get-Content -LiteralPath $week51Ref)
        if ($rows51.Count -gt 1) {
            $metrics.week51_via_timer_rows = $rows51.Count - 1
            $underflowRows = 0
            $irqRows = 0
            foreach ($line in $rows51) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 9) {
                    if ($parts[6] -eq '1') { $underflowRows++ }
                    if ($parts[7] -eq '1') { $irqRows++ }
                }
            }
            $metrics.week51_via_timer_underflow_rows = $underflowRows
            $metrics.week51_via_timer_irq_assert_rows = $irqRows
        }
    }
    $week52Ref = Join-Path -Path $repo -ChildPath "reference\edge\week52_via_shift_trace.csv"
    if (Test-Path -LiteralPath $week52Ref) {
        $rows52 = @(Get-Content -LiteralPath $week52Ref)
        if ($rows52.Count -gt 1) {
            $metrics.week52_via_shift_rows = $rows52.Count - 1
            $shiftIrqRows = 0
            $edgeCountMax = 0
            foreach ($line in $rows52) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 10) {
                    if ($parts[7] -eq '1') { $shiftIrqRows++ }
                    $edge = 0
                    if ([int]::TryParse($parts[5], [ref]$edge)) { if ($edge -gt $edgeCountMax) { $edgeCountMax = $edge } }
                }
            }
            $metrics.week52_via_shift_irq_rows = $shiftIrqRows
            $metrics.week52_via_shift_edge_count_max = $edgeCountMax
        }
    }
    $week53Ref = Join-Path -Path $repo -ChildPath "reference\edge\week53_cpu_via_iec_integration_trace.csv"
    if (Test-Path -LiteralPath $week53Ref) {
        $rows53 = @(Get-Content -LiteralPath $week53Ref)
        if ($rows53.Count -gt 1) {
            $metrics.week53_cpu_via_iec_rows = $rows53.Count - 1
            $cpuAdvancedRows = 0
            $irqOverlapRows = 0
            $gapMax = 0
            foreach ($line in $rows53) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 19) {
                    if ($parts[7] -eq '1') { $cpuAdvancedRows++ }
                    if (($parts[10] -eq '1') -and ($parts[11] -eq '1')) { $irqOverlapRows++ }
                    $gap = 0
                    if ([int]::TryParse($parts[18], [ref]$gap)) { if ($gap -gt $gapMax) { $gapMax = $gap } }
                }
            }
            $metrics.week53_cpu_via_iec_cpu_advanced_rows = $cpuAdvancedRows
            $metrics.week53_cpu_via_iec_irq_overlap_rows = $irqOverlapRows
            $metrics.week53_cpu_via_iec_cadence_gap_max = $gapMax
        }
    }
    $week54Ref = Join-Path -Path $repo -ChildPath "reference\edge\week54_cpu_via_iec_irq_bridge_trace.csv"
    if (Test-Path -LiteralPath $week54Ref) {
        $rows54 = @(Get-Content -LiteralPath $week54Ref)
        if ($rows54.Count -gt 1) {
            $metrics.week54_irq_bridge_rows = $rows54.Count - 1
            $cpuAdvancedRows = 0
            $viaIrqRows = 0
            $txServedMax = 0
            $gapMax = 0
            foreach ($line in $rows54) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 19) {
                    if ($parts[7] -eq '1') { $cpuAdvancedRows++ }
                    if ($parts[10] -eq '1') { $viaIrqRows++ }
                    $txServed = 0
                    $gap = 0
                    if ([int]::TryParse($parts[14], [ref]$txServed)) { if ($txServed -gt $txServedMax) { $txServedMax = $txServed } }
                    if ([int]::TryParse($parts[18], [ref]$gap)) { if ($gap -gt $gapMax) { $gapMax = $gap } }
                }
            }
            $metrics.week54_irq_bridge_cpu_advanced_rows = $cpuAdvancedRows
            $metrics.week54_irq_bridge_via_irq_or_rows = $viaIrqRows
            $metrics.week54_irq_bridge_iec_tx_served_max = $txServedMax
            $metrics.week54_irq_bridge_cadence_gap_max = $gapMax
        }
    }
    $week55Ref = Join-Path -Path $repo -ChildPath "reference\edge\week55_cpu_via_iec_timeout_bridge_trace.csv"
    if (Test-Path -LiteralPath $week55Ref) {
        $rows55 = @(Get-Content -LiteralPath $week55Ref)
        if ($rows55.Count -gt 1) {
            $metrics.week55_timeout_bridge_rows = $rows55.Count - 1
            $eoiTimeoutMax = 0
            $txTimeoutMax = 0
            $rxTimeoutMax = 0
            $status74Rows = 0
            $gapMax = 0
            foreach ($line in $rows55) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 20) {
                    $eoiTo = 0
                    $txTo = 0
                    $rxTo = 0
                    $gap = 0
                    if ([int]::TryParse($parts[11], [ref]$eoiTo)) { if ($eoiTo -gt $eoiTimeoutMax) { $eoiTimeoutMax = $eoiTo } }
                    if ([int]::TryParse($parts[12], [ref]$txTo)) { if ($txTo -gt $txTimeoutMax) { $txTimeoutMax = $txTo } }
                    if ([int]::TryParse($parts[13], [ref]$rxTo)) { if ($rxTo -gt $rxTimeoutMax) { $rxTimeoutMax = $rxTo } }
                    if ($parts[18] -eq '74') { $status74Rows++ }
                    if ([int]::TryParse($parts[19], [ref]$gap)) { if ($gap -gt $gapMax) { $gapMax = $gap } }
                }
            }
            $metrics.week55_timeout_bridge_eoi_timeout_max = $eoiTimeoutMax
            $metrics.week55_timeout_bridge_tx_timeout_max = $txTimeoutMax
            $metrics.week55_timeout_bridge_rx_timeout_max = $rxTimeoutMax
            $metrics.week55_timeout_bridge_status74_rows = $status74Rows
            $metrics.week55_timeout_bridge_cadence_gap_max = $gapMax
        }
    }
    $week56Ref = Join-Path -Path $repo -ChildPath "reference\edge\week56_drive_iec_phase_map_trace.csv"
    if (Test-Path -LiteralPath $week56Ref) {
        $rows56 = @(Get-Content -LiteralPath $week56Ref)
        if ($rows56.Count -gt 1) {
            $metrics.week56_drive_iec_phase_rows = $rows56.Count - 1
            $edgeCount = 0
            $latMin = 2147483647
            $latMax = 0
            $kernelRows = 0
            foreach ($line in $rows56) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 24) {
                    $edge = 0
                    if ([int]::TryParse($parts[8], [ref]$edge)) { $edgeCount += $edge }
                    $lat = 0
                    if ([int]::TryParse($parts[10], [ref]$lat)) {
                        if ($lat -ge 0 -and $lat -lt $latMin) { $latMin = $lat }
                        if ($lat -gt $latMax) { $latMax = $lat }
                    }
                    if ($parts[11] -eq '1' -or $parts[11] -eq '2' -or $parts[11] -eq '3') { $kernelRows++ }
                }
            }
            if ($latMin -eq 2147483647) { $latMin = 0 }
            $metrics.week56_drive_iec_phase_edge_count = $edgeCount
            $metrics.week56_drive_iec_phase_latency_min = $latMin
            $metrics.week56_drive_iec_phase_latency_max = $latMax
            $metrics.week56_drive_iec_phase_kernel_path_rows = $kernelRows
            $metrics.week53_drive_iec_phase_rows = $rows56.Count - 1
            $metrics.week53_drive_iec_phase_edge_count = $edgeCount
            $metrics.week53_drive_iec_phase_latency_min = $latMin
            $metrics.week53_drive_iec_phase_latency_max = $latMax
            $metrics.week53_drive_iec_phase_kernel_path_rows = $kernelRows
        }
    }
    $week57Ref = Join-Path -Path $repo -ChildPath "reference\edge\week57_iec_signal_window_trace.csv"
    if (Test-Path -LiteralPath $week57Ref) {
        $rows57 = @(Get-Content -LiteralPath $week57Ref)
        if ($rows57.Count -gt 1) {
            $metrics.week57_iec_signal_rows = $rows57.Count - 1
            $slewMax = 0
            $polarityMismatchRows = 0
            $turnaroundMax = 0
            $turnaroundMin = 2147483647
            foreach ($line in $rows57) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 20) {
                    $slew = 0
                    $turn = -1
                    if ([int]::TryParse($parts[10], [ref]$slew)) { if ($slew -gt $slewMax) { $slewMax = $slew } }
                    if ($parts[11] -eq '1') { $polarityMismatchRows++ }
                    if ([int]::TryParse($parts[12], [ref]$turn)) {
                        if ($turn -ge 0) {
                            if ($turn -gt $turnaroundMax) { $turnaroundMax = $turn }
                            if ($turn -lt $turnaroundMin) { $turnaroundMin = $turn }
                        }
                    }
                }
            }
            if ($turnaroundMin -eq 2147483647) { $turnaroundMin = 0 }
            $metrics.week57_iec_signal_edge_slew_max = $slewMax
            $metrics.week57_iec_signal_polarity_mismatch_rows = $polarityMismatchRows
            $metrics.week57_iec_signal_turnaround_max = $turnaroundMax
            $metrics.week57_iec_signal_turnaround_min = $turnaroundMin
        }
    }
    $week58Ref = Join-Path -Path $repo -ChildPath "reference\edge\week58_iec_analog_edge_model_trace.csv"
    if (Test-Path -LiteralPath $week58Ref) {
        $rows58 = @(Get-Content -LiteralPath $week58Ref)
        if ($rows58.Count -gt 1) {
            $metrics.week58_iec_analog_rows = $rows58.Count - 1
            $riseMax = 0
            $fallMax = 0
            $glitchRows = 0
            $turnaroundMax = 0
            $turnaroundMin = 2147483647
            foreach ($line in $rows58) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 23) {
                    $ar = 0
                    $af = 0
                    $cr = 0
                    $cf = 0
                    $dr = 0
                    $df = 0
                    $turn = -1
                    if ([int]::TryParse($parts[8], [ref]$ar)) { if ($ar -gt $riseMax) { $riseMax = $ar } }
                    if ([int]::TryParse($parts[10], [ref]$cr)) { if ($cr -gt $riseMax) { $riseMax = $cr } }
                    if ([int]::TryParse($parts[12], [ref]$dr)) { if ($dr -gt $riseMax) { $riseMax = $dr } }
                    if ([int]::TryParse($parts[9], [ref]$af)) { if ($af -gt $fallMax) { $fallMax = $af } }
                    if ([int]::TryParse($parts[11], [ref]$cf)) { if ($cf -gt $fallMax) { $fallMax = $cf } }
                    if ([int]::TryParse($parts[13], [ref]$df)) { if ($df -gt $fallMax) { $fallMax = $df } }
                    if ($parts[14] -eq '1') { $glitchRows++ }
                    if ([int]::TryParse($parts[15], [ref]$turn)) {
                        if ($turn -ge 0) {
                            if ($turn -gt $turnaroundMax) { $turnaroundMax = $turn }
                            if ($turn -lt $turnaroundMin) { $turnaroundMin = $turn }
                        }
                    }
                }
            }
            if ($turnaroundMin -eq 2147483647) { $turnaroundMin = 0 }
            $metrics.week58_iec_analog_rise_ticks_max = $riseMax
            $metrics.week58_iec_analog_fall_ticks_max = $fallMax
            $metrics.week58_iec_analog_polarity_glitch_rows = $glitchRows
            $metrics.week58_iec_analog_turnaround_max = $turnaroundMax
            $metrics.week58_iec_analog_turnaround_min = $turnaroundMin
        }
    }
    $week59Ref = Join-Path -Path $repo -ChildPath "reference\edge\week59_iec_analog_pulse_window_trace.csv"
    if (Test-Path -LiteralPath $week59Ref) {
        $rows59 = @(Get-Content -LiteralPath $week59Ref)
        if ($rows59.Count -gt 1) {
            $metrics.week59_iec_analog_pulse_rows = $rows59.Count - 1
            $pulseMinTicks = 2147483647
            $pulseMaxTicks = 0
            $turnaroundJitterMax = 0
            $turnaroundMax = 0
            $turnaroundMin = 2147483647
            foreach ($line in $rows59) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 23) {
                    $pmin = 0
                    $pmax = 0
                    $jit = 0
                    $turn = -1
                    if ([int]::TryParse($parts[12], [ref]$pmin)) {
                        if ($pmin -gt 0 -and $pmin -lt $pulseMinTicks) { $pulseMinTicks = $pmin }
                    }
                    if ([int]::TryParse($parts[13], [ref]$pmax)) {
                        if ($pmax -gt $pulseMaxTicks) { $pulseMaxTicks = $pmax }
                    }
                    if ([int]::TryParse($parts[15], [ref]$jit)) {
                        if ($jit -gt $turnaroundJitterMax) { $turnaroundJitterMax = $jit }
                    }
                    if ([int]::TryParse($parts[14], [ref]$turn)) {
                        if ($turn -ge 0) {
                            if ($turn -gt $turnaroundMax) { $turnaroundMax = $turn }
                            if ($turn -lt $turnaroundMin) { $turnaroundMin = $turn }
                        }
                    }
                }
            }
            if ($pulseMinTicks -eq 2147483647) { $pulseMinTicks = 0 }
            if ($turnaroundMin -eq 2147483647) { $turnaroundMin = 0 }
            $metrics.week59_iec_analog_pulse_min_ticks = $pulseMinTicks
            $metrics.week59_iec_analog_pulse_max_ticks = $pulseMaxTicks
            $metrics.week59_iec_analog_turnaround_jitter_max = $turnaroundJitterMax
            $metrics.week59_iec_analog_turnaround_max = $turnaroundMax
            $metrics.week59_iec_analog_turnaround_min = $turnaroundMin
        }
    }
    $week60Ref = Join-Path -Path $repo -ChildPath "reference\edge\week60_iec_contention_release_trace.csv"
    if (Test-Path -LiteralPath $week60Ref) {
        $rows60 = @(Get-Content -LiteralPath $week60Ref)
        if ($rows60.Count -gt 1) {
            $metrics.week60_iec_contention_rows = $rows60.Count - 1
            $releaseLatencyMax = 0
            $illegalOverlapRows = 0
            foreach ($line in $rows60) {
                $parts = $line.Split(',')
                if ($parts.Count -ge 21) {
                    $lat = -1
                    if ([int]::TryParse($parts[13], [ref]$lat)) {
                        if ($lat -gt $releaseLatencyMax) { $releaseLatencyMax = $lat }
                    }
                    if ($parts[14] -eq '1') { $illegalOverlapRows++ }
                }
            }
            $metrics.week60_iec_release_latency_max = $releaseLatencyMax
            $metrics.week60_iec_illegal_overlap_rows = $illegalOverlapRows
        }
    }
    Update-MetricsFromEdgeReferences -Metrics $metrics -RepoPath $repo

    $metricsPath = Join-Path -Path $repo -ChildPath "reference\edge\revision_tolerance_metrics.json"
    (@{ metrics = $metrics } | ConvertTo-Json -Depth 5) | Set-Content -LiteralPath $metricsPath -Encoding ASCII
    "[SIGNOFF] tolerance metrics: $metricsPath"
    "[SIGNOFF] ----------------------------------------"
}
finally {
    $env:PATH = $savedPath
}
