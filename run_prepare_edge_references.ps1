param(
    [string]$Manifest = "external_tests_golden_corpus.json",
    [switch]$RebuildStrict
)

$ErrorActionPreference = "Stop"

$repo = $PSScriptRoot
$gxx = "C:\msys64\ucrt64\bin\g++.exe"
$py = "python"
$pcTool = Join-Path $repo "tools\make_pc_only_reference.py"
$strictExe = Join-Path $repo "c64_11_strict_edge_ref.exe"
$savedPath = $env:PATH

try {
    $env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $env:PATH

if (-not (Test-Path -LiteralPath $pcTool)) {
    throw "Missing tool: $pcTool"
}

    if ($RebuildStrict -or -not (Test-Path -LiteralPath $strictExe)) {
        & $gxx -std=c++17 -O2 "-DRUN_PROFILE=RUN_PROFILE_STRICT" (Join-Path $repo "c64_11.cpp") -o $strictExe
        if ($LASTEXITCODE -ne 0) {
            throw "Strict build failed"
        }
    }

$savedManifest = [Environment]::GetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", "Process")
$savedGuard = [Environment]::GetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", "Process")
$savedWeek15 = [Environment]::GetEnvironmentVariable("WEEK15_BOOTSTRAP_BAAEC_REF", "Process")
$savedWeek16 = [Environment]::GetEnvironmentVariable("WEEK16_BOOTSTRAP_CROSS_REF", "Process")
$savedWeek18 = [Environment]::GetEnvironmentVariable("WEEK18_BOOTSTRAP_OPENBUS_REF", "Process")
$savedWeek19 = [Environment]::GetEnvironmentVariable("WEEK19_BOOTSTRAP_CIA_REF", "Process")
$savedWeek20 = [Environment]::GetEnvironmentVariable("WEEK20_BOOTSTRAP_VIC_REF", "Process")
$savedWeek21 = [Environment]::GetEnvironmentVariable("WEEK21_BOOTSTRAP_BUS_REF", "Process")
$savedWeek22 = [Environment]::GetEnvironmentVariable("WEEK22_BOOTSTRAP_PORTMAP_REF", "Process")
$savedWeek23 = [Environment]::GetEnvironmentVariable("WEEK23_BOOTSTRAP_IRQNMI_REF", "Process")
$savedWeek24 = [Environment]::GetEnvironmentVariable("WEEK24_BOOTSTRAP_IRQLATCH_REF", "Process")
$savedWeek25 = [Environment]::GetEnvironmentVariable("WEEK25_BOOTSTRAP_CIASERIAL_REF", "Process")
$savedWeek26 = [Environment]::GetEnvironmentVariable("WEEK26_BOOTSTRAP_DRIVEIEC_REF", "Process")
$savedWeek27 = [Environment]::GetEnvironmentVariable("WEEK27_BOOTSTRAP_DRIVECMD_REF", "Process")
$savedWeek28 = [Environment]::GetEnvironmentVariable("WEEK28_BOOTSTRAP_DRIVEEOI_REF", "Process")
$savedWeek29 = [Environment]::GetEnvironmentVariable("WEEK29_BOOTSTRAP_DRIVETIMEOUT_REF", "Process")
$savedWeek30 = [Environment]::GetEnvironmentVariable("WEEK30_BOOTSTRAP_CMDCH_REF", "Process")
$savedWeek31 = [Environment]::GetEnvironmentVariable("WEEK31_BOOTSTRAP_STATUSTALK_REF", "Process")
$savedWeek32 = [Environment]::GetEnvironmentVariable("WEEK32_BOOTSTRAP_DIRSTREAM_REF", "Process")
$savedWeek33 = [Environment]::GetEnvironmentVariable("WEEK33_BOOTSTRAP_DIRFILTER_REF", "Process")
$savedWeek34 = [Environment]::GetEnvironmentVariable("WEEK34_BOOTSTRAP_ALLOCMAP_REF", "Process")
$savedWeek35 = [Environment]::GetEnvironmentVariable("WEEK35_BOOTSTRAP_PTRDIR_REF", "Process")
$savedWeek36 = [Environment]::GetEnvironmentVariable("WEEK36_BOOTSTRAP_CATALOG_REF", "Process")
$savedWeek37 = [Environment]::GetEnvironmentVariable("WEEK37_BOOTSTRAP_ATNGATE_REF", "Process")
$savedWeek38 = [Environment]::GetEnvironmentVariable("WEEK38_BOOTSTRAP_TALKCH_REF", "Process")
$savedWeek39 = [Environment]::GetEnvironmentVariable("WEEK39_BOOTSTRAP_CMDRESP_REF", "Process")
$savedWeek40 = [Environment]::GetEnvironmentVariable("WEEK40_BOOTSTRAP_CMDBUF_REF", "Process")
$savedWeek41 = [Environment]::GetEnvironmentVariable("WEEK41_BOOTSTRAP_CLOSEDROP_REF", "Process")
$savedWeek42 = [Environment]::GetEnvironmentVariable("WEEK42_BOOTSTRAP_STATUS_REF", "Process")
$savedWeek43 = [Environment]::GetEnvironmentVariable("WEEK43_BOOTSTRAP_DIRMODE_REF", "Process")
$savedWeek44 = [Environment]::GetEnvironmentVariable("WEEK44_BOOTSTRAP_CMDRESPTERM_REF", "Process")
$savedWeek45Final = [Environment]::GetEnvironmentVariable("WEEK45_BOOTSTRAP_FINALFREEZE_REF", "Process")
$savedWeek46IecTiming = [Environment]::GetEnvironmentVariable("WEEK46_BOOTSTRAP_IECTIMING_REF", "Process")
$savedWeek47HostTiming = [Environment]::GetEnvironmentVariable("WEEK47_BOOTSTRAP_HOSTTIMING_REF", "Process")
$savedWeek48DriveCore = [Environment]::GetEnvironmentVariable("WEEK48_BOOTSTRAP_DRIVECORE_REF", "Process")
$savedWeek49DriveCpu = [Environment]::GetEnvironmentVariable("WEEK49_BOOTSTRAP_DRIVECPU_REF", "Process")
$savedWeek50DriveOpcode = [Environment]::GetEnvironmentVariable("WEEK50_BOOTSTRAP_DRIVEOPCODE_REF", "Process")
$savedWeek51ViaTimer = [Environment]::GetEnvironmentVariable("WEEK51_BOOTSTRAP_VIATIMER_REF", "Process")
$savedWeek52ViaShift = [Environment]::GetEnvironmentVariable("WEEK52_BOOTSTRAP_VIASHIFT_REF", "Process")
$savedWeek53CpuViaIec = [Environment]::GetEnvironmentVariable("WEEK53_BOOTSTRAP_CPUVIAIEC_REF", "Process")
$savedWeek54IrqBridge = [Environment]::GetEnvironmentVariable("WEEK54_BOOTSTRAP_IRQBRIDGE_REF", "Process")
$savedWeek55TimeoutBridge = [Environment]::GetEnvironmentVariable("WEEK55_BOOTSTRAP_TIMEOUTBRIDGE_REF", "Process")
$savedWeek56PhaseMap = [Environment]::GetEnvironmentVariable("WEEK56_BOOTSTRAP_PHASEMAP_REF", "Process")
$savedWeek57SignalWindow = [Environment]::GetEnvironmentVariable("WEEK57_BOOTSTRAP_SIGNALWINDOW_REF", "Process")
$savedWeek58Analog = [Environment]::GetEnvironmentVariable("WEEK58_BOOTSTRAP_ANALOG_REF", "Process")
$savedWeek59AnalogPulse = [Environment]::GetEnvironmentVariable("WEEK59_BOOTSTRAP_ANALOGPULSE_REF", "Process")
$savedWeek60Contention = [Environment]::GetEnvironmentVariable("WEEK60_BOOTSTRAP_CONTENTION_REF", "Process")
$savedWeek61Dos = [Environment]::GetEnvironmentVariable("WEEK61_BOOTSTRAP_DOS_REF", "Process")
$savedWeek62Gcr = [Environment]::GetEnvironmentVariable("WEEK62_BOOTSTRAP_GCR_REF", "Process")
$savedWeek63GcrDecode = [Environment]::GetEnvironmentVariable("WEEK63_BOOTSTRAP_GCRDECODE_REF", "Process")
$savedWeek64TrackLayout = [Environment]::GetEnvironmentVariable("WEEK64_BOOTSTRAP_TRACKLAYOUT_REF", "Process")
$savedWeek65CrcErrMap = [Environment]::GetEnvironmentVariable("WEEK65_BOOTSTRAP_CRCERRMAP_REF", "Process")
$savedWeek66CrcStatus = [Environment]::GetEnvironmentVariable("WEEK66_BOOTSTRAP_CRCSTATUS_REF", "Process")
$savedWeek67CrcErrClass = [Environment]::GetEnvironmentVariable("WEEK67_BOOTSTRAP_CRCERRCLASS_REF", "Process")
$savedWeek68CpuOwnership = [Environment]::GetEnvironmentVariable("WEEK68_BOOTSTRAP_CPUOWNERSHIP_REF", "Process")
$savedWeek69ViaTiming = [Environment]::GetEnvironmentVariable("WEEK69_BOOTSTRAP_VIATIMING_REF", "Process")
$savedWeek70GcrPipeline = [Environment]::GetEnvironmentVariable("WEEK70_BOOTSTRAP_GCRPIPE_REF", "Process")
$savedWeek71WriteRoundtrip = [Environment]::GetEnvironmentVariable("WEEK71_BOOTSTRAP_WRITE_REF", "Process")
$savedWeek72Physical = [Environment]::GetEnvironmentVariable("WEEK72_BOOTSTRAP_PHYSICAL_REF", "Process")
$savedWeek73ErrorMap = [Environment]::GetEnvironmentVariable("WEEK73_BOOTSTRAP_ERRORMAP_REF", "Process")
$savedWeek74Image = [Environment]::GetEnvironmentVariable("WEEK74_BOOTSTRAP_IMAGE_REF", "Process")
$savedWeek75Compat = [Environment]::GetEnvironmentVariable("WEEK75_BOOTSTRAP_COMPAT_REF", "Process")
$savedWeek76CompatDrift = [Environment]::GetEnvironmentVariable("WEEK76_BOOTSTRAP_COMPATDRIFT_REF", "Process")
$savedWeek77Corpus = [Environment]::GetEnvironmentVariable("WEEK77_BOOTSTRAP_CORPUS_REF", "Process")
$savedWeek78RealCorpus = [Environment]::GetEnvironmentVariable("WEEK78_BOOTSTRAP_REALCORPUS_REF", "Process")
$savedWeek79HardCorpus = [Environment]::GetEnvironmentVariable("WEEK79_BOOTSTRAP_HARDCORPUS_REF", "Process")
$savedWeek80Release = [Environment]::GetEnvironmentVariable("WEEK80_BOOTSTRAP_RELEASE_REF", "Process")

try {
    [Environment]::SetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", $Manifest, "Process")
    [Environment]::SetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK15_BOOTSTRAP_BAAEC_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK16_BOOTSTRAP_CROSS_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK18_BOOTSTRAP_OPENBUS_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK19_BOOTSTRAP_CIA_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK20_BOOTSTRAP_VIC_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK21_BOOTSTRAP_BUS_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK22_BOOTSTRAP_PORTMAP_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK23_BOOTSTRAP_IRQNMI_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK24_BOOTSTRAP_IRQLATCH_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK25_BOOTSTRAP_CIASERIAL_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK26_BOOTSTRAP_DRIVEIEC_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK27_BOOTSTRAP_DRIVECMD_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK28_BOOTSTRAP_DRIVEEOI_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK29_BOOTSTRAP_DRIVETIMEOUT_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK30_BOOTSTRAP_CMDCH_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK31_BOOTSTRAP_STATUSTALK_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK32_BOOTSTRAP_DIRSTREAM_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK33_BOOTSTRAP_DIRFILTER_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK34_BOOTSTRAP_ALLOCMAP_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK35_BOOTSTRAP_PTRDIR_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK36_BOOTSTRAP_CATALOG_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK37_BOOTSTRAP_ATNGATE_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK38_BOOTSTRAP_TALKCH_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK39_BOOTSTRAP_CMDRESP_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK40_BOOTSTRAP_CMDBUF_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK41_BOOTSTRAP_CLOSEDROP_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK42_BOOTSTRAP_STATUS_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK43_BOOTSTRAP_DIRMODE_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK44_BOOTSTRAP_CMDRESPTERM_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK45_BOOTSTRAP_FINALFREEZE_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK46_BOOTSTRAP_IECTIMING_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK47_BOOTSTRAP_HOSTTIMING_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK48_BOOTSTRAP_DRIVECORE_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK49_BOOTSTRAP_DRIVECPU_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK50_BOOTSTRAP_DRIVEOPCODE_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK51_BOOTSTRAP_VIATIMER_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK52_BOOTSTRAP_VIASHIFT_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK53_BOOTSTRAP_CPUVIAIEC_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK54_BOOTSTRAP_IRQBRIDGE_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK55_BOOTSTRAP_TIMEOUTBRIDGE_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK56_BOOTSTRAP_PHASEMAP_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK57_BOOTSTRAP_SIGNALWINDOW_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK58_BOOTSTRAP_ANALOG_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK59_BOOTSTRAP_ANALOGPULSE_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK60_BOOTSTRAP_CONTENTION_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK61_BOOTSTRAP_DOS_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK62_BOOTSTRAP_GCR_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK63_BOOTSTRAP_GCRDECODE_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK64_BOOTSTRAP_TRACKLAYOUT_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK65_BOOTSTRAP_CRCERRMAP_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK66_BOOTSTRAP_CRCSTATUS_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK67_BOOTSTRAP_CRCERRCLASS_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK68_BOOTSTRAP_CPUOWNERSHIP_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK69_BOOTSTRAP_VIATIMING_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK70_BOOTSTRAP_GCRPIPE_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK71_BOOTSTRAP_WRITE_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK72_BOOTSTRAP_PHYSICAL_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK73_BOOTSTRAP_ERRORMAP_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK74_BOOTSTRAP_IMAGE_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK75_BOOTSTRAP_COMPAT_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK76_BOOTSTRAP_COMPATDRIFT_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK77_BOOTSTRAP_CORPUS_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK78_BOOTSTRAP_REALCORPUS_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK79_BOOTSTRAP_HARDCORPUS_REF", "1", "Process")
    [Environment]::SetEnvironmentVariable("WEEK80_BOOTSTRAP_RELEASE_REF", "1", "Process")

    & $strictExe
    if ($LASTEXITCODE -ne 0) {
        throw "Strict run failed during edge reference bootstrap"
    }
}
finally {
    [Environment]::SetEnvironmentVariable("EXTERNAL_TEST_MANIFEST", $savedManifest, "Process")
    [Environment]::SetEnvironmentVariable("KERNAL_TEST_ONLY_PURE_CMD_GUARD", $savedGuard, "Process")
    [Environment]::SetEnvironmentVariable("WEEK15_BOOTSTRAP_BAAEC_REF", $savedWeek15, "Process")
    [Environment]::SetEnvironmentVariable("WEEK16_BOOTSTRAP_CROSS_REF", $savedWeek16, "Process")
    [Environment]::SetEnvironmentVariable("WEEK18_BOOTSTRAP_OPENBUS_REF", $savedWeek18, "Process")
    [Environment]::SetEnvironmentVariable("WEEK19_BOOTSTRAP_CIA_REF", $savedWeek19, "Process")
    [Environment]::SetEnvironmentVariable("WEEK20_BOOTSTRAP_VIC_REF", $savedWeek20, "Process")
    [Environment]::SetEnvironmentVariable("WEEK21_BOOTSTRAP_BUS_REF", $savedWeek21, "Process")
    [Environment]::SetEnvironmentVariable("WEEK22_BOOTSTRAP_PORTMAP_REF", $savedWeek22, "Process")
    [Environment]::SetEnvironmentVariable("WEEK23_BOOTSTRAP_IRQNMI_REF", $savedWeek23, "Process")
    [Environment]::SetEnvironmentVariable("WEEK24_BOOTSTRAP_IRQLATCH_REF", $savedWeek24, "Process")
    [Environment]::SetEnvironmentVariable("WEEK25_BOOTSTRAP_CIASERIAL_REF", $savedWeek25, "Process")
    [Environment]::SetEnvironmentVariable("WEEK26_BOOTSTRAP_DRIVEIEC_REF", $savedWeek26, "Process")
    [Environment]::SetEnvironmentVariable("WEEK27_BOOTSTRAP_DRIVECMD_REF", $savedWeek27, "Process")
    [Environment]::SetEnvironmentVariable("WEEK28_BOOTSTRAP_DRIVEEOI_REF", $savedWeek28, "Process")
    [Environment]::SetEnvironmentVariable("WEEK29_BOOTSTRAP_DRIVETIMEOUT_REF", $savedWeek29, "Process")
    [Environment]::SetEnvironmentVariable("WEEK30_BOOTSTRAP_CMDCH_REF", $savedWeek30, "Process")
    [Environment]::SetEnvironmentVariable("WEEK31_BOOTSTRAP_STATUSTALK_REF", $savedWeek31, "Process")
    [Environment]::SetEnvironmentVariable("WEEK32_BOOTSTRAP_DIRSTREAM_REF", $savedWeek32, "Process")
    [Environment]::SetEnvironmentVariable("WEEK33_BOOTSTRAP_DIRFILTER_REF", $savedWeek33, "Process")
    [Environment]::SetEnvironmentVariable("WEEK34_BOOTSTRAP_ALLOCMAP_REF", $savedWeek34, "Process")
    [Environment]::SetEnvironmentVariable("WEEK35_BOOTSTRAP_PTRDIR_REF", $savedWeek35, "Process")
    [Environment]::SetEnvironmentVariable("WEEK36_BOOTSTRAP_CATALOG_REF", $savedWeek36, "Process")
    [Environment]::SetEnvironmentVariable("WEEK37_BOOTSTRAP_ATNGATE_REF", $savedWeek37, "Process")
    [Environment]::SetEnvironmentVariable("WEEK38_BOOTSTRAP_TALKCH_REF", $savedWeek38, "Process")
    [Environment]::SetEnvironmentVariable("WEEK39_BOOTSTRAP_CMDRESP_REF", $savedWeek39, "Process")
    [Environment]::SetEnvironmentVariable("WEEK40_BOOTSTRAP_CMDBUF_REF", $savedWeek40, "Process")
    [Environment]::SetEnvironmentVariable("WEEK41_BOOTSTRAP_CLOSEDROP_REF", $savedWeek41, "Process")
    [Environment]::SetEnvironmentVariable("WEEK42_BOOTSTRAP_STATUS_REF", $savedWeek42, "Process")
    [Environment]::SetEnvironmentVariable("WEEK43_BOOTSTRAP_DIRMODE_REF", $savedWeek43, "Process")
    [Environment]::SetEnvironmentVariable("WEEK44_BOOTSTRAP_CMDRESPTERM_REF", $savedWeek44, "Process")
    [Environment]::SetEnvironmentVariable("WEEK45_BOOTSTRAP_FINALFREEZE_REF", $savedWeek45Final, "Process")
    [Environment]::SetEnvironmentVariable("WEEK46_BOOTSTRAP_IECTIMING_REF", $savedWeek46IecTiming, "Process")
    [Environment]::SetEnvironmentVariable("WEEK47_BOOTSTRAP_HOSTTIMING_REF", $savedWeek47HostTiming, "Process")
    [Environment]::SetEnvironmentVariable("WEEK48_BOOTSTRAP_DRIVECORE_REF", $savedWeek48DriveCore, "Process")
    [Environment]::SetEnvironmentVariable("WEEK49_BOOTSTRAP_DRIVECPU_REF", $savedWeek49DriveCpu, "Process")
    [Environment]::SetEnvironmentVariable("WEEK50_BOOTSTRAP_DRIVEOPCODE_REF", $savedWeek50DriveOpcode, "Process")
    [Environment]::SetEnvironmentVariable("WEEK51_BOOTSTRAP_VIATIMER_REF", $savedWeek51ViaTimer, "Process")
    [Environment]::SetEnvironmentVariable("WEEK52_BOOTSTRAP_VIASHIFT_REF", $savedWeek52ViaShift, "Process")
    [Environment]::SetEnvironmentVariable("WEEK53_BOOTSTRAP_CPUVIAIEC_REF", $savedWeek53CpuViaIec, "Process")
    [Environment]::SetEnvironmentVariable("WEEK54_BOOTSTRAP_IRQBRIDGE_REF", $savedWeek54IrqBridge, "Process")
    [Environment]::SetEnvironmentVariable("WEEK55_BOOTSTRAP_TIMEOUTBRIDGE_REF", $savedWeek55TimeoutBridge, "Process")
    [Environment]::SetEnvironmentVariable("WEEK56_BOOTSTRAP_PHASEMAP_REF", $savedWeek56PhaseMap, "Process")
    [Environment]::SetEnvironmentVariable("WEEK57_BOOTSTRAP_SIGNALWINDOW_REF", $savedWeek57SignalWindow, "Process")
    [Environment]::SetEnvironmentVariable("WEEK58_BOOTSTRAP_ANALOG_REF", $savedWeek58Analog, "Process")
    [Environment]::SetEnvironmentVariable("WEEK59_BOOTSTRAP_ANALOGPULSE_REF", $savedWeek59AnalogPulse, "Process")
    [Environment]::SetEnvironmentVariable("WEEK60_BOOTSTRAP_CONTENTION_REF", $savedWeek60Contention, "Process")
    [Environment]::SetEnvironmentVariable("WEEK61_BOOTSTRAP_DOS_REF", $savedWeek61Dos, "Process")
    [Environment]::SetEnvironmentVariable("WEEK62_BOOTSTRAP_GCR_REF", $savedWeek62Gcr, "Process")
    [Environment]::SetEnvironmentVariable("WEEK63_BOOTSTRAP_GCRDECODE_REF", $savedWeek63GcrDecode, "Process")
    [Environment]::SetEnvironmentVariable("WEEK64_BOOTSTRAP_TRACKLAYOUT_REF", $savedWeek64TrackLayout, "Process")
    [Environment]::SetEnvironmentVariable("WEEK65_BOOTSTRAP_CRCERRMAP_REF", $savedWeek65CrcErrMap, "Process")
    [Environment]::SetEnvironmentVariable("WEEK66_BOOTSTRAP_CRCSTATUS_REF", $savedWeek66CrcStatus, "Process")
    [Environment]::SetEnvironmentVariable("WEEK67_BOOTSTRAP_CRCERRCLASS_REF", $savedWeek67CrcErrClass, "Process")
    [Environment]::SetEnvironmentVariable("WEEK68_BOOTSTRAP_CPUOWNERSHIP_REF", $savedWeek68CpuOwnership, "Process")
    [Environment]::SetEnvironmentVariable("WEEK69_BOOTSTRAP_VIATIMING_REF", $savedWeek69ViaTiming, "Process")
    [Environment]::SetEnvironmentVariable("WEEK70_BOOTSTRAP_GCRPIPE_REF", $savedWeek70GcrPipeline, "Process")
    [Environment]::SetEnvironmentVariable("WEEK71_BOOTSTRAP_WRITE_REF", $savedWeek71WriteRoundtrip, "Process")
    [Environment]::SetEnvironmentVariable("WEEK72_BOOTSTRAP_PHYSICAL_REF", $savedWeek72Physical, "Process")
    [Environment]::SetEnvironmentVariable("WEEK73_BOOTSTRAP_ERRORMAP_REF", $savedWeek73ErrorMap, "Process")
    [Environment]::SetEnvironmentVariable("WEEK74_BOOTSTRAP_IMAGE_REF", $savedWeek74Image, "Process")
    [Environment]::SetEnvironmentVariable("WEEK75_BOOTSTRAP_COMPAT_REF", $savedWeek75Compat, "Process")
    [Environment]::SetEnvironmentVariable("WEEK76_BOOTSTRAP_COMPATDRIFT_REF", $savedWeek76CompatDrift, "Process")
    [Environment]::SetEnvironmentVariable("WEEK77_BOOTSTRAP_CORPUS_REF", $savedWeek77Corpus, "Process")
    [Environment]::SetEnvironmentVariable("WEEK78_BOOTSTRAP_REALCORPUS_REF", $savedWeek78RealCorpus, "Process")
    [Environment]::SetEnvironmentVariable("WEEK79_BOOTSTRAP_HARDCORPUS_REF", $savedWeek79HardCorpus, "Process")
    [Environment]::SetEnvironmentVariable("WEEK80_BOOTSTRAP_RELEASE_REF", $savedWeek80Release, "Process")
}

$week15Runtime = Join-Path $repo "week15_baaec_handoff_runtime.csv"
$week16Runtime = Join-Path $repo "week16_cross_domain_runtime.csv"
$week18Runtime = Join-Path $repo "week18_openbus_revision_runtime.csv"
$week19Runtime = Join-Path $repo "week19_cia_dense_runtime.csv"
$week20Runtime = Join-Path $repo "week20_vic_pathological_runtime.csv"
$week21Runtime = Join-Path $repo "week21_bus_corner_runtime.csv"
$week22Runtime = Join-Path $repo "week22_port_map_runtime.csv"
$week23Runtime = Join-Path $repo "week23_cia_irq_nmi_runtime.csv"
$week24Runtime = Join-Path $repo "week24_irq_latch_runtime.csv"
$week25Runtime = Join-Path $repo "week25_cia_serial_runtime.csv"
$week26Runtime = Join-Path $repo "week26_drive_iec_runtime.csv"
$week27Runtime = Join-Path $repo "week27_drive_cmdphase_runtime.csv"
$week28Runtime = Join-Path $repo "week28_drive_eoi_atn_runtime.csv"
$week29Runtime = Join-Path $repo "week29_drive_timeout_runtime.csv"
$week30Runtime = Join-Path $repo "week30_drive_cmdch_runtime.csv"
$week31Runtime = Join-Path $repo "week31_drive_status_talk_runtime.csv"
$week32Runtime = Join-Path $repo "week32_drive_dir_stream_runtime.csv"
$week33Runtime = Join-Path $repo "week33_drive_dir_filter_runtime.csv"
$week34Runtime = Join-Path $repo "week34_drive_alloc_map_runtime.csv"
$week35Runtime = Join-Path $repo "week35_drive_ptr_dir_runtime.csv"
$week36Runtime = Join-Path $repo "week36_drive_catalog_runtime.csv"
$week37Runtime = Join-Path $repo "week37_drive_atn_gate_runtime.csv"
$week38Runtime = Join-Path $repo "week38_drive_talkch_close_runtime.csv"
$week39Runtime = Join-Path $repo "week39_drive_cmdresp_fallback_runtime.csv"
$week40Runtime = Join-Path $repo "week40_drive_cmdbuf_commit_runtime.csv"
$week41Runtime = Join-Path $repo "week41_drive_close15_drop_runtime.csv"
$week42Runtime = Join-Path $repo "week42_drive_status_rebuild_runtime.csv"
$week43Runtime = Join-Path $repo "week43_drive_dirmode_runtime.csv"
$week44Runtime = Join-Path $repo "week44_drive_cmdresp_term_runtime.csv"
$week45Runtime = Join-Path $repo "week45_drive_final_freeze_runtime.csv"
$week46Runtime = Join-Path $repo "week46_drive_iec_timing_grade_runtime.csv"
$week47Runtime = Join-Path $repo "week47_host_timing_runtime.csv"
$week48Runtime = Join-Path $repo "week48_drive_core_timing_runtime.csv"
$week49Runtime = Join-Path $repo "week49_drive_cpu_cadence_runtime.csv"
$week50Runtime = Join-Path $repo "week50_drive_cpu_opcode_timing_runtime.csv"
$week51Runtime = Join-Path $repo "week51_via_timer_irq_runtime.csv"
$week52Runtime = Join-Path $repo "week52_via_shift_runtime.csv"
$week53Runtime = Join-Path $repo "week53_cpu_via_iec_integration_runtime.csv"
$week54Runtime = Join-Path $repo "week54_cpu_via_iec_irq_bridge_runtime.csv"
$week55Runtime = Join-Path $repo "week55_cpu_via_iec_timeout_bridge_runtime.csv"
$week56Runtime = Join-Path $repo "week56_drive_iec_phase_map_runtime.csv"
$week57Runtime = Join-Path $repo "week57_iec_signal_window_runtime.csv"
$week58Runtime = Join-Path $repo "week58_iec_analog_edge_model_runtime.csv"
$week59Runtime = Join-Path $repo "week59_iec_analog_pulse_window_runtime.csv"
$week60Runtime = Join-Path $repo "week60_iec_contention_release_runtime.csv"
$week61Runtime = Join-Path $repo "week61_drive_dos_semantic_runtime.csv"
$week62Runtime = Join-Path $repo "week62_disk_fidelity_gcr_runtime.csv"
$week63Runtime = Join-Path $repo "week63_gcr_decode_path_runtime.csv"
$week64Runtime = Join-Path $repo "week64_track_layout_realism_runtime.csv"
$week65Runtime = Join-Path $repo "week65_crc_ecc_error_map_runtime.csv"
$week66Runtime = Join-Path $repo "week66_crc_status_latch_runtime.csv"
$week67Runtime = Join-Path $repo "week67_crc_error_class_runtime.csv"
$week68Runtime = Join-Path $repo "week68_drive_cpu_ownership_runtime.csv"
$week69Runtime = Join-Path $repo "week69_via_timing_grade_runtime.csv"
$week70Runtime = Join-Path $repo "week70_gcr_read_pipeline_runtime.csv"
$week71Runtime = Join-Path $repo "week71_gcr_write_roundtrip_runtime.csv"
$week72Runtime = Join-Path $repo "week72_physical_disk_effects_runtime.csv"
$week73Runtime = Join-Path $repo "week73_error_engine_dos_mapping_runtime.csv"
$week74Runtime = Join-Path $repo "week74_image_fidelity_runtime.csv"
$week75Runtime = Join-Path $repo "week75_compatibility_signoff_runtime.csv"
$week76Runtime = Join-Path $repo "week76_compatibility_drift_runtime.csv"
$week77Runtime = Join-Path $repo "week77_real_corpus_bridge_runtime.csv"
$week78Runtime = Join-Path $repo "week78_real_disk_corpus_runtime.csv"
$week79Runtime = Join-Path $repo "week79_real_hard_corpus_runtime.csv"
$week80Runtime = Join-Path $repo "week80_release_readiness_runtime.csv"
$brknRuntime = Join-Path $repo "c64_lorenz_brkn_edge_ref.trace.csv"

if (-not (Test-Path -LiteralPath $week15Runtime)) {
    throw "Missing runtime edge trace: $week15Runtime"
}
if (-not (Test-Path -LiteralPath $week16Runtime)) {
    throw "Missing runtime edge trace: $week16Runtime"
}
if (-not (Test-Path -LiteralPath $week18Runtime)) {
    throw "Missing runtime edge trace: $week18Runtime"
}
if (-not (Test-Path -LiteralPath $week19Runtime)) {
    throw "Missing runtime edge trace: $week19Runtime"
}
if (-not (Test-Path -LiteralPath $week20Runtime)) {
    throw "Missing runtime edge trace: $week20Runtime"
}
if (-not (Test-Path -LiteralPath $week21Runtime)) {
    throw "Missing runtime edge trace: $week21Runtime"
}
if (-not (Test-Path -LiteralPath $week22Runtime)) {
    throw "Missing runtime edge trace: $week22Runtime"
}
if (-not (Test-Path -LiteralPath $week23Runtime)) {
    throw "Missing runtime edge trace: $week23Runtime"
}
if (-not (Test-Path -LiteralPath $week24Runtime)) {
    throw "Missing runtime edge trace: $week24Runtime"
}
if (-not (Test-Path -LiteralPath $week25Runtime)) {
    throw "Missing runtime edge trace: $week25Runtime"
}
if (-not (Test-Path -LiteralPath $week26Runtime)) {
    throw "Missing runtime edge trace: $week26Runtime"
}
if (-not (Test-Path -LiteralPath $week27Runtime)) {
    throw "Missing runtime edge trace: $week27Runtime"
}
if (-not (Test-Path -LiteralPath $week28Runtime)) {
    throw "Missing runtime edge trace: $week28Runtime"
}
if (-not (Test-Path -LiteralPath $week29Runtime)) {
    throw "Missing runtime edge trace: $week29Runtime"
}
if (-not (Test-Path -LiteralPath $week30Runtime)) {
    throw "Missing runtime edge trace: $week30Runtime"
}
if (-not (Test-Path -LiteralPath $week31Runtime)) {
    throw "Missing runtime edge trace: $week31Runtime"
}
if (-not (Test-Path -LiteralPath $week32Runtime)) {
    throw "Missing runtime edge trace: $week32Runtime"
}
if (-not (Test-Path -LiteralPath $week33Runtime)) {
    throw "Missing runtime edge trace: $week33Runtime"
}
if (-not (Test-Path -LiteralPath $week34Runtime)) {
    throw "Missing runtime edge trace: $week34Runtime"
}
if (-not (Test-Path -LiteralPath $week35Runtime)) {
    throw "Missing runtime edge trace: $week35Runtime"
}
if (-not (Test-Path -LiteralPath $week36Runtime)) {
    throw "Missing runtime edge trace: $week36Runtime"
}
if (-not (Test-Path -LiteralPath $week37Runtime)) {
    throw "Missing runtime edge trace: $week37Runtime"
}
if (-not (Test-Path -LiteralPath $week38Runtime)) {
    throw "Missing runtime edge trace: $week38Runtime"
}
if (-not (Test-Path -LiteralPath $week39Runtime)) {
    throw "Missing runtime edge trace: $week39Runtime"
}
if (-not (Test-Path -LiteralPath $week40Runtime)) {
    throw "Missing runtime edge trace: $week40Runtime"
}
if (-not (Test-Path -LiteralPath $week41Runtime)) {
    throw "Missing runtime edge trace: $week41Runtime"
}
if (-not (Test-Path -LiteralPath $week42Runtime)) {
    throw "Missing runtime edge trace: $week42Runtime"
}
if (-not (Test-Path -LiteralPath $week43Runtime)) {
    throw "Missing runtime edge trace: $week43Runtime"
}
if (-not (Test-Path -LiteralPath $week44Runtime)) {
    throw "Missing runtime edge trace: $week44Runtime"
}
if (-not (Test-Path -LiteralPath $week45Runtime)) {
    throw "Missing runtime edge trace: $week45Runtime"
}
if (-not (Test-Path -LiteralPath $week46Runtime)) {
    throw "Missing runtime edge trace: $week46Runtime"
}
if (-not (Test-Path -LiteralPath $week47Runtime)) {
    throw "Missing runtime edge trace: $week47Runtime"
}
if (-not (Test-Path -LiteralPath $week48Runtime)) {
    throw "Missing runtime edge trace: $week48Runtime"
}
if (-not (Test-Path -LiteralPath $week49Runtime)) {
    throw "Missing runtime edge trace: $week49Runtime"
}
if (-not (Test-Path -LiteralPath $week50Runtime)) {
    throw "Missing runtime edge trace: $week50Runtime"
}
if (-not (Test-Path -LiteralPath $week51Runtime)) {
    throw "Missing runtime edge trace: $week51Runtime"
}
if (-not (Test-Path -LiteralPath $week52Runtime)) {
    throw "Missing runtime edge trace: $week52Runtime"
}
if (-not (Test-Path -LiteralPath $week53Runtime)) {
    throw "Missing runtime edge trace: $week53Runtime"
}
if (-not (Test-Path -LiteralPath $week54Runtime)) {
    throw "Missing runtime edge trace: $week54Runtime"
}
if (-not (Test-Path -LiteralPath $week55Runtime)) {
    throw "Missing runtime edge trace: $week55Runtime"
}
if (-not (Test-Path -LiteralPath $week56Runtime)) {
    throw "Missing runtime edge trace: $week56Runtime"
}
if (-not (Test-Path -LiteralPath $week57Runtime)) {
    throw "Missing runtime edge trace: $week57Runtime"
}
if (-not (Test-Path -LiteralPath $week58Runtime)) {
    throw "Missing runtime edge trace: $week58Runtime"
}
if (-not (Test-Path -LiteralPath $week59Runtime)) {
    throw "Missing runtime edge trace: $week59Runtime"
}
if (-not (Test-Path -LiteralPath $week60Runtime)) {
    throw "Missing runtime edge trace: $week60Runtime"
}
if (-not (Test-Path -LiteralPath $week61Runtime)) {
    throw "Missing runtime edge trace: $week61Runtime"
}
if (-not (Test-Path -LiteralPath $week62Runtime)) {
    throw "Missing runtime edge trace: $week62Runtime"
}
if (-not (Test-Path -LiteralPath $week63Runtime)) {
    throw "Missing runtime edge trace: $week63Runtime"
}
if (-not (Test-Path -LiteralPath $week64Runtime)) {
    throw "Missing runtime edge trace: $week64Runtime"
}
if (-not (Test-Path -LiteralPath $week65Runtime)) {
    throw "Missing runtime edge trace: $week65Runtime"
}
if (-not (Test-Path -LiteralPath $week66Runtime)) {
    throw "Missing runtime edge trace: $week66Runtime"
}
if (-not (Test-Path -LiteralPath $week67Runtime)) {
    throw "Missing runtime edge trace: $week67Runtime"
}
if (-not (Test-Path -LiteralPath $week68Runtime)) {
    throw "Missing runtime edge trace: $week68Runtime"
}
if (-not (Test-Path -LiteralPath $week69Runtime)) {
    throw "Missing runtime edge trace: $week69Runtime"
}
if (-not (Test-Path -LiteralPath $week70Runtime)) {
    throw "Missing runtime edge trace: $week70Runtime"
}
if (-not (Test-Path -LiteralPath $week71Runtime)) {
    throw "Missing runtime edge trace: $week71Runtime"
}
if (-not (Test-Path -LiteralPath $week72Runtime)) {
    throw "Missing runtime edge trace: $week72Runtime"
}
if (-not (Test-Path -LiteralPath $week73Runtime)) {
    throw "Missing runtime edge trace: $week73Runtime"
}
if (-not (Test-Path -LiteralPath $week74Runtime)) {
    throw "Missing runtime edge trace: $week74Runtime"
}
if (-not (Test-Path -LiteralPath $week75Runtime)) {
    throw "Missing runtime edge trace: $week75Runtime"
}
if (-not (Test-Path -LiteralPath $week76Runtime)) {
    throw "Missing runtime edge trace: $week76Runtime"
}
if (-not (Test-Path -LiteralPath $week77Runtime)) {
    throw "Missing runtime edge trace: $week77Runtime"
}
if (-not (Test-Path -LiteralPath $week78Runtime)) {
    throw "Missing runtime edge trace: $week78Runtime"
}
if (-not (Test-Path -LiteralPath $week79Runtime)) {
    throw "Missing runtime edge trace: $week79Runtime"
}
if (-not (Test-Path -LiteralPath $week80Runtime)) {
    throw "Missing runtime edge trace: $week80Runtime"
}
if (-not (Test-Path -LiteralPath $brknRuntime)) {
    throw "Missing runtime trace: $brknRuntime"
}

$brknRef = Join-Path $repo "reference\vice\c64_lorenz_brkn_edge_ref.trace.csv"
& $py $pcTool --source $brknRuntime --dest $brknRef
if ($LASTEXITCODE -ne 0) {
    throw "Failed building pc_only reference for c64_lorenz_brkn_edge_ref"
}

"[EDGE-REF] PASS: refreshed week15/week16/week18/week19/week20/week21/week22/week23/week24/week25/week26/week27/week28/week29/week30/week31/week32/week33/week34/week35/week36/week37/week38/week39/week40/week41/week42/week43/week44/week45/week46/week47/week48/week49/week50/week51/week52/week53/week54/week55/week56/week57/week58/week59/week60/week61/week62/week63/week64/week65/week66/week67/week68/week69/week70/week71/week72/week73/week74/week75/week76/week77/week78/week79/week80 edge references and c64_lorenz_brkn_edge_ref pc_only reference."
}
finally {
    $env:PATH = $savedPath
}
