# Week 13-14 Reference Diff & Sign-off

## Scope

- Week13: reference diff against VICE/x64sc traces on key scenarios.
- Week14: define and run a golden corpus that covers CPU, VIC, CIA, IEC+1541.

## Reference Diff (Week13)

- External case runner now supports optional per-test reference trace diff fields:
  - `reference_trace`
  - `max_trace_mismatches`
  - `reference_optional`
- Diff compares normalized CSV rows (excluding header) between runtime trace and reference trace.
- If reference is missing and `reference_optional=true`, test is logged as
  `"[EXT][REFDIFF] SKIP optional"` and does not fail the run.
- If reference exists, pass condition is `mismatches <= max_trace_mismatches`.

## Golden Corpus (Week14)

- New manifest: `external_tests_golden_corpus.json`
- Fast-profile companion manifest: `external_tests_golden_corpus_fast.json`
- Coverage groups:
  - CPU official: `cpu_klaus_6502_functional`, `cpu_klaus_6502_decimal`
  - CPU unofficial: `cpu_unofficial_illegal_codes`
  - CPU timing proxy set: branch/pagecross, RMW/dummy, IRQ/NMI delay
  - VIC raster/sprite/badline: `c64_vice_testprogs_vicii` + VIC checklist digest gate
  - CIA timers/TOD: `c64_vice_testprogs_cia` + CIA battery digest gate
  - CPU/VIC boundary edge hard-ref: `c64_lorenz_brkn_edge_ref` (pc_only hard gate)
  - IEC + 1541 E2E: drive IEC smoke suite + KERNAL IEC E2E

## Week15 BA/AEC Edge Handoff Gate

- Added dedicated BA/AEC edge battery with explicit acquire/release scenarios and PHI phase checks.
- CPU bus gating now stalls PHI2 bus work when `AEC=0` while preserving half-cycle progression.
- Added hard reference trace (row-by-row strict compare, non-pc-frequency proxy):
  - runtime: `week15_baaec_handoff_runtime.csv`
  - reference: `reference/edge/week15_baaec_handoff_trace.csv`

## Week16 Cross-Domain + Fuzz + Invariants

- Added cross-domain edge hard-ref scenario combining VIC AEC toggles + CPU phase + CIA IRQ/NMI line activity.
  - runtime: `week16_cross_domain_runtime.csv`
  - reference: `reference/edge/week16_cross_domain_trace.csv`
- Added deterministic temporal fuzz campaign with fixed seeds and digest replay assertions.
- Added light model-check invariants in-loop:
  - `total_halfcycles` monotonic one-step progression
  - no PHI2 marker execution while bus is denied (`AEC=0` + pre-phase PHI2)
  - queue/pending consistency under prolonged contention and release

## Week19 CIA Dense Edge Hard-Ref

- Added dense CIA mixed scenario per revision (6526 / 6526A / 6526R4) combining:
  - TOD ticking,
  - serial pin transitions,
  - FLAG edge IRQ timing,
  - timer underflow progression.
- Hard reference trace:
  - runtime: `week19_cia_dense_runtime.csv`
  - reference: `reference/edge/week19_cia_dense_trace.csv`

## Week20 VIC Pathological Edge Hard-Ref

- Added pathological VIC scenario across revisions (6569 / 6569R3 / 8565 / 8565R2) with:
  - repeated VSP-like toggles,
  - FLD-like y-scroll transitions,
  - sprite DMA overlap windows,
  - IRQ side-effects sampling.
- Hard reference trace:
  - runtime: `week20_vic_pathological_runtime.csv`
  - reference: `reference/edge/week20_vic_pathological_trace.csv`

## Week21 Bus Corner Edge Hard-Ref

- Added bus-level corner battery with edge cases for:
  - open-bus decay under floating reads,
  - bank switch visibility transitions,
  - flat memory mode latch behavior,
  - BA/AEC contention hold/release around PHI phase.
- Hard reference trace:
  - runtime: `week21_bus_corner_runtime.csv`
  - reference: `reference/edge/week21_bus_corner_trace.csv`

## Week22 Port Map + Open-Bus Transition Hard-Ref

- Added a dedicated bus mapping battery per open-bus revision (NMOS/HMOS) for:
  - `$0001` port mapping transitions (`LORAM/HIRAM/CHAREN`) across IO/CHAR/RAM visibility,
  - floating IO region decay progression and threshold crossing,
  - masked port readback with DDR interaction,
  - flat-memory passthrough behavior under active open-bus profile.
- Hard reference trace:
  - runtime: `week22_port_map_runtime.csv`
  - reference: `reference/edge/week22_port_map_trace.csv`

## Week23 CIA IRQ/NMI Bridge Hard-Ref

- Added dedicated CIA interrupt-line bridging edge battery (per 6526/6526A/6526R4) for:
  - FLAG falling edge propagation timing into deferred/immediate ICR paths,
  - CPU interrupt-line synchronization (`IRQ` from CIA1 and `NMI` from CIA2),
  - interrupt clear semantics via ICR readback.
- Hard reference trace:
  - runtime: `week23_cia_irq_nmi_runtime.csv`
  - reference: `reference/edge/week23_cia_irq_nmi_trace.csv`

## Week24 IRQ/NMI Latch Under AEC Hard-Ref

- Added dedicated IRQ/NMI latch behavior battery under VIC bus denial (`AEC=0`) across CPU revisions:
  - IRQ sampled-low persistence while PHI2 bus micro-op is blocked,
  - delayed execution after `AEC` release,
  - NMI edge capture while contention is active.
- Hard reference trace:
  - runtime: `week24_irq_latch_runtime.csv`
  - reference: `reference/edge/week24_irq_latch_trace.csv`

## Week25 CIA Serial Rx/Tx Revision Edge Hard-Ref

- Added dedicated CIA serial battery across 6526/6526A/6526R4 for:
  - serial output shift progression and completion IRQ timing,
  - serial input edge direction differences (rising/falling) by revision,
  - ICR clear semantics across tx/rx completion paths.
- Hard reference trace:
  - runtime: `week25_cia_serial_runtime.csv`
  - reference: `reference/edge/week25_cia_serial_trace.csv`

## Week26 Drive IEC Handshake Revision Edge Hard-Ref

