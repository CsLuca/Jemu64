# Phase 5 Final Signoff

## Status

- Phase 5 final signoff status: **READY**
- Closure level: **near-closure strengthened with hard gates + soak + expanded corpus**

## Final Gate Contract

The final phase5 contract is considered satisfied when all checks below are green.

1. `run_signoff_week13_14.ps1`
2. `run_copier_matrix.ps1`
3. `run_advanced_real_corpus_gate.ps1`
4. `run_advanced_dos_recovery_gate.ps1`
5. `run_phase5_soak_gate.ps1`
6. `run_check_phase5_hard_thresholds.ps1`
7. `run_check_phase5_closure.ps1 -GenerateChecklist`

## Mandatory Criteria

- Matrix pass requirement: all scenarios must pass (`overall_pass=1`).
- Hard scenario requirement: all hard scenarios must pass.
- Real corpus requirement: expanded corpus list must pass entirely with no host fallback regressions.
- DOS recovery requirement: advanced DOS recovery hardset must pass.
- Soak requirement: flake rate must stay at or below configured threshold.
- Closure requirement: auto-generated support matrix must contain all matrix scenario IDs.

## Active Baseline Values

- Matrix scenarios: 27
- Soak runs: 6
- Max flake threshold: 0.05
- Expanded advanced real corpus titles: 16
- Advanced DOS recovery hardset scenarios: 2

## Artifacts

- `copier_matrix_report.json`
- `copier_matrix_report.csv`
- `advanced_real_corpus_runtime.csv`
- `advanced_dos_recovery_runtime.csv`
- `phase5_soak_runtime.csv`
- `COPIER_SUPPORT_MATRIX.md` (auto-generated)

## Notes

- This signoff certifies stability under current gate set and dataset.
- Further parity improvements for long-tail copier/protection cases can continue without reopening the phase5 gate framework.
