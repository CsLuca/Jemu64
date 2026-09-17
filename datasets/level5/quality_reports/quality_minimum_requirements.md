# Level5 Dump Quality Minimums

Use this checklist before promoting a capture into Level5 manifests.

## Required

- Integrity: flux file hash present and matches file bytes.
- Coverage: requested track range captured and readable.
- Metadata: required fields complete (no critical unknown values).
- Oracle: timing/IEC/error/loader/weak-sync sections present.

## Thresholds

- `rpm_stddev <= 1.5`
- `max_retry_count == 0` for strict baseline titles
- `parity_max_mismatch_rows == 0`
- `max_unexpected_error_rows == 0`
- `min_weak_events >= 25` for weak-bit stress titles
- `min_sync_recovery_events >= 15` for sync-loss stress titles

## Repeatability

- At least 2 captures per side (3+ recommended).
- Inter-capture KPI spread must remain within project budget.

## Promotion Rule

- A title is eligible for Level5 manifest promotion only when all required checks pass.