- Added dedicated 1541 IEC handshake battery across drive revisions (1541/1541C/1541-II) for:
  - ATN acknowledge window and clock-low acknowledgement behavior,
  - listener byte-ack progression after receive completion,
  - revision-sensitive handshake cadence visibility.
- Hard reference trace:
  - runtime: `week26_drive_iec_runtime.csv`
  - reference: `reference/edge/week26_drive_iec_trace.csv`

## Week27 Drive Command-Phase Transition Hard-Ref

- Added dedicated 1541 command-phase transition battery across drive revisions (1541/1541C/1541-II) for:
  - ATN-low command entry,
  - LISTEN/UNLISTEN and TALK/UNTALK transitions,
  - TALK SA0 confirmation and channel-state evolution.
- Hard reference trace:
  - runtime: `week27_drive_cmdphase_runtime.csv`
  - reference: `reference/edge/week27_drive_cmdphase_trace.csv`

## Week28 Drive EOI/ATN Interaction Hard-Ref

- Added dedicated 1541 edge battery across drive revisions (1541/1541C/1541-II) for:
  - TALK byte shifting up to EOI pending state,
  - EOI acknowledge low/high handshake completion,
  - ATN assertion/release interaction around active TALK flow.
- Hard reference trace:
  - runtime: `week28_drive_eoi_atn_runtime.csv`
  - reference: `reference/edge/week28_drive_eoi_atn_trace.csv`

## Week29 Drive Timeout/Recovery Hard-Ref

- Added dedicated 1541 timeout and recovery battery across drive revisions (1541/1541C/1541-II) for:
  - RX idle timeout trigger and edge-based recovery,
  - TX idle timeout trigger under active talk state,
  - EOI wait timeout trigger and post-timeout command processing recovery.
- Hard reference trace:
  - runtime: `week29_drive_timeout_runtime.csv`
  - reference: `reference/edge/week29_drive_timeout_trace.csv`

## Week30 Drive Command-Channel + Status Hard-Ref

- Added dedicated 1541 command-channel battery across drive revisions (1541/1541C/1541-II) for:
  - `M-W`/`M-R` command handling with response payload generation,
  - `B-A`/`B-F` allocation/free status transitions,
  - syntax-error path (`30,SYNTAX ERROR`) with stable counter progression.
- Hard reference trace:
  - runtime: `week30_drive_cmdch_runtime.csv`
  - reference: `reference/edge/week30_drive_cmdch_trace.csv`

## Week31 Drive Status-Talk Path Hard-Ref

- Added dedicated 1541 status-talk battery across drive revisions (1541/1541C/1541-II) for:
  - TALK channel-15 transition from response queue payload to status payload fallback,
  - status-line propagation in talk stream,
  - TALK/UNTALK state stability and queue shape progression.
- Hard reference trace:
  - runtime: `week31_drive_status_talk_runtime.csv`
  - reference: `reference/edge/week31_drive_status_talk_trace.csv`

## Week32 Drive Directory Stream Hard-Ref

- Added dedicated 1541 directory stream battery across drive revisions (1541/1541C/1541-II) for:
  - filtered directory stub payload creation (wildcard/type/mode),
  - block-buffer directory streaming with pointer offsets (`B-P`-style channel pointer use),
  - fallback reset to unfiltered directory stream shape.
- Hard reference trace:
  - runtime: `week32_drive_dir_stream_runtime.csv`
  - reference: `reference/edge/week32_drive_dir_stream_trace.csv`

## Week33 Drive Directory Filter Hard-Ref

- Added dedicated 1541 directory filter battery across drive revisions (1541/1541C/1541-II) for:
  - wildcard + type + mode filtered directory generation,
  - negated mode filter path (`!W`-style behavior),
  - filter reset back to unfiltered payload baseline.
- Hard reference trace:
  - runtime: `week33_drive_dir_filter_runtime.csv`
  - reference: `reference/edge/week33_drive_dir_filter_trace.csv`

## Week34 Drive Allocation/Map Hard-Ref

- Added dedicated 1541 allocation-map battery across drive revisions (1541/1541C/1541-II) for:
  - successful allocation/free transitions,
  - duplicate allocate and missing-free failure paths,
  - block ownership bitmap and free-block counter consistency.
- Hard reference trace:
  - runtime: `week34_drive_alloc_map_runtime.csv`
  - reference: `reference/edge/week34_drive_alloc_map_trace.csv`

## Week35 Drive Pointer-Directory Hard-Ref

- Added dedicated 1541 pointer-directory battery across drive revisions (1541/1541C/1541-II) for:
  - block-buffer directory streaming at multiple pointer offsets,
  - pointer validity and stream-shape consistency,
  - fallback to stub directory path after pointer-driven block-buffer flow.
- Hard reference trace:
  - runtime: `week35_drive_ptr_dir_runtime.csv`
  - reference: `reference/edge/week35_drive_ptr_dir_trace.csv`

## Week36 Drive Catalog Lifecycle Hard-Ref

- Added dedicated 1541 catalog lifecycle battery across drive revisions (1541/1541C/1541-II) for:
  - catalog entry creation/growth across multi-channel allocations,
  - entry removal when block count returns to zero,
  - error path stability on missing free (`65,NO BLOCK,00,00`).
- Hard reference trace:
  - runtime: `week36_drive_catalog_runtime.csv`
  - reference: `reference/edge/week36_drive_catalog_trace.csv`

## Week37 Drive ATN Command-Gate Hard-Ref

- Added dedicated 1541 ATN command-gate battery across drive revisions (1541/1541C/1541-II) for:
  - LISTEN/TALK command rejection when command-state sees ATN high,
  - acceptance when ATN is asserted low,
  - follow-up SA0/UNLISTEN/UNTALK transition consistency.
- Hard reference trace:
  - runtime: `week37_drive_atn_gate_runtime.csv`
  - reference: `reference/edge/week37_drive_atn_gate_trace.csv`

## Week38 Drive TALK/CLOSE Channel Hard-Ref

- Added dedicated 1541 TALK/CLOSE channel battery across drive revisions (1541/1541C/1541-II) for:
  - TALK command + SA15 status channel open sequencing,
  - invalid TALK secondary path (`SA2`) syntax/error stability,
  - repeated CLOSE15 idempotence and UNTALK cleanup consistency.
