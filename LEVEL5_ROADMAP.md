# 1541 Level5 Roadmap

This roadmap defines the path from current Level4 status to Level5 cycle/analog-accurate validation.

## Scope

- Keep 1541 architecture independent from C64 core.
- Tighten coupling only at shared timing and IEC edge boundaries.
- Extend physical fidelity in flux/write path with measurable budgets.
- Promote only with differential multi-oracle evidence on real protected corpus.

## Phases

## L5.0 Data Foundation

Goal: ingest a real flux corpus with complete metadata/oracle coverage and quality gates.

Deliverables:

- `datasets/level5/` populated with real captures.
- `run_level5_dataset_quality_gate.ps1` green on seeded corpus.
- Initial real manifest: `datasets/level5/manifests/level5_real_seed_manifest.json`.

Exit criteria:

- Metadata + oracle completeness: 100% on seed set.
- Quality gate pass rate: >= 95% during ingestion, 100% for promotion candidates.

## L5.1 Sub-cycle Coupling

Goal: tighter CPU/VIA/IEC/drive coupling at cycle/sub-cycle granularity.

Deliverables:

- Shared high-resolution timestamp discipline for C64 and drive domains.
- Deterministic IEC edge ordering under sub-cycle arbitration.
- New gate: `run_level5_cycle_coupling_gate.ps1`.

Exit criteria:

- Timing mismatch budget met on hard subset (target zero mismatch rows).

## L5.2 Analog Flux/Write Model

Goal: physically stronger write/read model with long-horizon drift.

Deliverables:

- Write hysteresis and long-term thermal drift model.
- Persistent multi-pass write memory per track.
- Revision-aware calibration for 1541/1541C/1541-II.
- New gate: `run_level5_flux_analog_gate.ps1`.

Exit criteria:

- Loader parity and DOS error-map budgets met across real protected set.

## L5.3 Differential Multi-Oracle

Goal: validate behavior against multiple independent oracles.

Deliverables:

- Per-title timing + IEC + DOS + weak/sync oracle checks.
- Cross-profile differential report L3/L4/L5.
- New gate: `run_level5_diff_oracle_gate.ps1`.

Exit criteria:

- Differential spread within budget for all promoted titles.

## L5.4 Chaos/Soak Promotion

Goal: prove long-run stability and deterministic behavior.

Deliverables:

- Long soak matrix and chaos runner under Level5 profile.
- New gate: `run_level5_chaos_soak.ps1`.
- CI integration with artifact publication and promotion snapshots.

Exit criteria:

- Flake rate <= 0.05 (or stricter project budget).
- Full regression stack remains green.

## Current Promotion Policy Note

- Development snapshots may use `L5 promoted (cap02_deferred)` when software gates are green but physical corpus closure is pending.
- `cap02_deferred` is not final physical promotion: rows with missing multi-capture/calibration remain blocked in `datasets/level5/manifests/level5_real20_physical_readiness_tracker.csv`.
- Final L5 promotion requires full physical closure (`ready_for_l5_promotion=yes` on target corpus rows).

## KPI Targets

- Protected real corpus pass rate: 100%.
- Differential oracle spread: <= configured budget (target 0 for critical classes).
- Host fallback on pure path: 0.
- Soak flake rate: <= 0.05.
- Legacy gates and Level3 freeze checks: no regressions.

## Suggested Commit Plan

1. `feat(1541-l5): add real dataset manifests and quality budgets`
2. `feat(1541-l5): add sub-cycle coupling scheduler and IEC edge timing gate`
3. `feat(1541-l5): add analog write hysteresis and thermal drift model`
4. `test(1541-l5): add differential multi-oracle validation gate`
5. `ci(1541-l5): add chaos soak and promotion checks`
