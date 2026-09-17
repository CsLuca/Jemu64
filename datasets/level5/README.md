# Level5 Dataset Workspace

This folder hosts templates and schemas for building a Level5 cycle/analog-accurate
validation corpus.

## Layout

- `raw_flux/`: raw captures (`.raw`, `.scp`, equivalent low-level dumps)
- `images_derived/`: optional derived images (`g64`, `nib`, `d64`)
- `metadata/`: one metadata JSON per capture session/dump
- `oracles/`: one oracle JSON per title/side target behavior
- `calibration/`: drive/environment calibration snapshots
- `quality_reports/`: generated quality gate reports
- `manifests/`: corpus manifests used by automation
- `schemas/`: JSON schema files for validation
- `templates/`: starter JSON files

## Naming

Recommended base filename:

`<title>_<side>_<drive_model>_<dump_id>_<rpm>_<date>`

Example:

`summer_games_sideA_1541c_dump01_300rpm_2026-09-17`

Related files:

- `<base>.raw`
- `<base>.metadata.json`
- `<base>.oracle.json`

## Automated Quality Gate

Run:

`./run_level5_dataset_quality_gate.ps1 -Manifest datasets/level5/manifests/level5_dataset_manifest.sample.json`

Outputs:

- `datasets/level5/quality_reports/level5_dataset_quality_report.json`
- `datasets/level5/quality_reports/level5_dataset_quality_report.csv`

The gate enforces minimum thresholds from `quality_reports/quality_minimum_requirements.md`.

## Real Onboarding Assets

- Roadmap: `LEVEL5_ROADMAP.md`
- Seed manifest: `datasets/level5/manifests/level5_real_seed_manifest.json`
- Acquisition backlog: `datasets/level5/manifests/level5_acquisition_backlog.csv`