- Hard reference trace:
  - runtime: `week38_drive_talkch_close_runtime.csv`
  - reference: `reference/edge/week38_drive_talkch_close_trace.csv`

## Week39 Drive Command-Response Fallback Hard-Ref

- Added dedicated 1541 command-response/status fallback battery across drive revisions (1541/1541C/1541-II) for:
  - TALK+SA15 payload emission when command-response queue is populated,
  - deterministic fallback to status payload once response queue is consumed,
  - post-UNTALK SA15 behavior stability under unchanged status line.
- Hard reference trace:
  - runtime: `week39_drive_cmdresp_fallback_runtime.csv`
  - reference: `reference/edge/week39_drive_cmdresp_fallback_trace.csv`

## Week40 Drive Command-Buffer Commit Hard-Ref

- Added dedicated 1541 command-buffer commit battery across drive revisions (1541/1541C/1541-II) for:
  - SA15 command-channel data accumulation before commit,
  - UNLISTEN-triggered command execution for valid and invalid command payloads,
  - deterministic post-commit command buffer clearing + syntax counter progression.
- Hard reference trace:
  - runtime: `week40_drive_cmdbuf_commit_runtime.csv`
  - reference: `reference/edge/week40_drive_cmdbuf_commit_trace.csv`

## Week41 Drive CLOSE15 Drop/Commit Hard-Ref

- Added dedicated 1541 CLOSE15 drop/commit battery across drive revisions (1541/1541C/1541-II) for:
  - command-buffer drop path when channel 15 is closed before UNLISTEN,
  - clean re-open of LISTEN/SA15 and subsequent data buffering,
  - deterministic UNLISTEN commit for the re-opened command path.
- Hard reference trace:
  - runtime: `week41_drive_close15_drop_runtime.csv`
  - reference: `reference/edge/week41_drive_close15_drop_trace.csv`

## Week42 Drive TALK Status Rebuild Hard-Ref

- Added dedicated 1541 TALK status rebuild battery across drive revisions (1541/1541C/1541-II) for:
  - repeated SA15 status payload generation from evolving status strings,
  - deterministic transition across `00,OK` -> `74,DRIVE NOT READY` -> `30,SYNTAX ERROR`,
  - post-UNTALK SA15 command rejection consistency.
- Hard reference trace:
  - runtime: `week42_drive_status_rebuild_runtime.csv`
  - reference: `reference/edge/week42_drive_status_rebuild_trace.csv`

## Week43 Drive Directory Mode-Filter Hard-Ref

- Added dedicated 1541 directory mode-filter battery across drive revisions (1541/1541C/1541-II) for:
  - mode-positive filter paths (`W`, `R`) with stable row discrimination,
  - negated mode filter (`!W`) consistency,
  - unfiltered baseline (`mode_all`) retention of all SEQ modes.
- Hard reference trace:
  - runtime: `week43_drive_dirmode_runtime.csv`
  - reference: `reference/edge/week43_drive_dirmode_trace.csv`

## Week44 Drive Command-Response Terminator Hard-Ref

- Added dedicated 1541 command-response terminator battery across drive revisions (1541/1541C/1541-II) for:
  - SA15 command-response payload generation with explicit response queue seed,
  - response-queue drain + fallback on immediate re-issue,
  - stable TALK/UNTALK transition after payload/status boundary.
- Hard reference trace:
  - runtime: `week44_drive_cmdresp_term_runtime.csv`
  - reference: `reference/edge/week44_drive_cmdresp_term_trace.csv`

## Week45 Drive Final Freeze Hard-Ref

- Added final 1541 freeze battery across drive revisions (1541/1541C/1541-II) for:
  - command-channel write/read commit chain (`M-W` then `M-R`) under SA15,
  - deterministic payload availability on TALK/SA15,
  - memory-write persistence (`$0400=$AA`, `$0401=$55`) across the full command/talk sequence.
- Hard reference trace:
  - runtime: `week45_drive_final_freeze_runtime.csv`
  - reference: `reference/edge/week45_drive_final_freeze_trace.csv`

## Week46 Drive IEC Timing-Grade Hard-Ref

- Added IEC timing-grade hard reference battery across drive revisions (1541/1541C/1541-II) for:
  - ATN handshake visibility and listener byte-ack progression,
  - EOI pending path with explicit 200-us guard window,
  - deterministic EOI timeout crossing against current timeout tick budget.
- Timing source used for thresholds/gates:
  - `https://janderogee.com/projects/1541-III/files/pdf/IEC_disected-IEC_1541_info.pdf`
  - secondary host-side scope reference: `http://tech.guitarsite.de/c64_scope.html`
- Hard reference trace:
  - runtime: `week46_drive_iec_timing_grade_runtime.csv`
  - reference: `reference/edge/week46_drive_iec_timing_grade_trace.csv`

## Week47 Host Timing Stabilization Hard-Ref

- Added host-side timing stabilization hard reference to anchor IEC-related host behavior for:
  - AEC low-window stability (`week47_host_aec_low_windows`),
  - RDY low pulse cadence (`week47_host_rdy_low_pulses`),
  - DD00 edge-gap jitter guard (`week47_host_dd00_edge_jitter_max`).
- Sources used for host-side expectations:
  - primary host waveform reference: `http://tech.guitarsite.de/c64_scope.html`
  - IEC timing source for cross-check context: `https://janderogee.com/projects/1541-III/files/pdf/IEC_disected-IEC_1541_info.pdf`
- Hard reference trace:
  - runtime: `week47_host_timing_runtime.csv`
  - reference: `reference/edge/week47_host_timing_trace.csv`

## Week48 Drive Core Timing Baseline Hard-Ref

- Added baseline drive core timing instrumentation across drive revisions (1541/1541C/1541-II) for:
  - per-tick drive CPU progress (`cpu_step_count`, `cpu_last_opcode`),
  - explicit VIA access tagging (`via_access`, `via1_ifr`, `via2_ifr`),
  - IEC line side-effect visibility (`iec_effect`) while running deterministic line patterns.
- Hard reference trace:
  - runtime: `week48_drive_core_timing_runtime.csv`
  - reference: `reference/edge/week48_drive_core_timing_trace.csv`

