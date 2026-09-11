# Copier Support Matrix

## Scope

This document tracks the current compatibility closure level for copier-related scenarios,
based on the active runtime markers and `copier_matrix.json` gate definitions.

## Current Gate Snapshot

- Matrix source: `copier_matrix.json`
- Runtime evaluator: `run_copier_matrix.ps1`
- Latest local hard baseline result: `pass=23/23`

## Scenario Status

### D64 Core

- `copy_8_to_9_file_e2e`: `PASS`
- `copy_8_to_9_disk_e2e`: `PASS`

### Advanced Format Baseline

- `advanced_g64_mount_baseline`: `PASS`
- `advanced_nib_mount_baseline`: `PASS`
- `advanced_raw_mount_baseline`: `PASS`
- `advanced_g64_write_protect_baseline`: `PASS`
- `advanced_nib_write_protect_baseline`: `PASS`
- `advanced_g64_weakbit_baseline`: `PASS`
- `advanced_nib_weakbit_baseline`: `PASS`
- `advanced_g64_parser_baseline`: `PASS`
- `advanced_nib_parser_baseline`: `PASS`

### Hard GCR + Error Map

- `advanced_g64_gcr_hard_baseline`: `PASS`
- `advanced_nib_gcr_hard_baseline`: `PASS`
- `advanced_g64_dos_error_map_hard_baseline`: `PASS`
- `advanced_nib_dos_error_map_hard_baseline`: `PASS`

### Hard Multi-Track / Relock / Jitter

- `advanced_g64_multitrack_halftrack_hard_baseline`: `PASS`
- `advanced_nib_multitrack_hard_baseline`: `PASS`
- `advanced_g64_sync_relock_drift_hard_baseline`: `PASS`
- `advanced_nib_sync_relock_drift_hard_baseline`: `PASS`
- `advanced_g64_jitter_window_hard_baseline`: `PASS`
- `advanced_nib_jitter_window_hard_baseline`: `PASS`
- `advanced_g64_relock_window_soak_hard_baseline`: `PASS`
- `advanced_nib_relock_window_soak_hard_baseline`: `PASS`

## Quasi-Closure Criteria (Phase 5)

The project can be considered near closure for phase 5 when all of the following hold:

- `run_signoff_week13_14.ps1` remains green on fast/strict/full profiles.
- `run_copier_matrix.ps1` remains full-pass on the hard matrix profile set.
- No regression in mandatory runtime markers for D64 and advanced format scenarios.
- No host fallback regressions in matrix/signoff contract.

## Freeze Statement

Phase 5 is currently considered **near-closure achieved** under frozen gate conditions.

Frozen conditions snapshot:

- `run_signoff_week13_14.ps1` is green with active copier matrix gating.
- `run_copier_matrix.ps1` is full-pass on the active hard scenario set.
- `run_check_phase5_closure.ps1` is green (matrix/report/checklist consistency).
- CI workflows enforce the same phase5 closure gate and publish checklist artifacts.

Freeze does not imply final physical parity completion; it certifies current hard-gate contract stability.

## Remaining Work Before Full Closure

- Introduce format-native edge corpus cases with real-world stress traces for `g64/nib/raw`.
- Expand hard scenarios from synthetic stress to corpus-driven timing/loader parity checks.
- Lock CI publication/consumption of matrix status as release checklist input.
