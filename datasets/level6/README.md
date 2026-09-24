# Level6 Dataset Workflow

## Synthetic-First Calibration Path

When direct hardware measurements are not yet available, use the synthetic profile workflow:

1. Start from baseline profiles (`baseline_1541`, `baseline_1541c`, `baseline_1541ii`).
2. Add candidate profiles that emulate expected physical stress envelopes (long cable, weak pull-up, noisy bus).
3. Run synthetic envelope gating to keep behavior and performance inside budget.

Manifest:

- `datasets/level6/manifests/level6_synthetic_profiles_manifest.json`

Gate script:

- `run_level6_synthetic_envelope_gate.ps1`

## Future Real-Capture Contract (Planned)

When real captures become available, keep this structure:

- `datasets/level6/captures/raw/` for original acquisitions,
- `datasets/level6/captures/normalized/` for normalized edge timelines,
- per-device profile overlays in `config/iec_profiles/real_<unit>.json`.

Required metadata for future captures:

- machine/revision identifier,
- drive identifier and board revision,
- cable type/length,
- probe/tool model and sample rate,
- temperature range during acquisition,
- scenario id (`load_dir`, `atn_toggle`, `eoi_ack`, `hotplug`, etc.).

## Calibration Scaffolding (Phase 3)

Calibration script:

- `run_level6_calibrate_profile.ps1`

Inputs:

- capture calibration manifest (example):
  - `datasets/level6/manifests/level6_capture_calibration_manifest_sample.json`
- normalized capture metric files (example placeholders):
  - `datasets/level6/captures/normalized/sample_capture_1541_short.json`
  - `datasets/level6/captures/normalized/sample_capture_1541_long.json`

Example:

```powershell
& ".\run_level6_calibrate_profile.ps1" `
  -Manifest "datasets/level6/manifests/level6_capture_calibration_manifest_sample.json" `
  -BaseProfile "config/iec_profiles/baseline_1541.json" `
  -OutputProfile "config/iec_profiles/calibrated_synthetic_1541.json" `
  -ProfileId "calibrated_synthetic_1541"
```

Outputs:

- calibrated profile JSON (default): `config/iec_profiles/calibrated_synthetic_1541.json`
- fit report (default): `datasets/level6/quality_reports/level6_calibration_fit_metrics.json`

Current fitter behavior:

- weighted-average fit over capture metrics,
- optional base-profile blending (`-BlendWithBase`) for conservative convergence,
- emits line + analog + node-level skew/tau calibration values.

## Calibration Gate

One-command gate (calibrate + run physical-l6 with calibrated profile):

```powershell
& ".\run_level6_calibration_gate.ps1"
```

Default output folder:

- `datasets/level6/quality_reports/calibration_gate/`

## Hardening Gate (Final)

One-command hardening run (calibration gate + profile drift budgets):

```powershell
& ".\run_level6_hardening_gate.ps1"
```

Default output folder:

- `datasets/level6/quality_reports/hardening_gate/`

Produced artifacts:

- `level6_hardening_gate_metrics.json`
- `level6_hardening_gate_runtime.csv`