## Week49 Drive CPU Cadence Hard-Ref

- Added drive CPU cadence hard reference across 1541/1541C/1541-II for:
  - per-tick CPU advance visibility (`cpu_advanced`, `cpu_step_count`),
  - PC movement consistency (`pc_advanced`, `pc_advance_events`),
  - cadence spacing guard (`cadence_gap_max`) while IEC lines are toggled deterministically.
- Hard reference trace:
  - runtime: `week49_drive_cpu_cadence_runtime.csv`
  - reference: `reference/edge/week49_drive_cpu_cadence_trace.csv`

## Week50 Drive CPU Opcode Timing Hard-Ref

- Added drive CPU opcode timing hard reference across 1541/1541C/1541-II for:
  - branch timing split between not-taken and page-cross taken path,
  - JSR/RTS cadence and stack-side visibility (`$01FE/$01FF`),
  - per-tick opcode cadence guard (`cadence_gap_max`) under deterministic IEC toggling.
- Hard reference trace:
  - runtime: `week50_drive_cpu_opcode_timing_runtime.csv`
  - reference: `reference/edge/week50_drive_cpu_opcode_timing_trace.csv`

## Week51 VIA6522 Timer/IRQ Window Hard-Ref

- Added VIA6522 timer/IRQ hard reference (drive side) across 1541/1541C/1541-II for:
  - T1 underflow visibility and IFR bit-set ordering,
  - IRQ assert/release window through IFR/IER interaction.
- Hard reference trace:
  - runtime: `week51_via_timer_irq_runtime.csv`
  - reference: `reference/edge/week51_via_timer_irq_trace.csv`

## Week52 VIA6522 Shift Edge/Latch Hard-Ref

- Added VIA6522 shift/latch hard reference (drive side) across 1541/1541C/1541-II for:
  - shift edge progression and bit countdown,
  - shift-complete IFR/IRQ assertion and explicit IRQ clear ordering.
- Hard reference trace:
  - runtime: `week52_via_shift_runtime.csv`
  - reference: `reference/edge/week52_via_shift_trace.csv`

## Week53 CPU<->VIA<->IEC Integration Hard-Ref

- Added integrated drive-side hard reference across 1541/1541C/1541-II that exercises CPU cadence, VIA IRQ windows, and IEC pull-line behavior together in one deterministic trace.
- Coverage focus:
  - CPU progress under mixed I/O pressure (`cpu_step_count`, `cpu_advanced`, `cadence_gap_max`),
  - concurrent VIA observability (`via1_t1_irq`, `via2_sr_irq`, overlap windows),
  - IEC line effects emitted from combined CPU/VIA state (`iec_pull_clk`, `iec_pull_data`, `iec_state`).
- Hard reference trace:
  - runtime: `week53_cpu_via_iec_integration_runtime.csv`
  - reference: `reference/edge/week53_cpu_via_iec_integration_trace.csv`

## Week54 CPU<->VIA<->IEC IRQ Bridge Hard-Ref

- Added a second integration hard reference focused on IRQ bridging behavior across 1541/1541C/1541-II while CPU, VIA, and IEC state evolve together.
- Coverage focus:
  - OR-window visibility of drive-side VIA IRQ sources (`via_irq_or`) while CPU cadence progresses,
  - IEC command/data exchange pressure and talk-side service progression (`iec_rx_processed`, `iec_tx_served`, pending queues),
  - cadence stability guard under integrated load (`cadence_gap_max`).
- Hard reference trace:
  - runtime: `week54_cpu_via_iec_irq_bridge_runtime.csv`
  - reference: `reference/edge/week54_cpu_via_iec_irq_bridge_trace.csv`

## Week55 CPU<->VIA<->IEC Timeout Bridge Hard-Ref

- Added an integration hard reference focused on timeout-path bridging while CPU/VIA/IEC interact deterministically across 1541/1541C/1541-II.
- Coverage focus:
  - explicit EOI/TX/RX timeout-path visibility (`eoi_timeout_count`, `tx_timeout_count`, `rx_timeout_count`),
  - status-line propagation guard for timeout state (`status_code=74` windows),
  - cadence stability under timeout pressure (`cadence_gap_max`) with concurrent VIA IRQ OR activity.
- Hard reference trace:
  - runtime: `week55_cpu_via_iec_timeout_bridge_runtime.csv`
  - reference: `reference/edge/week55_cpu_via_iec_timeout_bridge_trace.csv`

## Week56 Drive IEC Phase-Map Hard-Ref

- Added a phase-mapping hard reference focused on temporal alignment between VIA accesses and IEC side effects while the drive CPU runs deterministic KERNAL-like command/talk/listen paths.
- Coverage focus:
  - access-to-edge temporal mapping (`via_access`, `iec_edge_count`, `last_via_to_iec_latency`),
  - phase-bucket validation across command/listen/talk windows (`phase_bucket`, `iec_state`),
  - integrated CPU cadence and queue progression under IEC path transitions.
- Hard reference trace:
  - runtime: `week56_drive_iec_phase_map_runtime.csv`
  - reference: `reference/edge/week56_drive_iec_phase_map_trace.csv`

## Fase 5 Week54 Pure-Mode Stabilization

- Stabilized pure mode execution for `run_kernel_iec_e2e.ps1 -Mode pure` by hardening the KERNAL IEC bootstrap path when no compat helpers are enabled.
- Added repeat-run pure stability gate (`10-20` runs target) with explicit no-fallback requirement in runner output:
  - `pass_runs=N/N`
  - `host_fallback_no=True`
- Added tolerance metrics for pure stability closure:
  - `week54_pure_stability_runs`
  - `week54_pure_stability_pass_runs`
  - `week54_pure_stability_host_fallback_no`

## Fase 6 Week55 Prerequisito Signal-Level IEC

- Goal: close the prerequisite layer before analog-aware work by freezing CPU/VIA core timing behavior validated in phases 2-5.
- Core-timing freeze policy:
  - tightened tolerance bands to exact deterministic values for key CPU/VIA timing metrics (`week48`..`week56`) and pure-stability metrics.
  - freeze focuses on cycle-accuracy invariance so future signal-level work can be isolated from core timing drift.
