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
