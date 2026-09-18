# Level5 Data Foundation Status

Date: 2026-09-18

## Gate Results

- `level5_official_testset_v1_manifest.json`: PASS `8/8`
- `level5_real_manifest_v1.json`: PASS `20/20`
- `level5_real_raw_seed_manifest_v1.json`: PASS `4/4`
- `level5_copyii_multicapture_manifest_v1.json`: PASS `4/4`
- `level5_new_archive_raw_batch_manifest_v1.json`: PASS `14/14`

## L5.0 Data Foundation Progress

- Ingestion quality gates are green for all active manifests used in onboarding and official v1 test set.
- Real raw titles promoted into official v1 test set with bootstrap metadata/oracle:
  - `l5_archive_billbudgepinballconstructionsetcommodore64` (A, cap01+cap02)
  - `l5_archive_transylvania_c_64_atari` (A/B, cap01)
  - `l5_archive_kane_human_race` (A/B, cap01)
  - `l5_archive_aztec_challenge_c64` (A/B, cap01)
- Tracker ingestion status for these raw titles is now `pass`.

## Remaining Gaps Before Final L5 Promotion

- `ready_for_l5_promotion` remains `no` for all rows pending full physical calibration closure.
- Missing cap02 on A/B for:
  - `l5_archive_transylvania_c_64_atari`
  - `l5_archive_kane_human_race`
  - `l5_archive_aztec_challenge_c64`
- Metadata/oracle currently `partial` on archive-imported raw titles and require same-media Greaseweazle calibration for final promotion.