- Sequential gate script added:
  - `run_phase6_week55_prereq_gate.ps1`
  - enforced sequence: `prepare -> signoff -> tolerance -> e2e compat`
  - gate is PASS only when all steps are green in order.

## Fase 7 Week57 Scope (Signal-Level IEC Analog-Aware Bootstrap)

- Scope definition:
  - start signal-level IEC validation on top of frozen core timing (Phase 6),
  - keep CPU/VIA cadence deterministic while introducing edge/window observability.
- First signal-level metrics/gates:
  - edge slew/window guard (`week57_iec_signal_edge_slew_max`),
  - polarity mismatch guard (`week57_iec_signal_polarity_mismatch_rows`),
  - turnaround window bounds (`week57_iec_signal_turnaround_min`, `week57_iec_signal_turnaround_max`).
- Hard reference trace:
  - runtime: `week57_iec_signal_window_runtime.csv`
  - reference: `reference/edge/week57_iec_signal_window_trace.csv`

## Week58 IEC Analog-Aware First Refinement

- Introduced the first analog-aware IEC refinement layer (deterministic RC-lite + hysteresis thresholds) on top of frozen core timing.
- Coverage focus:
  - rise/fall edge-duration observability (`*_rise_ticks`, `*_fall_ticks`),
  - polarity/glitch guard during talk/listen transitions,
  - turnaround window bounds under analog-modeled line transitions.
- Hard reference trace:
  - runtime: `week58_iec_analog_edge_model_runtime.csv`
  - reference: `reference/edge/week58_iec_analog_edge_model_trace.csv`

## Week59 IEC Analog-Aware Pulse-Window Refinement

- Added a second analog-aware refinement layer focused on pulse-window observability and turnaround jitter under deterministic line dynamics.
- Coverage focus:
  - pulse-width envelope guards (`week59_iec_analog_pulse_min_ticks`, `week59_iec_analog_pulse_max_ticks`),
  - turnaround jitter bound under mode transitions (`week59_iec_analog_turnaround_jitter_max`),
  - turnaround min/max guard continuity under analog pulse shaping.
- Hard reference trace:
  - runtime: `week59_iec_analog_pulse_window_runtime.csv`
  - reference: `reference/edge/week59_iec_analog_pulse_window_trace.csv`

## Week60 Bus Contention & Drive/Host Interaction

- Added a deterministic contention/release hard-reference layer for host/drive IEC ownership transitions across TALK/LISTEN/EOI windows.
- Coverage focus:
  - contention-window coverage count (`week60_iec_contention_rows`),
  - release ownership latency bound (`week60_iec_release_latency_max`),
  - illegal overlap guard (`week60_iec_illegal_overlap_rows`, target `0`).
- Hard reference trace:
  - runtime: `week60_iec_contention_release_runtime.csv`
  - reference: `reference/edge/week60_iec_contention_release_trace.csv`

## Week61 DOS Path Depth (Real Command Semantics)

- Added a DOS semantic hard-reference layer focused on reducing virtual/scaffold behavior in command-channel M-*/B-* flows and SA15 status sequencing under error/retry.
- Coverage focus:
  - semantic DOS scenario coverage (`week61_drive_dos_semantic_rows`),
  - status-code correctness guard (`week61_drive_status_code_mismatch_rows`, target `0`),
  - retry convergence depth (`week61_drive_cmd_retry_convergence_max`).
- Hard reference trace:
  - runtime: `week61_drive_dos_semantic_runtime.csv`
  - reference: `reference/edge/week61_drive_dos_semantic_trace.csv`

## Week62 Disk Fidelity Bootstrap (GCR-Oriented)

- Added a first disk-fidelity bootstrap hard-reference layer with deterministic GCR/logical-block rotation ticks and baseline physical-style observability.
- Coverage focus:
  - sync-mark detection accumulation (`week62_gcr_sync_detect_rows`),
  - read-window stability/jitter bound (`week62_gcr_read_window_jitter_max`),
  - baseline CRC-error row accumulation (`week62_block_crc_error_rows`).
- Hard reference trace:
  - runtime: `week62_disk_fidelity_gcr_runtime.csv`
  - reference: `reference/edge/week62_disk_fidelity_gcr_trace.csv`

## Physical-Grade Readiness (Post Week62)

- Baseline readiness target after Week62 is met when:
  - Week45..Week62 hard-reference gates are green without regression,
  - `LOAD"$",8` compatibility and pure paths remain PASS with no hidden fallback,
  - GCR bootstrap metrics are deterministic and within policy bounds across revision slots.
- This closes bootstrap scope and enables subsequent phases toward deeper physical disk fidelity.

## Week63 GCR Decode Path (Real Symbol Semantics)

- Added a real-symbol GCR decode hard-reference layer on top of Week62 bootstrap, introducing deterministic symbol decode windows and sync-lock observability.
- Coverage focus:
  - decode-path row coverage (`week63_gcr_decode_rows`),
  - illegal symbol guard (`week63_gcr_illegal_symbol_rows`, target `0`),
  - sync-lock latency bound (`week63_gcr_sync_lock_latency_max`).
- Hard reference trace:
  - runtime: `week63_gcr_decode_path_runtime.csv`
  - reference: `reference/edge/week63_gcr_decode_path_trace.csv`

## Week64 Track Layout Realism (sync/gap/header/data)

- Added a track-layout hard-reference layer with deterministic rotation stride and explicit sync/gap/header/data segmentation checks.
- Coverage focus:
  - track sync density accumulation (`week64_track_sync_density_rows`),
  - gap class mismatch guard (`week64_gap_class_mismatch_rows`, target `0`),
  - header/data boundary guard (`week64_header_data_boundary_errors`, target `0`).
- Hard reference trace:
  - runtime: `week64_track_layout_realism_runtime.csv`
  - reference: `reference/edge/week64_track_layout_realism_trace.csv`

## Week65 CRC/ECC Behavior + Error Map (DOS/channel15 + LOAD path)

- Added a CRC/ECC behavior hard-reference layer with deterministic bad-sector map injection and retry/recovery semantics observable through command channel and LOAD-style block-read path.
- Coverage focus:
  - CRC success row accumulation (`week65_crc_ok_rows`),
  - CRC error row accumulation (`week65_crc_error_rows`),
  - retry recovery convergence bound (`week65_retry_recovery_convergence_max`).
