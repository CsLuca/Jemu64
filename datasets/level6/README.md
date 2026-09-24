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
