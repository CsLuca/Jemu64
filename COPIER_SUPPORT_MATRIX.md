# Copier Support Matrix

## Scope

Auto-generated from runtime report + matrix contract.

- Matrix source: `copier_matrix.json`
- Runtime report: `copier_matrix_report.json`
- Summary: `PASS 29/29`

## Scenario Status

| Scenario ID | Level | Mode | Format | Status |
|---|---|---|---|---|
| copy_8_to_9_file_e2e | baseline | file_copy | d64->d64 | PASS |
| copy_8_to_9_disk_e2e | baseline | disk_copy | d64->d64 | PASS |
| advanced_g64_mount_baseline | advanced | file_copy | g64->g64 | PASS |
| advanced_nib_mount_baseline | advanced | file_copy | nib->nib | PASS |
| advanced_raw_mount_baseline | advanced | file_copy | raw->raw | PASS |
| advanced_g64_write_protect_baseline | advanced | file_copy | g64->g64 | PASS |
| advanced_nib_write_protect_baseline | advanced | file_copy | nib->nib | PASS |
| advanced_g64_weakbit_baseline | advanced | file_copy | g64->g64 | PASS |
| advanced_nib_weakbit_baseline | advanced | file_copy | nib->nib | PASS |
| advanced_g64_parser_baseline | advanced | file_copy | g64->g64 | PASS |
| advanced_nib_parser_baseline | advanced | file_copy | nib->nib | PASS |
| advanced_g64_gcr_hard_baseline | advanced | file_copy | g64->g64 | PASS |
| advanced_nib_gcr_hard_baseline | advanced | file_copy | nib->nib | PASS |
| advanced_g64_dos_error_map_hard_baseline | advanced | file_copy | g64->g64 | PASS |
| advanced_nib_dos_error_map_hard_baseline | advanced | file_copy | nib->nib | PASS |
| advanced_g64_multitrack_halftrack_hard_baseline | advanced | file_copy | g64->g64 | PASS |
| advanced_nib_multitrack_hard_baseline | advanced | file_copy | nib->nib | PASS |
| advanced_g64_sync_relock_drift_hard_baseline | advanced | file_copy | g64->g64 | PASS |
| advanced_nib_sync_relock_drift_hard_baseline | advanced | file_copy | nib->nib | PASS |
| advanced_g64_jitter_window_hard_baseline | advanced | file_copy | g64->g64 | PASS |
| advanced_nib_jitter_window_hard_baseline | advanced | file_copy | nib->nib | PASS |
| advanced_g64_relock_window_soak_hard_baseline | advanced | file_copy | g64->g64 | PASS |
| advanced_nib_relock_window_soak_hard_baseline | advanced | file_copy | nib->nib | PASS |
| advanced_g64_flux_hysteresis_hard_baseline | advanced | file_copy | g64->g64 | PASS |
| advanced_nib_flux_hysteresis_hard_baseline | advanced | file_copy | nib->nib | PASS |
| advanced_g64_dos_recovery_hard_baseline | advanced | file_copy | g64->g64 | PASS |
| advanced_nib_dos_recovery_hard_baseline | advanced | file_copy | nib->nib | PASS |
| advanced_g64_longtail_burst_hard_baseline | advanced | file_copy | g64->g64 | PASS |
| advanced_nib_longtail_burst_hard_baseline | advanced | file_copy | nib->nib | PASS |

## Quasi-Closure Criteria (Phase 5)

- `run_signoff_week13_14.ps1` remains green with matrix gate enabled.
- `run_copier_matrix.ps1` remains full-pass on active scenario set.
- `run_check_phase5_closure.ps1` and `run_check_phase5_hard_thresholds.ps1` remain green.

## Freeze Statement

Phase 5 is currently considered **near-closure achieved** under frozen gate conditions.