- Hard reference trace:
  - runtime: `week65_crc_ecc_error_map_runtime.csv`
  - reference: `reference/edge/week65_crc_ecc_error_map_trace.csv`

## Week66 CRC Status-Latch + Clear-Latency (channel15 realism)

- Added a CRC status-latch hard-reference layer to model error persistence across retries and explicit clear-on-recovery behavior as observed via channel 15 polling.
- Coverage focus:
  - CRC status latch row accumulation (`week66_crc_status_latch_rows`),
  - retry backoff span bound (`week66_retry_backoff_span_max`),
  - channel 15 clear latency bound (`week66_channel15_clear_latency_max`).
- Hard reference trace:
  - runtime: `week66_crc_status_latch_runtime.csv`
  - reference: `reference/edge/week66_crc_status_latch_trace.csv`

## Week67 CRC Error-Class Map + Recovery Profile

- Added a CRC error-class hard-reference layer that distinguishes deterministic read-error classes across load path retries and validates channel 15 status-class coherence.
- Coverage focus:
  - CRC error-class row accumulation (`week67_crc_error_class_rows`),
  - channel 15 error-class mismatch guard (`week67_channel15_error_class_mismatch_rows`, target `0`),
  - load recovery profile bound (`week67_load_recovery_profile_max`).
- Hard reference trace:
  - runtime: `week67_crc_error_class_runtime.csv`
  - reference: `reference/edge/week67_crc_error_class_trace.csv`

## Week68 Drive CPU Ownership Cutover (phase 1)

- Added a phase-1 CPU ownership hard-reference layer for command-channel/status flow, introducing deterministic observability counters while cutover progressively shifts from scaffold-only handling to CPU/VIA acknowledged command ownership.
- Coverage focus:
  - CPU-owned command accumulation (`w68_cpu_owned_cmd_rows`),
  - scaffold fallback accumulation (`w68_scaffold_fallback_rows`, target low),
  - command/status divergence guard (`w68_cmd_status_divergence_rows`, target `0`).
- Hard reference trace:
  - runtime: `week68_drive_cpu_ownership_runtime.csv`
  - reference: `reference/edge/week68_drive_cpu_ownership_trace.csv`

## Week69 VIA 6522 Timing-Grade Expansion (shift/timer/handshake)

- Added a VIA timing-grade hard-reference stress layer focused on realistic shift/timer/handshake edge behavior and IEC-facing timing impact across drive revisions.
- Coverage focus:
  - shift phase mismatch guard (`w69_via_shift_phase_mismatch_rows`, target `0`),
  - timer IRQ jitter bound (`w69_via_timer_irq_jitter_max`),
  - handshake stall guard (`w69_handshake_stall_rows`, target `0`).
- Hard reference trace:
  - runtime: `week69_via_timing_grade_runtime.csv`
  - reference: `reference/edge/week69_via_timing_grade_trace.csv`

## Week70 GCR Read Pipeline Full Chain (track-aware sector sweep)

- Added a full-chain GCR read hard-reference layer that models deterministic pipeline progression per sector (`sync scan -> header decode -> data decode -> checksum/CRC -> status`) with track-layout-aware sector sweep.
- Coverage focus:
  - header decode accumulation (`w70_gcr_header_decode_rows`),
  - data decode accumulation (`w70_gcr_data_decode_rows`),
  - pipeline chain-break guard (`w70_gcr_chain_break_rows`, target `0`).
- Hard reference trace:
  - runtime: `week70_gcr_read_pipeline_runtime.csv`
  - reference: `reference/edge/week70_gcr_read_pipeline_trace.csv`

## Week71 Write Path + Read-After-Write Realism (drift-controlled)

- Added a write-path hard-reference layer with deterministic GCR-style RAW roundtrip verification on the following rotation, including controlled drift and bounded retry behavior.
- Coverage focus:
  - RAW roundtrip success accumulation (`w71_raw_roundtrip_ok_rows`),
  - write verify fail guard (`w71_write_verify_fail_rows`, target `0` on baseline),
  - post-write retry depth bound (`w71_postwrite_retry_max`).
- Hard reference trace:
  - runtime: `week71_gcr_write_roundtrip_runtime.csv`
  - reference: `reference/edge/week71_gcr_write_roundtrip_trace.csv`

## Week72 Physical Disk Effects Model (zones/jitter/slip/weak-bits)

- Added a physical-disk hard-reference layer that models track-band speed zones, bounded parametric timing jitter, controlled bit-slip injection, and weak-bit observation windows.
- Coverage focus:
  - zone timing span bound (`w72_zone_timing_span_max`),
  - bit-slip accumulation (`w72_bitslip_events_rows`),
  - weak-bit observation accumulation (`w72_weakbit_observed_rows`).
- Gate focus:
  - stable zone-span bands across revisions,
  - no regression in LOAD/e2e signoff suites.
- Hard reference trace:
  - runtime: `week72_physical_disk_effects_runtime.csv`
  - reference: `reference/edge/week72_physical_disk_effects_trace.csv`

## Week73 Media Aging / Long-Run Drift Envelope (Error Engine + DOS mapping)

- Added an extended error-engine hard-reference layer for long-run media aging envelope checks, with complete DOS-class mapping matrix (`23/27/29/20/21/74` where applicable), persistence semantics, and clear/recovery behavior.
- Coverage focus:
  - error-class coverage accumulation (`w73_errorclass_coverage_rows`),
  - channel-15 mapping mismatch guard (`w73_channel15_mapping_mismatch_rows`, target `0`),
  - retry/convergence bound (`w73_recovery_profile_max`).
- Hard reference trace:
  - runtime: `week73_error_engine_dos_mapping_runtime.csv`
  - reference: `reference/edge/week73_error_engine_dos_mapping_trace.csv`

## Week74 Image Fidelity (G64/NIB parity + lossy-transform gate)

- Added an image-fidelity hard-reference layer focused on format parity for first-class G64/NIB ingest paths across drive revisions.
- Coverage focus:
  - G64 feature parity accumulation (`w74_g64_feature_parity_rows`),
  - NIB parity accumulation (`w74_nib_parity_rows`),
  - lossy transform guard on target subset (`w74_image_lossy_transform_rows`, target `0`).
- Hard reference trace:
  - runtime: `week74_image_fidelity_runtime.csv`
  - reference: `reference/edge/week74_image_fidelity_trace.csv`

## Week75 Compatibility Signoff (real software corpus)

- Added a compatibility signoff hard-reference layer oriented to real loader/software corpus behavior across revisions and profiles.
- Coverage focus:
  - real corpus pass-rate (`w75_real_corpus_pass_rate`, gate `>=95`),
  - loader timing regression accumulation (`w75_loader_timing_regressions`),
  - host fallback guard (`w75_host_fallback_rows`, target `0`).
- Hard reference trace:
  - runtime: `week75_compatibility_signoff_runtime.csv`
  - reference: `reference/edge/week75_compatibility_signoff_trace.csv`
- Signoff target:
  - final status line includes `1541 physical-grade beta` when all gates are green.

## Week76 Compatibility Drift Envelope (cross-profile replay)

- Added a compatibility drift hard-reference layer that extends the real-corpus signoff with deterministic cross-profile replay windows.
- Coverage focus:
  - cross-profile stability floor (`w76_cross_profile_stability_rate`, gate `>=98`),
  - loader drift regression bound (`w76_loader_drift_regressions`),
  - host fallback guard (`w76_host_fallback_rows`, target `0`).
- Hard reference trace:
  - runtime: `week76_compatibility_drift_runtime.csv`
  - reference: `reference/edge/week76_compatibility_drift_trace.csv`

## Week77 Real Corpus Bridge (loader families integration gate)

- Added a bridge hard-reference layer toward full real-software closure, combining profile/revision runs across representative loader families.
- Coverage focus:
  - real corpus coverage accumulation (`w77_real_corpus_coverage_rows`),
  - loader timing regression bound (`w77_loader_timing_regressions`),
  - host fallback guard (`w77_host_fallback_rows`, target `0`).
- Hard reference trace:
  - runtime: `week77_real_corpus_bridge_runtime.csv`
  - reference: `reference/edge/week77_real_corpus_bridge_trace.csv`

## Week78 Real Disk Corpus (in-repo images)

- Added a first real-disk corpus hard-reference layer using concrete in-repo images under `roms/tsuit215` to reduce synthetic-only coverage.
- Coverage focus:
  - real corpus coverage accumulation (`w78_real_corpus_coverage_rows`),
  - loader timing regression bound (`w78_loader_timing_regressions`),
  - host fallback guard (`w78_host_fallback_rows`, target `0`),
  - format coverage accumulation (`w78_format_coverage_rows`, current corpus: D64).
- Hard reference trace:
  - runtime: `week78_real_disk_corpus_runtime.csv`
  - reference: `reference/edge/week78_real_disk_corpus_trace.csv`

## Exit Criteria Results

- `strict/full` green: PASS
- `pure/compat/fast` green: PASS
- no hidden fallback:
  - 1541 IEC E2E asserts if host fallback path is used.
  - Current run reports `host_fallback=no`.
- multi-domain drift test stable:
  - Week45 deterministic replay PASS with stable digest.
- BA/AEC edge handoff hard-ref (Week15): PASS
- cross-domain edge hard-ref + temporal fuzz determinism (Week16): PASS

## No Default-On Hacks Policy

- `run_kernel_iec_e2e.ps1` now defaults to pure mode with no compatibility/test helpers enabled by default.
- Test-only and compat helper flags are opt-in and explicit via parameters.
- `run_signoff_week13_14.ps1` passes any helper flags explicitly at call-site so there are no implicit default-on hacks.
- `run_signoff_week13_14.ps1` accepts `-FastManifest` and uses it for fast external validation (fallback to `-Manifest` if missing).

## Subcycle Exact Completo Criteria

The project is considered "Subcycle Exact Completo" when all of the following hold:

1. **Boundary-correct interrupts**
   - Week12 interrupt-boundary suite has zero mismatches.
   - Covers prefetch boundary and BRK/RTI/PLP/SEI/CLI edge ordering.
2. **PHI1/PHI2 ordering integrity**
   - Critical interrupt micro-ops keep expected PHI phase ordering.
3. **CPU timing + legality coverage**
   - Official + unofficial CPU corpus passes in strict profile.
4. **VIC determinism**
   - VIC checklist passes with stable frame hash + raster-event digest.
5. **CIA determinism**
   - CIA battery digest remains stable across repeated strict runs.
6. **IEC/1541 end-to-end correctness**
   - Command/data/status suites and KERNAL `LOAD"$",8` E2E pass.
7. **Cross-profile stability**
   - fast/strict/full + pure/compat all green without profile-only hacks.
8. **Reference-diff readiness**
   - Golden corpus supports per-test VICE/x64sc trace diff with mismatch thresholds.
9. **BA/AEC edge handoff hard-ref**
   - Dedicated edge trace must match strict row-by-row reference.
10. **Cross-domain stress determinism**
   - Combined VIC/CPU/CIA edge trace and seed-based fuzz digests are deterministic.

## Next Step

- Add automated capture/refresh pipeline for new `reference/edge/*.csv` artifacts (same bootstrap discipline as VICE refs).
- Continue replacing any remaining proxy references with direct VICE/x64sc captures where feasible.

## Week47 Proposal (Host-Side Timing Stabilization)

- Goal:
  - add host-side timing checks that support IEC robustness in pure mode, without changing protocol semantics.
- Proposed metrics:
  - `week47_host_aec_low_windows`: count stable AEC-low windows during KERNAL IEC E2E (must be non-zero and deterministic banded).
  - `week47_host_rdy_low_pulses`: count RDY low pulses and bound drift against the same run profile.
  - `week47_host_dd00_edge_jitter_max`: max edge-gap jitter from DD00 transition history during E2E.
- Initial policy strategy:
  - start with wide min/max tolerance bands on first bootstrap,
  - tighten after 3-5 consecutive green runs.
- Sources:
  - primary IEC timing reference: `https://janderogee.com/projects/1541-III/files/pdf/IEC_disected-IEC_1541_info.pdf`
  - host waveforms/reference behavior: `http://tech.guitarsite.de/c64_scope.html`

## Reference Refresh Commands

- `run_prepare_pc_only_references.ps1`
  - rebuilds pc_only references for timing/cia/vicii runtime traces.
- `run_prepare_edge_references.ps1`
  - boots strict with explicit edge bootstrap flags and refreshes:
    - `reference/edge/week15_baaec_handoff_trace.csv`
    - `reference/edge/week16_cross_domain_trace.csv`
    - `reference/edge/week18_openbus_revision_trace.csv`
    - `reference/edge/week19_cia_dense_trace.csv`
    - `reference/edge/week20_vic_pathological_trace.csv`
    - `reference/edge/week21_bus_corner_trace.csv`
    - `reference/edge/week22_port_map_trace.csv`
    - `reference/edge/week23_cia_irq_nmi_trace.csv`
    - `reference/edge/week24_irq_latch_trace.csv`
    - `reference/edge/week25_cia_serial_trace.csv`
    - `reference/edge/week26_drive_iec_trace.csv`
    - `reference/edge/week27_drive_cmdphase_trace.csv`
    - `reference/edge/week28_drive_eoi_atn_trace.csv`
    - `reference/edge/week29_drive_timeout_trace.csv`
    - `reference/edge/week30_drive_cmdch_trace.csv`
    - `reference/edge/week31_drive_status_talk_trace.csv`
    - `reference/edge/week32_drive_dir_stream_trace.csv`
    - `reference/edge/week33_drive_dir_filter_trace.csv`
    - `reference/edge/week34_drive_alloc_map_trace.csv`
    - `reference/edge/week35_drive_ptr_dir_trace.csv`
    - `reference/edge/week36_drive_catalog_trace.csv`
    - `reference/edge/week37_drive_atn_gate_trace.csv`
    - `reference/edge/week38_drive_talkch_close_trace.csv`
    - `reference/edge/week39_drive_cmdresp_fallback_trace.csv`
    - `reference/edge/week40_drive_cmdbuf_commit_trace.csv`
    - `reference/edge/week41_drive_close15_drop_trace.csv`
    - `reference/edge/week42_drive_status_rebuild_trace.csv`
    - `reference/edge/week43_drive_dirmode_trace.csv`
    - `reference/edge/week44_drive_cmdresp_term_trace.csv`
    - `reference/edge/week45_drive_final_freeze_trace.csv`
    - `reference/edge/week46_drive_iec_timing_grade_trace.csv`
    - `reference/edge/week47_host_timing_trace.csv`
    - `reference/edge/week48_drive_core_timing_trace.csv`
    - `reference/edge/week49_drive_cpu_cadence_trace.csv`
    - `reference/edge/week50_drive_cpu_opcode_timing_trace.csv`
    - `reference/edge/week51_via_timer_irq_trace.csv`
    - `reference/edge/week52_via_shift_trace.csv`
    - `reference/edge/week53_cpu_via_iec_integration_trace.csv`
    - `reference/edge/week54_cpu_via_iec_irq_bridge_trace.csv`
    - `reference/edge/week55_cpu_via_iec_timeout_bridge_trace.csv`
    - `reference/edge/week56_drive_iec_phase_map_trace.csv`
    - `reference/edge/week57_iec_signal_window_trace.csv`
    - `reference/edge/week58_iec_analog_edge_model_trace.csv`
    - `reference/edge/week59_iec_analog_pulse_window_trace.csv`
    - `reference/edge/week60_iec_contention_release_trace.csv`
    - `reference/edge/week61_drive_dos_semantic_trace.csv`
    - `reference/edge/week62_disk_fidelity_gcr_trace.csv`
    - `reference/edge/week63_gcr_decode_path_trace.csv`
    - `reference/edge/week64_track_layout_realism_trace.csv`
    - `reference/edge/week65_crc_ecc_error_map_trace.csv`
    - `reference/edge/week66_crc_status_latch_trace.csv`
    - `reference/edge/week67_crc_error_class_trace.csv`
    - `reference/edge/week68_drive_cpu_ownership_trace.csv`
    - `reference/edge/week69_via_timing_grade_trace.csv`
    - `reference/edge/week70_gcr_read_pipeline_trace.csv`
    - `reference/edge/week71_gcr_write_roundtrip_trace.csv`
    - `reference/edge/week72_physical_disk_effects_trace.csv`
    - `reference/edge/week73_error_engine_dos_mapping_trace.csv`
    - `reference/edge/week74_image_fidelity_trace.csv`
    - `reference/edge/week75_compatibility_signoff_trace.csv`
    - `reference/edge/week76_compatibility_drift_trace.csv`
    - `reference/edge/week77_real_corpus_bridge_trace.csv`
    - `reference/edge/week78_real_disk_corpus_trace.csv`
    - `reference/vice/c64_lorenz_brkn_edge_ref.trace.csv` (pc_only)
- `run_prepare_pla_snapshot.ps1`
  - boots strict with `VIC_EXPORT_PLA_SPEC=1` and refreshes:
    - `reference/edge/vic_pla_spec.csv`
    - `reference/edge/vic_pla_snapshot.md`
- `run_check_pla_snapshot.ps1`
  - fast digest consistency check between:
    - `reference/edge/vic_pla_spec.csv`
    - `reference/edge/vic_pla_snapshot.md`

## CI Manual Job

- Added GitHub Actions manual workflow: `.github/workflows/edge-reference-refresh.yml`
  - trigger: `workflow_dispatch`
  - runs `run_prepare_edge_references.ps1` + `run_prepare_pla_snapshot.ps1` (with optional strict rebuild)
  - uploads refreshed edge/reference artifacts (including PLA snapshot) for review
- Added GitHub Actions manual workflow: `.github/workflows/pla-snapshot-check.yml`
  - trigger: `workflow_dispatch`
  - runs only `run_check_pla_snapshot.ps1` for a quick PLA digest gate

## Revision Tolerance Policy

- Added policy file: `reference/edge/revision_tolerance_policy.json`
- Signoff now emits metrics file: `reference/edge/revision_tolerance_metrics.json`
- New checker script: `run_check_revision_tolerance.ps1`
  - executes `tools/check_revision_tolerance.py` and enforces min/max bands for software-level revision metrics.
- Added manual CI workflow: `.github/workflows/revision-tolerance-check.yml`
  - runs full signoff, generates metrics, then enforces tolerance policy gate.
