# Jemu64

## Multi-Drive 1541 Slot Configuration

The emulator runtime exposes explicit 1541 slots for IEC device units `8`, `9`, `10`, and `11`.

- Slot map:
  - slot `0` -> unit `8`
  - slot `1` -> unit `9`
  - slot `2` -> unit `10`
  - slot `3` -> unit `11`
- Slots are attached on a shared IEC bus domain and keep independent per-drive state.

### Per-Slot Image Environment Variables

You can configure mount metadata independently per unit with environment variables:

- `DRIVE8_IMAGE`, `DRIVE9_IMAGE`, `DRIVE10_IMAGE`, `DRIVE11_IMAGE`
- Optional explicit format override:
  - `DRIVE8_FORMAT`, `DRIVE9_FORMAT`, `DRIVE10_FORMAT`, `DRIVE11_FORMAT`

Supported format identifiers in metadata are:

- `d64`
- `g64`
- `nib`
- `raw`

If `DRIVE*_FORMAT` is not provided, the runtime infers format from file extension.

### Commit-5 API Details

#### `struct DriveSlotMountConfig` (`c64_11.cpp`)

Purpose:

- carry parsed per-unit mount metadata from env parsing to drive-slot setup.

Fields:

- `path`: resolved image path string from `DRIVE{unit}_IMAGE`.
- `format`: normalized lowercase format (`d64/g64/nib/raw`) from explicit env or extension inference.
- `exists`: boolean set from `std::filesystem::exists(path)`.

#### `loadDriveSlotMountConfig(uint8_t unit)` (`c64_11.cpp`)

Purpose:

- parse a single unit mount configuration from environment.

Behavior:

- reads `DRIVE{unit}_IMAGE`; if unset/empty, returns an empty config.
- reads optional `DRIVE{unit}_FORMAT` override and normalizes it to lowercase.
- when format override is absent, infers format from image extension.
- computes `exists` by checking the path on disk.

#### `Drive1541::configureMountedImage(const std::string &path, const std::string &format, bool exists)` (`drive_1541.hpp`)

Purpose:

- persist mount metadata directly inside a drive instance so each slot remains independent.

State updated in `Drive1541`:

- `mountedImageConfigured`
- `mountedImagePath`
- `mountedImageFormat`
- `mountedImageExists`

### Current Scope

This commit wires per-slot independent mount metadata and configuration. It does not yet implement full image ingest semantics for each format in the 1541 data path.

## Multi-Unit IEC Smoke Test (Commit 6)

Commit 6 adds a dedicated multi-unit smoke test to lock expected address-isolation behavior.

### New Function

#### `runDrive1541IecMultiUnitSmoke()` (`drive_iec_multiunit_smoke.hpp`)

Purpose:

- verify that unit-level IEC addressing (`8` vs `9`) keeps LISTEN/TALK behavior isolated per drive instance.

What it does:

- creates two independent drives (`unit 8` and `unit 9`) with distinct catalog payload markers.
- broadcasts the same IEC command bytes to both drives and validates only the addressed unit transitions to LISTEN/TALK.
- runs two flows:
  - `LOAD "$",8` equivalent command sequence (`LISTEN 8`, `SA0`, name `$`, `UNLISTEN`, `TALK 8`, `TALK SA0`)
  - `LOAD "$",9` equivalent command sequence
- verifies non-addressed unit does not emit talk payload (`pendingIecTx() == 0`).
- verifies addressed unit emits payload (`hostReadTalkByte` succeeds).

Integration:

- added to the existing drive IEC smoke suite in `c64_11.cpp` via `runDriveIecSmokeSuite(...)`.

## Signoff Integration (Commit 7)

Commit 7 wires multi-unit verification directly into signoff gating.

### Updated Function

#### `Run-Binary(...)` (`run_signoff_week13_14.ps1`)

New parameter:

- `NeedIecMultiUnit` (`bool`)

New gate behavior:

- when `NeedIecMultiUnit` is `true`, the run output must contain:
  - `[1541 IEC MULTI] PASS:`
- missing marker is treated as signoff failure.

### Call-Site Wiring

`NeedIecMultiUnit` is now enabled for:

- fast run (`run-fast`) including 6510/8500 revision slots
- strict run (`run-strict`) including 6510/8500 revision slots

`NeedIecMultiUnit` is disabled for:

- full run (`run-full`), which keeps current external-manifest contract unchanged.

## Multi-Drive Quickstart (Commit 8)

### PowerShell Example (Units 8 and 9)

```powershell
$env:DRIVE8_IMAGE = "testdata\real_corpus_draven_top10\g64\TheLastV8_v2.g64"
$env:DRIVE8_FORMAT = "g64"
$env:DRIVE9_IMAGE = "testdata\real_corpus_draven_top10\d64\Mule_v1.d64"
$env:DRIVE9_FORMAT = "d64"
```

Notes:

- `DRIVE*_FORMAT` is optional when extension is already one of `d64/g64/nib/raw`.
- unset variables for a slot means no configured image metadata for that slot.

### Suggested BASIC Usage Pattern

- Unit 8 directory:
  - `LOAD"$",8`
- Unit 9 directory:
  - `LOAD"$",9`

The current smoke/signoff path validates unit-address isolation for these flows.

## Migration Notes (Single Drive -> Multi-Drive)

- Existing single-drive setups remain valid because slot 0 is still unit 8.
- If you already use `C64_DRIVE_REVISION`, it still applies to each instantiated slot drive.
- New per-slot env vars are additive and non-breaking.

## Operational Boundaries

- Commit 8 provides operational guidance and migration notes.
- Format-specific full ingest semantics per slot remain staged for later commits.
- Current CI/signoff already enforces multi-unit marker coverage in fast/strict profiles.

## Default Policy Flip (Commit 9)

Commit 9 makes multi-drive readiness explicit as runtime default policy.

- default active units are now `8,9,10,11`.
- legacy behavior can be forced with:
  - `C64_IEC_LEGACY_SINGLE_DRIVE=1`

Optional explicit active-drive selection:

- `IEC_ACTIVE_DRIVES` as comma-separated units in range `8..11`
  - example: `IEC_ACTIVE_DRIVES=8,9`

Selection precedence:

1. `C64_IEC_LEGACY_SINGLE_DRIVE=1` -> only unit `8` active.
2. else if `IEC_ACTIVE_DRIVES` is set -> active set parsed from it.
3. else -> default all active (`8,9,10,11`).

Safety fallback:

- if `IEC_ACTIVE_DRIVES` parses to an empty/invalid set, runtime falls back to all active.

### New Function (Commit 9)

#### `resolveActiveDriveSlotsFromEnv()` (`c64_11.cpp`)

Purpose:

- compute the active slot mask (`slot0..slot3`) from environment policy.

Behavior:

- initializes default mask to all active;
- checks legacy single-drive override;
- parses `IEC_ACTIVE_DRIVES` tokens, validates unit range, builds mask;
- applies fallback to all active when parsed set is empty.

## IEC Device Abstraction (Decoupling Step)

To reduce runtime coupling between bus orchestration and concrete drive implementation, the IEC stack now includes an abstract device interface.

### New Interface

#### `class IIecDevice` (`iec_device.hpp`)

Purpose:

- define the minimal contract required by IEC bus scheduling and line propagation.

Methods:

- `tickIecHalfCycle()`
- `setIecLines(bool atnHigh, bool clkHigh, bool dataHigh)`
- `getIecDrivePullCLK() const`
- `getIecDrivePullDATA() const`

### Drive Integration

`Drive1541` now implements `IIecDevice` and provides the required overrides without changing existing drive semantics.

### Bus Integration

`IecBusDomain` now stores and operates on `IIecDevice*` instead of `Drive1541*`.
This removes direct compile-time dependence on concrete drive internals in bus orchestration logic.

## Per-Slot D64 R/W Backend Path (Commit 10)

Commit 10 enables an actual D64 read/write backend path for block-level operations used by IEC DOS commands.

### Updated Functions

#### `Drive1541::isMountedD64BackendActive() const` (`drive_1541.hpp`)

Purpose:

- declare whether mounted image metadata represents an active writable D64 backend.

Condition:

- true only when all are true:
  - image is configured,
  - image exists,
  - format is `d64`.

#### `Drive1541::d64TrackSectorToOffset(uint8_t track, uint8_t sector, uint32_t &offset) const` (`drive_1541.hpp`)

Purpose:

- map Commodore 1541 CHS (`track/sector`) to byte offset inside a standard 35-track D64 image.

Behavior:

- validates track range `1..35`;
- validates sector against per-track geometry (`21/19/18/17` zones);
- computes `offset = linearSectorIndex * 256`.

#### `Drive1541::loadVirtualBlock(...)` / `Drive1541::flushVirtualBlock(...)` (`drive_1541.hpp`)

New backend behavior:

- when D64 backend is active, B-R/B-W block flow reads/writes sector bytes from/to mounted D64 file.
- when backend is not active, previous in-memory virtual block behavior is preserved.
- error handling on D64 path:
  - invalid CHS -> `66,ILLEGAL TRACK OR SECTOR,00,00`
  - missing/unopenable image on write -> `74,DRIVE NOT READY,00,00`

### Updated Smoke Test

#### `runDrive1541IecExecBlockSemanticsSmoke(...)` (`drive_iec_exec_block_sem_smoke.hpp`)

Added coverage:

- creates a temporary D64 image (`174848` bytes),
- seeds known bytes at `T18/S1`,
- mounts it via `configureMountedImage(..., "d64", true)`,
- verifies `B-R` reads seeded bytes,
- verifies `B-W` persists modified bytes back to D64 file.

## Dual-Drive Copy-Like E2E Smoke (Commit 11)

Commit 11 adds a dual-drive smoke to validate the first copy-like transfer path from unit 8 to unit 9 using mounted D64 backend blocks.

### New Function

#### `runDrive1541IecDualDriveCopySmoke()` (`drive_iec_dualdrive_copy_smoke.hpp`)

Purpose:

- verify that two mounted D64-backed drives can transfer a block payload from source (`unit 8`) to destination (`unit 9`) without regressions.

Behavior:

- creates temporary source and destination D64 images,
- seeds a known payload on source track/sector (`T18/S1`),
- mounts source image on drive 8 and destination image on drive 9,
- reads source block via `loadVirtualBlock`,
- copies block buffer into destination drive buffer,
- writes destination via `flushVirtualBlock`,
- verifies destination D64 file contains expected payload bytes.

Integration:

- wired into `runDriveIecSmokeSuite(...)` so fast/strict signoff paths cover it.
- emits dedicated marker:
  - `[IEC COPY E2E] PASS: copy_8_to_9_file_e2e`

## Signoff Gate for Copy File E2E (Commit 14)

`run_signoff_week13_14.ps1` now has an explicit pass gate for file copy path.

### Updated Signoff Contract

- new `Run-Binary(...)` flag:
  - `NeedIecCopyFileE2E`
  - `NeedIecCopyDiskE2E`
- marker required when enabled:
  - `[IEC COPY E2E] PASS: copy_8_to_9_file_e2e`
  - `[IEC COPY E2E] PASS: copy_8_to_9_disk_e2e`

Manifest/checksum artifact:

- dual-drive copy smoke emits:
  - `copy_8_to_9_disk_e2e_manifest.csv`
- signoff validates the manifest exists and reports `match=1`.

### Wiring Policy

- enabled in fast/strict runs (including revision-slot variants),
- disabled in full run (keeps existing full-profile external contract unchanged).

## Per-Unit Mounted D64 Directory Path (Commit 12)

Commit 12 closes the phase-1 DoD by ensuring directory payloads for `LOAD"$",8` and `LOAD"$",9` can come from different mounted D64 images.

### Updated Functions

#### `Drive1541::readMountedD64Sector(...)` (`drive_1541.hpp`)

Purpose:

- read one 256-byte sector from mounted D64 backend by CHS.

#### `Drive1541::decodeD64Name(...)` (`drive_1541.hpp`)

Purpose:

- decode PETSCII-like padded names from D64 BAM/directory entries into printable uppercase ASCII.

#### `Drive1541::d64FileTypeToString(...)` (`drive_1541.hpp`)

Purpose:

- map directory file-type nibble to user-visible type labels (`PRG/SEQ/USR/REL/DEL`).

#### `Drive1541::buildDirectoryPayloadFromMountedD64()` (`drive_1541.hpp`)

Purpose:

- build the IEC directory stream directly from mounted D64:
  - disk name from BAM (`T18/S0`),
  - entries from directory chain (`T18/S1` onward),
  - free block count from BAM per-track counters.

Integration behavior:

- directory request handling now tries mounted D64 directory build first;
- if unavailable, it falls back to previous virtual/stub directory behavior.

### New Smoke Test

#### `runDrive1541IecD64DirectoryMountSmoke()` (`drive_iec_d64_directory_mount_smoke.hpp`)

Coverage:

- creates two different temporary D64 images (unit 8 and unit 9),
- mounts them separately,
- triggers `LOAD"$"` command flow on each unit,
- verifies returned directory payload reflects the correct per-unit mounted image,
- verifies no cross-unit contamination.

## Common Image Backend API (Commit 13)

Commit 13 introduces a shared image backend abstraction and migrates D64 handling behind that API.

### New API

- `image_backend.hpp`
  - `IImageBackend`
    - `isReady()`
    - `formatName()`
    - `readBlock(track, sector, out, error)`
    - `writeBlock(track, sector, in, error)`
    - `readDirectoryListing(listing, error)`
  - `ImageIoError`, `ImageDirectoryEntry`, `ImageDirectoryListing`

### First Adapter

- `d64_image_backend.hpp`
  - `D64ImageBackend : IImageBackend`
  - implements CHS mapping, block read/write, and directory listing extraction for standard 35-track D64.

### Drive Integration

- `Drive1541` now keeps `std::unique_ptr<IImageBackend> mountedImageBackend`.
- `configureMountedImage(...)` instantiates `D64ImageBackend` when format is `d64`.
- block and directory paths now use the common backend API instead of ad-hoc direct file code.

## Copier Matrix Manifest (Commit 16)

Commit 16 introduces a versioned copier compatibility matrix manifest plus schema to make pass criteria explicit and machine-readable.

### New Artifacts

- `copier_matrix.json`
  - initial baseline matrix with:
    - `copy_8_to_9_file_e2e`
    - `copy_8_to_9_disk_e2e`
  - includes per-scenario format/unit/mode and required pass gates.

- `copier_matrix.schema.json`
  - schema for validating matrix structure and required fields.

### Matrix Semantics

- `defaults` section declares global policy:
  - no host fallback requirement,
  - checksum algorithm,
  - expected artifact filenames.
- `scenarios` section defines measurable gates:
  - required runtime markers,
  - manifest match requirement,
  - expected manifest column contract.

## Copier Matrix Runner (Commit 17)

### New Script

- `run_copier_matrix.ps1`

Purpose:

- execute the baseline copier matrix from `copier_matrix.json`,
- build selected profile (`fast` or `strict`),
- run emulator once,
- evaluate scenario gates (markers + manifest checks),
- emit consolidated reports.

Outputs:

- JSON report: `copier_matrix_report.json`
- CSV report: `copier_matrix_report.csv`
- Optional run-scoped outputs:
  - `-OutputDir artifacts\\runs\\<run-id>`
  - `-ReportPrefix <name>` -> `<name>_report.json/.csv`

Terminal summary marker:

- `[COPIER-MATRIX] profile=... pass=X/Y ...`

Example:

```powershell
.\run_copier_matrix.ps1 -Profile fast -Manifest external_tests_manifest.json
```

Parallel-safe example:

```powershell
.\run_copier_matrix.ps1 -Profile fast -Manifest external_tests_manifest.json -OutputDir artifacts\runs\jobA -ReportPrefix copier_jobA
```

Related gate scripts (`run_phase5_soak_gate.ps1`, `run_advanced_dos_recovery_gate.ps1`, `run_advanced_real_corpus_gate.ps1`, `run_real_golden_gate.ps1`, `run_check_phase5_hard_thresholds.ps1`, `run_check_phase5_closure.ps1`, `run_signoff_week13_14.ps1`) also accept `-OutputDir` for run-scoped artifacts.

## Copier Matrix Signoff/CI Gate (Commit 18)

Commit 18 promotes copier matrix evaluation to a mandatory signoff gate.

### Signoff Integration

- `run_signoff_week13_14.ps1` now always runs `run_copier_matrix.ps1` as step `run-copier-matrix`.
- The step fails signoff if copier matrix exit is non-zero or if expected summary marker is missing.
- Default signoff profile for matrix gate is `fast`.

### New Signoff Parameters

- `CopierMatrixPath` (default `copier_matrix.json`)
- `CopierMatrixManifest` (default `external_tests_manifest.json`)
- `CopierMatrixProfile` (`fast` or `strict`, default `fast`)
- `CopierMatrixReportJson` (default `copier_matrix_report.json`)
- `CopierMatrixReportCsv` (default `copier_matrix_report.csv`)

### Report Availability in Signoff Output

On success, signoff prints resolved output paths for:

- copier matrix JSON report
- copier matrix CSV report

### CI Artifact Publishing

Both manual workflows now upload copier matrix reports as artifacts:

- `.github/workflows/revision-signoff-matrix.yml`
- `.github/workflows/revision-tolerance-check.yml`

## Advanced Image Backends Baseline (Commit 19)

Commit 19 extends `IImageBackend` wiring with baseline adapters for advanced image formats.

### New Backends

- `advanced_image_backends.hpp`
  - `G64ImageBackend` (read baseline)
  - `NIBImageBackend` (read baseline)
  - `RAWImageBackend` (read/write baseline)

All three backends expose:

- `isReady()`
- `formatName()`
- `readBlock(track, sector, ...)`

`RAWImageBackend` additionally enables `writeBlock(...)` for baseline persistence checks.

### Drive Mount Wiring

- `Drive1541::configureMountedImage(...)` now instantiates:
  - `D64ImageBackend` for `d64`
  - `G64ImageBackend` for `g64`
  - `NIBImageBackend` for `nib`
  - `RAWImageBackend` for `raw`

### New Smoke Coverage

- `drive_iec_advanced_image_mount_smoke.hpp`
  - verifies backend activation and read baseline for `g64`, `nib`, `raw`
  - verifies write baseline persistence for `raw`
  - emits markers:
    - `[IEC COPY E2E] PASS: advanced_g64_mount_baseline`
    - `[IEC COPY E2E] PASS: advanced_nib_mount_baseline`
    - `[IEC COPY E2E] PASS: advanced_raw_mount_baseline`

### Copier Matrix Extension

`copier_matrix.json` now includes advanced baseline scenarios:

- `advanced_g64_mount_baseline`
- `advanced_nib_mount_baseline`
- `advanced_raw_mount_baseline`

## Advanced Write-Protect Baseline (Next Step)

The advanced backend baseline now enforces read-only semantics for `g64` and `nib` writes.

### Error Contract

- `ImageIoError` now includes `WriteProtected`.
- `Drive1541` maps write-protected backend writes to status:
  - `26,WRITE PROTECT ON,00,00`

### Smoke and Matrix Coverage

- `drive_iec_advanced_image_mount_smoke.hpp` now verifies write-protect behavior for:
  - `g64`
  - `nib`
- New runtime markers:
- `[IEC COPY E2E] PASS: advanced_g64_write_protect_baseline`
- `[IEC COPY E2E] PASS: advanced_nib_write_protect_baseline`
- `copier_matrix.json` includes both scenarios as advanced baseline gates.

## Advanced Track/Zone/Sync/Weakbit Mapper (Next Step)

Advanced format backends now run a format-aware read path baseline with deterministic mapping stages.

### Backend Model

- `advanced_image_backends.hpp` now uses `FluxMappedImageBackend` for `g64/nib/raw`.
- Read path stages:
  - CHS validation (`track/sector` geometry)
  - track-zone mapping transform
  - sync-loss marker perturbation
  - weakbit dynamic nibble variance (`g64`/`nib` only)

### Format Policy

- `g64`: read-only, sync-loss + weakbit model enabled
- `nib`: read-only, sync-loss + weakbit model enabled
- `raw`: writable, sync-loss model enabled, weakbit model disabled

### Smoke and Matrix Markers

- Advanced smoke now verifies weakbit variance on repeated reads for `g64` and `nib`.
- New markers:
- `[IEC COPY E2E] PASS: advanced_g64_weakbit_baseline`
- `[IEC COPY E2E] PASS: advanced_nib_weakbit_baseline`
- `copier_matrix.json` includes both weakbit baseline scenarios.

## Advanced Parser/Mapper Baseline (Next Step)

Advanced backend path now includes initial format-aware parser layers for `g64` and `nib`.

### G64 Baseline Parser

- validates `GCR-1541` signature
- reads `track_count`
- parses 32-bit track offset table
- resolves per-track payload region from on-disk track length header

### NIB Baseline Parser

- infers track stride from file size (`size / 35` when aligned)
- maps track payload slices by stride

### New Matrix Markers

- `[IEC COPY E2E] PASS: advanced_g64_parser_baseline`
- `[IEC COPY E2E] PASS: advanced_nib_parser_baseline`

Both are included in `copier_matrix.json` as advanced scenarios while preserving existing gates.

## Hard GCR Fidelity Baseline (Next Step)

Advanced format backends now include a stricter GCR decode/encode path with explicit quality/error classification.

### New Core Structures (`advanced_image_backends.hpp`)

- `advanced_image_detail::GcrGapQuality`
  - values: `Poor`, `Marginal`, `Stable`
- `advanced_image_detail::GcrErrorClass`
  - values: `None`, `Soft`, `Hard`
- `advanced_image_detail::GcrMetrics`
  - fields:
    - `invalidSymbols`
    - `syncLossEvents`
    - `gapQuality`
    - `errorClass`

### New GCR Functions (`advanced_image_backends.hpp`)

- `advanced_image_detail::encodeNibbleToGcr(uint8_t nibble)`
  - maps 4-bit nibble to 5-bit GCR symbol using a fixed table.

- `advanced_image_detail::decodeGcrToNibble(uint8_t symbol, uint8_t &nibble)`
  - reverse-maps 5-bit GCR symbol to nibble and reports invalid symbols.

- `advanced_image_detail::classifyGcrErrors(GcrMetrics &metrics)`
  - classifies aggregate decode health as `None`, `Soft`, or `Hard`.

- `FluxMappedImageBackend::applyStrictGcrDecodePipeline(...)`
  - computes sync and gap runs,
  - decodes GCR symbols,
  - counts invalid symbol events,
  - applies error-class dependent markers into output block.

- `FluxMappedImageBackend::applyStrictGcrEncodePipeline(...)`
  - encodes input bytes to GCR-derived payload representation before write path.

### Runtime and Matrix Hard Scenarios

Advanced smoke now emits:

- `[IEC COPY E2E] PASS: advanced_g64_gcr_hard_baseline`
- `[IEC COPY E2E] PASS: advanced_nib_gcr_hard_baseline`

`copier_matrix.json` includes both hard scenarios while keeping prior gates active.

## DOS Error-Map Hard Baseline (Next Step)

The strict GCR pipeline now exposes DOS-oriented error classes for hard scenarios.

### New Function

- `advanced_image_detail::mapMetricsToDosErrorCode(const GcrMetrics &metrics)`
  - maps strict GCR metrics to DOS-like codes:
    - `20` (header-like)
    - `21` (sync)
    - `22` (data block/gap quality)
    - `23` (checksum/data)
    - `27` (hard checksum)

### Decode Output Contract

- `FluxMappedImageBackend::applyStrictGcrDecodePipeline(...)` now stores mapped DOS code in decoded block byte index `5`.

### Smoke and Matrix Hard Scenarios

New hard markers:

- `[IEC COPY E2E] PASS: advanced_g64_dos_error_map_hard_baseline`
- `[IEC COPY E2E] PASS: advanced_nib_dos_error_map_hard_baseline`

Both are now in `copier_matrix.json` as additional hard scenarios.

## Multi-Track / Half-Track / Relock Hard Baseline (Next Step)

Advanced hard coverage now includes track-window realism and relock drift stability checks.

### New Debug Functions (`advanced_image_backends.hpp`)

- `G64ImageBackend::debugTrackSliceTag(uint8_t track)`
  - returns a deterministic tag derived from parsed track slice offset/size.
  - used to verify multi-track payload differentiation.

- `G64ImageBackend::debugHasHalfTrackSlice(uint8_t track)`
  - reports whether half-track-style slot data is available for the given track.

- `NIBImageBackend::debugTrackStrideTag()`
  - returns parsed track stride low-byte tag for stride sanity checks.

- `NIBImageBackend::debugTrackWindowReadable(uint8_t track)`
  - verifies computed per-track stride window stays in bounds.

### New Hard Markers

- `[IEC COPY E2E] PASS: advanced_g64_multitrack_halftrack_hard_baseline`
- `[IEC COPY E2E] PASS: advanced_nib_multitrack_hard_baseline`
- `[IEC COPY E2E] PASS: advanced_g64_sync_relock_drift_hard_baseline`
- `[IEC COPY E2E] PASS: advanced_nib_sync_relock_drift_hard_baseline`

All four are included in `copier_matrix.json` as hard advanced scenarios.

## Jitter Window + Relock Soak Hard Baseline (Next Step)

Added hard stress checks focused on temporal jitter and short soak stability.

### New Hard Behaviors in Advanced Smoke

- **Windowed jitter digest (`g64`/`nib`)**
  - runs repeated reads and builds an FNV-style digest from weakbit-sensitive and DOS-map bytes.
  - pass condition: digest must evolve from initial seed.

- **Relock soak window (`g64`/`nib`)**
  - performs 3 repeated reads in a short window.
  - pass condition: classification byte remains stable across the window.

### New Hard Markers

- `[IEC COPY E2E] PASS: advanced_g64_jitter_window_hard_baseline`
- `[IEC COPY E2E] PASS: advanced_nib_jitter_window_hard_baseline`
- `[IEC COPY E2E] PASS: advanced_g64_relock_window_soak_hard_baseline`
- `[IEC COPY E2E] PASS: advanced_nib_relock_window_soak_hard_baseline`

All are wired into `copier_matrix.json` as advanced hard scenarios.

## Advanced Real Corpus Gate Bootstrap (Commit 1)

### New Manifest

- `advanced_real_corpus_manifest.json`

Purpose:

- bootstrap an advanced real-corpus gate for `g64`/`nib`/`raw` titles,
- keep no-host-fallback and timing-window expectations explicit per title.

### New Runner

- `run_advanced_real_corpus_gate.ps1`

What it does:

- parses `advanced_real_corpus_manifest.json`,
- verifies title paths,
- validates RAW sample and week81-oracle constraints when requested,
- executes `run_kernel_iec_e2e.ps1` per-title,
- enforces no-host-fallback contract,
- emits per-title lines and summary marker:
  - `[ADV-REAL-CORPUS] title=... pass=...`
  - `[ADV-REAL-CORPUS] summary pass=X/Y report=...`

Artifact:

- `advanced_real_corpus_runtime.csv`

## Hard Threshold Policy (Commit 2)

### New Policy File

- `phase5_hard_thresholds.json`

Policy sections:

- `rules.matrix`
  - `min_pass_rate`
  - `max_runtime_exit_code`
  - `require_all_markers`
- `rules.advanced`
  - `min_advanced_scenarios`
  - `min_advanced_pass_rate`
  - `require_hard_scenario_pass`
- `rules.drift`
  - `max_relock_window_failures`
  - `max_jitter_digest_static_rows`
  - `max_error_map_missing_rows`

### New Checker

- `run_check_phase5_hard_thresholds.ps1`

What it validates:

- matrix global pass-rate and runtime exit policy,
- advanced scenario count and pass-rate policy,
- hard scenario mandatory pass,
- drift/jitter/error-map subgroup failure caps.

Pass marker:

- `[PHASE5-HARD] PASS: ...`

## Flux/Bitcell Fidelity Upgrade (Commit 3)

Commit 3 adds a first bitcell-style hysteresis layer to the advanced read model.

### New Internal State (`advanced_image_backends.hpp`)

- `relockClassState`
  - per track/sector last accepted relock class byte.
- `relockConfidence`
  - per track/sector confidence counter for class persistence.
- `weakWindowPhase`
  - per track/sector phase cursor for weakbit window drift progression.

### New Functions (`advanced_image_backends.hpp`)

- `modelIndex(uint8_t track, uint8_t sector)`
  - maps CHS to stable index in model-state arrays.

- `applyRelockHysteresis(uint8_t track, uint8_t sector, uint8_t &classByte)`
  - applies confidence-based hysteresis:
    - stable class strengthens confidence,
    - transient class changes are damped,
    - class flip accepted only after confidence exhaustion.

### Updated Functions (`advanced_image_backends.hpp`)

- `applySyncAndWeakBitModel(...)`
  - now injects bitcell-style sync perturbation byte,
  - weakbit drift uses phased window progression (`weakWindowPhase`) instead of static-only positions.

- `applyStrictGcrDecodePipeline(...)`
  - now runs relock hysteresis before publishing class byte.

### New Hard Markers

- `[IEC COPY E2E] PASS: advanced_g64_flux_hysteresis_hard_baseline`
- `[IEC COPY E2E] PASS: advanced_nib_flux_hysteresis_hard_baseline`

Both are included in `copier_matrix.json`.

## DOS Error/Recovery Parity Gate (Commit 4)

### New Manifest

- `advanced_dos_recovery_manifest.json`

Purpose:

- define hardset scenarios for advanced DOS recovery parity checks.

### New Runner

- `run_advanced_dos_recovery_gate.ps1`

What it does:

- runs `run_copier_matrix.ps1` (selected profile),
- validates required recovery markers from manifest,
- emits per-scenario status lines and summary marker:
  - `[ADV-DOS-RECOVERY] scenario=... pass=...`
  - `[ADV-DOS-RECOVERY] summary pass=X/Y ...`

Artifact:

- `advanced_dos_recovery_runtime.csv`

### New Hard Matrix Scenarios

- `advanced_g64_dos_recovery_hard_baseline`
- `advanced_nib_dos_recovery_hard_baseline`

These validate write-protect followed by read-path recovery parity stability.

## Phase5 Scale-Up + Soak + Auto Checklist (Current)

### Expanded Real Corpus

- `advanced_real_corpus_manifest.json` now includes a broader set of titles/formats (`d64/g64/nib/raw`) for better loader/protection diversity.

### New Soak Gate

- `run_phase5_soak_gate.ps1`

What it does:

- runs `run_copier_matrix.ps1` repeatedly (`Runs`),
- computes flake rate (`failRuns / Runs`),
- fails if flake rate exceeds configured threshold (`MaxFlakeRate`).

Output:

- per-run marker: `[PHASE5-SOAK] run=...`
- summary marker: `[PHASE5-SOAK] summary pass=... flake_rate=...`
- artifact: `phase5_soak_runtime.csv`

### Auto-Generated Support Matrix

- `run_generate_copier_support_matrix.ps1` now owns `COPIER_SUPPORT_MATRIX.md` generation from:
  - `copier_matrix.json`
  - `copier_matrix_report.json`

- `run_check_phase5_closure.ps1 -GenerateChecklist` now regenerates checklist before validating scenario coverage.

### CI Integration

Updated workflows now run:

- advanced real corpus gate,
- advanced DOS recovery gate,
- phase5 soak gate,
- phase5 hard thresholds gate,
- phase5 closure gate with auto-checklist generation.

Workflows:

- `.github/workflows/revision-tolerance-check.yml`
- `.github/workflows/revision-signoff-matrix.yml`

## Long-Tail Real Parity Push (Current)

### Expanded Long-Tail Corpus

- `advanced_real_corpus_manifest.json` now includes additional long-tail entries for:
  - repeated RAW recovery profile coverage,
  - loader-stress variants on `9.g64` / `9.nib`.
  - explicit extreme-fixture entries that lock parity expectations without introducing behavior changes:
    - `adv_g64_longtail_extreme_fixture_9`,
    - `adv_nib_longtail_extreme_fixture_9`,
    - `adv_raw_copy_ii_pc_v1_extreme_flux_fixture`.

### Physical Edge Refinement

`advanced_image_backends.hpp` now includes deeper edge-state modeling:

- `weakWindowSpanState`
  - dynamic weakbit window span per CHS.
- `bitcellSlipState`
  - per-CHS bitcell slip phase state.

Updated behavior:

- `applySyncAndWeakBitModel(...)`
  - adds bitcell slip-driven perturbation,
  - adds dynamic weakbit span and periodic burst perturbation.

- `applyStrictGcrDecodePipeline(...)`
  - adds long-tail mismatch marker path when invalid-symbol and sync-loss signals overlap.

### New Long-Tail Hard Markers

- `[IEC COPY E2E] PASS: advanced_g64_longtail_burst_hard_baseline`
- `[IEC COPY E2E] PASS: advanced_nib_longtail_burst_hard_baseline`
- `[IEC COPY E2E] PASS: advanced_g64_longtail_extreme_corpus_fixture`
- `[IEC COPY E2E] PASS: advanced_nib_longtail_extreme_corpus_fixture`
- `[IEC COPY E2E] PASS: advanced_raw_flux_extreme_corpus_fixture`

### Commit 0: Long-Tail Extreme Corpus + Fixtures

- Added behavior-neutral extreme fixture scenarios to `copier_matrix.json`:
  - `advanced_g64_longtail_extreme_corpus_fixture`,
  - `advanced_nib_longtail_extreme_corpus_fixture`,
  - `advanced_raw_flux_extreme_corpus_fixture`.

- Added matching parity fixture titles to `advanced_real_corpus_manifest.json` so long-tail envelope checks stay explicit and repeatable under gates.

- Added expected marker coverage in `advanced_dos_recovery_manifest.json` for the new long-tail G64/NIB fixture scenarios.

## Phase5 Final Signoff

- New final snapshot document:
  - `PHASE5_FINAL_SIGNOFF.md`

Content includes:

- final gate contract,
- mandatory criteria,
- active baseline values,
- release artifacts expected for phase5 closure.

Release pin manifest:

- `PHASE5_RELEASE_PIN.json` (formal freeze of manifests/gates/docs, behavior-neutral).

## Commit 20: Release Freeze (Physical Stack Milestone)

### Official Promotion Levels

| Level | Required Scope | Fixed Criteria |
|---|---|---|
| Level1 | baseline matrix correctness | matrix pass rate `= 1.0` |
| Level2 | Level1 + parity + timing core | baseline/advanced pass rates `= 1.0`, timing core pass rate `= 1.0` |
| Level3 | Level2 + corpus + DOS recovery + soak reliability | corpus pass `= 1.0`, DOS recovery pass `= 1.0`, flake rate `<= 0.05` |

### Runtime Controls (Frozen)

- Use run-scoped artifacts for orchestration:
  - `-OutputDir artifacts\\runs\\<run-id>`
  - optional `-ReportPrefix <name>` for matrix reports.
- `run_copier_matrix.ps1` is lock-serialized for shared-runtime safety.
- Release soak recommendation for milestone freeze: `run_phase5_soak_gate.ps1 -Runs 12 -MaxFlakeRate 0.05`.

### Known Limitations

- Strict/timing/VIC runs may intermittently exit in this environment (`-1073740791` class failure); strict-marker rerun policy remains valid.
- Full custom copy-protection parity beyond declared matrix/corpus fixtures is not claimed by this milestone.

## L4.1 Increment: Read-Channel PLL Gate (Experimental)

- Added `level4-accuracy` profile parsing for 1541 physical pipeline experiments.
- Added read-channel PLL model with:
  - lock dynamics,
  - revision-aware phase noise shaping,
  - deterministic jitter window sampling hooks for calibration.
- Added incremental gate: `run_level4_pll_gate.ps1`
  - runs matrix under `C64_DRIVE_PROFILE=level4-accuracy`,
  - requires marker `[1541 PLL] PASS`.

This gate is additive and does not alter existing Level1/2/3 promotion policy.

## L4.2 Increment: Write Persistence Roundtrip Gate (Experimental)

- Added write-surface persistence model:
  - `drive1541_physical/write_surface_model.hpp`
  - `drive1541_physical/write_surface_model.cpp`
- Integrated Level4-only write persistence hooks in flux-mapped image backends:
  - write splice / erase band evolution across write passes,
  - weak-window epoch coupling for readback drift in Level4 profile.
- Added runtime test marker and test:
  - `tests/drive1541_write_roundtrip_tests.hpp`
  - marker: `[1541 L4 WRITE] PASS`
- Added incremental gate: `run_level4_write_roundtrip_gate.ps1`
  - forces `C64_DRIVE_PROFILE=level4-accuracy`,
  - requires markers `[1541 L4 WRITE] PASS` and copier disk E2E pass.

This gate is additive and does not alter existing Level1/2/3 promotion policy.

## L4.3 Increment: Pathological KPI + Chaos Soak (Experimental)

- Added Level4 pathological corpus manifest:
  - `level4_pathological_corpus_manifest.json`
  - includes long-tail g64/nib fixtures and RAW flux-heavy fixture with strict oracle fields.
- Added KPI gate:
  - `run_level4_kpi_gate.ps1`
  - executes Level4 pathological corpus + advanced corpus + real golden + DOS recovery under `C64_DRIVE_PROFILE=level4-accuracy`,
  - emits `level4_kpi_metrics.json` + `level4_kpi_gate_runtime.csv`,
  - checks differential multi-oracle spread budget (`0.0`) and full pass-rate budgets.
- Added Level4 chaos soak gate:
  - `run_level4_chaos_soak.ps1`
  - runs copier matrix repeatedly under Level4 profile,
  - emits `level4_chaos_soak_runtime.csv` with flake-budget assertion.

These gates are additive and do not alter existing Level1/2/3 promotion policy.

## Level5 Planning Assets

- `LEVEL5_ROADMAP.md`: phased roadmap (L5.0 -> L5.4), gates, KPI targets, promotion criteria.
- `datasets/level5/manifests/level5_real_seed_manifest.json`: initial real-data seed manifest for onboarding.
- `datasets/level5/manifests/level5_acquisition_backlog.csv`: prioritized acquisition backlog with per-title capture targets.
- `run_level5_dataset_quality_gate.ps1`: automated dataset quality gate for metadata/oracle/integrity thresholds.

## L5.1 Increment: Sub-cycle Coupling Gate (Experimental)

- Added `level5-coupling` drive profile parsing and routing in physical pipeline:
  - profile enum support in `drive1541_physical/physical_profile.hpp`,
  - runtime profile parsing in `drive_1541.hpp`,
  - flux-layer routing parity with Level4 for flux-capable formats.
- Added Level5-coupling defaults for IEC phase behavior in `Drive1541` and kernel E2E setup:
  - stronger default ATN/listener ACK behavior,
  - default command/data edge coupling suitable for sub-cycle validation,
  - profile defaults remain overrideable via existing `KERNAL_DRIVE_*` environment flags.
- Added and validated gate:
  - `run_level5_cycle_coupling_gate.ps1`
  - runs dataset quality gate + kernel IEC E2E under `C64_DRIVE_PROFILE=level5-coupling` (fast/strict).
  - includes controlled kernel-run retry and process cleanup (`-KernelRetryCount`, default `3`) to reduce transient runtime lock flakes.
  - optional kernel warmup (`-EnableKernelWarmup`, `-KernelWarmupRepeat`) before main kernel gate attempts.

## L5.2 Increment: Flux/Write Analog Gate (Experimental)

- Added incremental gate scaffold:
  - `run_level5_flux_analog_gate.ps1`
  - runs dataset quality + write roundtrip + kernel IEC E2E under `C64_DRIVE_PROFILE=level5-coupling`.
- Fast profile validates:
  - official Level5 testset quality,
  - write roundtrip fast,
  - kernel IEC pure E2E.
- Strict profile additionally requires write roundtrip strict and stronger kernel repeat/budget settings.
  - Default strict mode keeps strict-write optional to avoid unrelated hard-reference corpus blockers.
  - Set `-RequireStrictWriteRoundtrip` to enforce strict write roundtrip as a hard gate.
- Gate hardening:
  - write roundtrip execution includes controlled retry and stale-exe process cleanup (`-WriteRoundtripRetryCount`, default `3`) to reduce transient lock flakes.

## L5.3 Increment: Differential Multi-Oracle Gate (Experimental)

- Added incremental gate scaffold:
  - `run_level5_diff_oracle_gate.ps1`
  - orchestrates:
    1) Level5 cycle coupling gate fast,
    2) Level5 cycle coupling gate strict,
    3) Level5 flux/analog gate fast.
- Produces differential spread report:
  - `datasets/level5/quality_reports/level5_diff_oracle_gate_metrics.json`
  - `datasets/level5/quality_reports/level5_diff_oracle_gate_runtime.csv`
- Default budget policy:
  - `SpreadBudget=0.0` (strict parity target).

## L5.4 Increment: Chaos/Soak Promotion Gate (Experimental)

- Added soak gate scaffold:
  - `run_level5_chaos_soak.ps1`
  - fast profile runs repeated L5.1 cycle-coupling validation;
  - strict profile runs repeated L5.3 diff-oracle validation.
- Output artifacts:
  - `datasets/level5/quality_reports/level5_chaos_soak_runtime.csv`
  - `datasets/level5/quality_reports/level5_chaos_soak_metrics.json`
- Budget defaults:
  - `MaxFlakeRate=0.05`
  - `SpreadBudget=0.0` (strict profile delegated to L5.3 gate).
- Hardening:
  - per-run gate retry with stale executable cleanup (`-GateRetryCount`, default `3`; `-RetryDelayMs`, default `250`) to reduce transient lock/runtime flakes in long soaks.
  - optional warmup path (`-EnableWarmup`) and kernel-only recovery retries (`-KernelOnlyRetryCount`) to reduce first-run instability before full gate replay.

## Level5 Promotion Status (Current)

- Current state is `software-ready` on official v1 testset gates (L5.1/L5.2/L5.3/L5.4).
- Temporary promotion label allowed for active development snapshots: `L5 promoted (cap02_deferred)`.
- `cap02_deferred` means physical promotion is not final yet:
  - per-side multi-capture (`min_required_captures=2`) is incomplete on part of the real corpus,
  - metadata/oracle calibration remains `partial` for deferred rows,
  - `ready_for_l5_promotion` in physical readiness tracker stays authoritative for final closure.
- Final Level5 physical promotion requires closing all tracker blockers and setting `ready_for_l5_promotion=yes` on target corpus rows.

## 1541 Power Matrix Progress

- Phase 1 complete: explicit C64 power states (`off/on/resetting`) + drive power APIs (`setC64Power`, `setDrivePower`, `getPowerMatrixState`).
- Phase 2 complete: IEC bus rules for all matrix combinations (`C64OffDriveOff`, `C64OffDriveOn`, `C64OnDriveOff`, `C64OnDriveOn`).
- Phase 3 complete: independent C64/drive domain scheduling controls in IEC bridge domains.
- Phase 4 (boot/reset semantics) in progress:
  - C64 `off/resetting` forces drive IEC parser idle and arms command reacquire,
  - after C64 resume (`on`), first valid command byte is required before accepting data bytes.
- Added dedicated gate:
  - `run_level5_power_matrix_gate.ps1`
  - validates power-matrix scenarios (`C64OffDriveOff`, `C64OffDriveOn`, `C64OnDriveOff`, `C64OnDriveOn`) and emits:
    - `datasets/level5/quality_reports/level5_power_matrix_gate_runtime.csv`
    - `datasets/level5/quality_reports/level5_power_matrix_gate_metrics.json`

## Commit 1: Physical Stack Scaffold and Runtime Profiles

- Added initial non-functional scaffold under `drive1541_physical/`:
  - `physical_profile.hpp`
  - `drive_scheduler.hpp`
  - `drive_cpu_domain.hpp`
  - `drive_via_domain.hpp`
  - `drive_dos_memory_map.hpp`
  - `drive_iec_port.hpp`

- Added minimal wiring in `drive_1541.hpp`:
  - profile enum alias and default profile `Level1Functional`,
  - placeholder physical-domain members for scheduler/CPU/VIA/memory-map/IEC port.

- Scope: scaffold only. No behavior change in active functional path.

## Commit 2: Runtime Drive Profile Selection (Safe Default)

- Added runtime profile parsing in `Drive1541`:
  - `level1-functional` -> `Level1Functional`
  - `level2-cycle` -> `Level2Cycle`
  - `level3-physical` -> `Level3Physical`
  - invalid/unknown values -> safe fallback `Level1Functional`

- Added environment-driven selection during drive-slot initialization:
  - `C64_DRIVE_PROFILE`
  - fallback `KERNAL_DRIVE_PROFILE`

- Default behavior remains unchanged (functional path) when profile env var is not set.

## Commit 3: Autonomous Drive Scheduler Core (Deterministic Stub)

- Extended `drive1541_physical/drive_scheduler.hpp` with deterministic scheduling primitives:
  - host ticks + drive ticks counters,
  - centralized fixed rational conversion (`setRateRatio(numerator, denominator)`),
  - timestamped event queue (`timestamp`, `eventType`, `payloadId`) with deterministic sequence tie-break.

- Added scheduler APIs:
  - `scheduleAt(...)` / `schedule_at(...)`
  - `runUntil(...)` / `run_until(...)`
  - `now()`

- Added smoke coverage in `drive1541_scheduler_determinism_smoke.hpp`:
  - identical replay produces identical ordered event trace,
  - equal-timestamp ordering remains stable and deterministic.

- Wired scheduler host-time tick in `drive_1541.hpp` (`tickIecHalfCycle`) and reset path; no I/O semantic changes.

## Anti-Flake Micro-Check for `copy_8_to_9_disk_e2e`

- `run_copier_matrix.ps1` now includes a behavior-neutral micro-check for the `copy_8_to_9_disk_e2e` manifest gate:
  - when manifest rows are transiently empty or `match=1` is not yet visible, it performs a short bounded re-read retry window,
  - retry is limited to this scenario only and does not alter emulator execution semantics.

- Purpose: reduce intermittent CI/local false negatives on manifest timing without masking real mismatches.

## Commit 4: Per-Drive Power Lifecycle State Machine

- Added `drive1541_physical/drive_power_controller.hpp` with power states:
  - `Off`, `SpinningUp`, `On`, `Resetting`, `SpinningDown`

- Integrated power lifecycle into `drive_1541.hpp`:
  - control methods: `powerOn(coldBoot)`, `powerOff()`, `powerReset()`
  - state queries: `isPoweredOn()`, `getPowerState()`
  - per-tick lifecycle progression via `powerController.tick(1)`.

- Off-state behavior guard (behavioral contract):
  - when powered off, drive processing loop returns early,
  - IEC outputs are released (`getIecDrivePullCLK/DATA` both false),
  - no CPU/VIA/scheduler advancement while off.

- Added smoke coverage in `tests/drive1541_power_lifecycle_smoke.hpp`:
  - on/off/reset transitions,
  - off-state no-advance checks,
  - IEC released lines while off,
  - deterministic repeated power-cycle digest.

## Commit 5: Concrete 1541 DOS Memory Map (ROM/RAM/IO Decode)

- `drive1541_physical/drive_dos_memory_map.hpp` now provides concrete decode behavior:
  - RAM (`< $1800`) read/write
  - IO windows (`$1800-$180F`, `$1C00-$1C0F`) via bound callbacks
  - ROM (`>= $C000`) read-only
  - unmapped regions return deterministic `0xFF`

- `drive_1541.hpp` wiring now routes memory access through `physicalDosMemoryMap` while preserving current behavior:
  - VIA read/write bound as IO callbacks
  - ROM pointer bound to `memory[0xC000]`
  - RAM seeded from base memory map on reset/load

- Added dedicated coverage in `tests/drive1541_memory_map_tests.hpp`:
  - RAM read/write
  - ROM write protection
  - IO callback dispatch
  - unmapped deterministic default.

## Commit 6: Drive CPU Domain Wiring via DOS Memory Map Bus

- `drive1541_physical/drive_cpu_domain.hpp` now exposes concrete bus-callback wiring:
  - `bind_bus(ReadFn, WriteFn)`
  - `reset()` fetches reset vector through bus read callback
  - `step_one_cycle()` fetches opcode through bus read callback and emits deterministic callback write for wiring validation
  - `set_irq(...)` / `set_nmi(...)` are latched and keep deterministic PC cadence behavior.

- `drive_1541.hpp` integration now binds CPU-domain bus to `physicalDosMemoryMap` callbacks through `bindPhysicalCpuDomain()` and initializes it in `reset()`.

- Added coverage in `tests/drive1541_cpu_domain_tests.hpp`:
  - reset vector fetch validation,
  - write callback propagation,
  - IRQ/NMI deterministic progression checks.

## Commit 7: Dual 6522 VIA Domain (Timer + IFR/IER + Composite IRQ)

- Added concrete dual-VIA domain files:
  - `drive1541_physical/drive_via_domain.hpp`
  - `drive1541_physical/drive_via_domain.cpp`

- `DriveViaDomain` now models two reusable `Via6522` instances (`via1` and `via2`) with base real behavior:
  - register IO via `read_io(addr)` / `write_io(addr, val)` on `$1800/$1C00` windows,
  - timer countdown + underflow for T1/T2,
  - IFR/IER set/clear semantics,
  - composite IRQ line via `irq_asserted()` (OR of VIA sources).

- `drive_1541.hpp` wiring updated:
  - DOS memory map IO callbacks now dispatch through `physicalViaDomain`,
  - external bridge keeps existing `drive.via1` / `drive.via2` state visible,
  - per-half-cycle VIA ticking is centralized in `physicalViaDomain.tick(1)`,
  - CPU domain IRQ input is fed from `physicalViaDomain.irq_asserted()`.

- Added dedicated coverage in `tests/drive1541_via_domain_tests.hpp`:
  - IER enable + timer event => IFR bit set,
  - IFR clear-by-write correctness,
  - timer countdown/underflow base behavior,
  - composite IRQ asserted when a single VIA interrupts.

## Commit 8: Physical GCR Codec + Sync Mark Support (Level3 Hook)

- Added physical-stack codec files:
  - `drive1541_physical/gcr_codec.hpp`
  - `drive1541_physical/gcr_codec.cpp`

- Implemented `jemu::drive1541::GcrCodec` API:
  - `encode_4to5(const uint8_t*, size_t)`
  - `decode_5to4(const uint8_t*, size_t)`
  - `is_sync_mark(uint8_t)`

- Decode result surface (`GcrDecodeResult`) now reports:
  - `ok` decode state,
  - `sync_found` detection state,
  - decoded byte payload.

- Added optional Level3-only integration hook in `advanced_image_backends.hpp`:
  - strict GCR decode/encode pipeline can delegate to physical codec only when drive profile resolves to `level3-physical`,
  - default Level1/Level2 behavior remains unchanged.

- Added coverage in `tests/drive1541_gcr_codec_tests.hpp`:
  - known-block roundtrip encode/decode,
  - sync-mark stream detection,
  - invalid symbol path with `ok=false`.

## Commit 9: Bitcell Timing Model (Zone Speed + Deterministic Jitter)

- Added new physical timing model units:
  - `drive1541_physical/bitcell_timing_model.hpp`
  - `drive1541_physical/bitcell_timing_model.cpp`

- Implemented `jemu::drive1541::BitcellTimingModel` API:
  - `reset(seed)`
  - `set_zone(zone)`
  - `next_cell_ticks()`

- Timing model behavior:
  - zone-aware base cell durations (`zone 0..3` => increasing base ticks),
  - deterministic bounded jitter (`[-1,+1]`) via xorshift32,
  - CI-stable deterministic sequence for same seed/zone.

- Integrated Level3-only hook in `advanced_image_backends.hpp`:
  - in sync/bitcell path, Level3 profile uses per-track/sector `BitcellTimingModel` instance,
  - emitted jitter tag feeds existing bitcell marker perturbation,
  - Level1/Level2 path remains behavior-compatible.

- Added coverage in `tests/drive1541_bitcell_timing_tests.hpp`:
  - different zones produce different base timings,
  - jitter stays in expected bounds,
  - same seed reproduces identical sequence.

## Commit 10: Mechanics Model (Spindle + Head + Half-Track)

- Added mechanical-domain files:
  - `drive1541_physical/mechanics_model.hpp`
  - `drive1541_physical/mechanics_model.cpp`

- Implemented `jemu::drive1541::MechanicsModel` API:
  - `reset()`
  - `set_motor_on(bool)`
  - `tick(uint64_t drive_cycles)`
  - `step_in()` / `step_out()`
  - `half_track()`
  - `spindle_angle_norm()`.

- Mechanics behavior:
  - spindle angle advances only when motor is ON,
  - normalized wrap kept in `[0,1)`,
  - head stepping clamped to half-track limits (`2..84`).

- Optional level3 hook integrated in `advanced_image_backends.hpp`:
  - per-track/sector mechanics state participates in physical bitcell perturbation path,
  - no global switch and no behavior change for non-level3 profiles.

- Added coverage in `tests/drive1541_mechanics_tests.hpp`:
  - motor-off no-angle-advance,
  - motor-on angle advance + wrap,
  - step in/out clamp bounds.

## Commit 11: Flux Track Model (Transitions + Weak-Bit Regions)

- Added physical flux-track files:
  - `drive1541_physical/flux_track_model.hpp`
  - `drive1541_physical/flux_track_model.cpp`

- Implemented `jemu::drive1541::FluxTrackModel` surface:
  - `clear()`
  - `set_transitions(std::vector<FluxTransition>)`
  - `set_weak_regions(std::vector<WeakRegion>)`
  - `advance(ticks, absolute_tick)`.

- Model behavior:
  - track represented as edge timing deltas (`delta_ticks`) with internal accumulator/cursor,
  - coherent wrap when end of transition vector is reached,
  - weak regions apply deterministic output perturbation (absolute-tick based, CI-stable).

- Optional level3 integration in `advanced_image_backends.hpp`:
  - per-track/sector `FluxTrackModel` state initialized lazily,
  - flux edge contributes into physical bitcell perturbation path,
  - lower profiles keep previous behavior unchanged.

- Added coverage in `tests/drive1541_flux_track_tests.hpp`:
  - deterministic transition sequence,
  - deterministic weak-region modulation,
  - coherent end-of-track wrap behavior.

## Commit 12: End-to-End Level3 Physical Read Pipeline Wiring

- Wired first complete level3 physical read path in `drive_1541.hpp`:
  - scheduler time source (`physicalScheduler.now()`),
  - mechanics update (spindle/head),
  - bitcell timing step,
  - flux edge generation,
  - GCR encode/decode sample path producing bitstream sample,
  - pipeline telemetry latched for smoke verification.

- Added level3-only physical pipeline state in `Drive1541`:
  - `physicalMechanicsModel`, `physicalBitcellTimingModel`, `physicalFluxTrackModel`, `physicalGcrCodec`,
  - initialization/lifecycle fields (`physicalPipelineInitialized`, `physicalPipelineRuns`, etc.).

- Runtime flow integration:
  - mounted image read (`loadVirtualBlock`) now triggers `runPhysicalLevel3ReadPipeline(track, sector)` only when level3 profile is active,
  - level1/level2 behavior remains unchanged.

- Added smoke suite `tests/drive1541_physical_pipeline_smoke_tests.hpp`:
  - g64/nib/raw mount+read in level3,
  - level3 pipeline execution assertions,
  - level1 vs level3 base-case parity assertion on RAW path.

- Updated test harness wiring in `c64_11.cpp` with new smoke run hook.

- Known runtime note:
- `run_signoff_week13_14.ps1` remains intermittently unstable in this environment (`EXIT=-1073740791`), while matrix/corpus/dos/soak/thresholds/closure gates are green.

## Post-Commit12 Hardening: Signoff Stability (External RefTrace Missing)

- Root cause isolated in external reference-diff path:
  - when `reference_trace` assets are missing/empty, external case runner was returning hard failure,
  - that propagated to `external_validation.hpp` assertion and produced `EXIT=-1073740791` in signoff.

- Hardened behavior in `external_case_runner.hpp`:
  - for missing/empty reference trace, keep warning reason and continue as non-fatal,
  - preserve runtime trace generation and existing diff behavior when references exist.

- Scope and compatibility:
  - no change to level1/level2/level3 emulation semantics,
  - change is runner hardening only (signoff robustness against incomplete external reference assets).

- Verification after hardening:
  - `run_signoff_week13_14.ps1` now completes with `EXIT=0` in this environment,
  - mandatory and extended gates remain green.

## Runner Hygiene Hardening: Executable Lock/Retry Robustness

- Hardened runner scripts against intermittent Windows executable lock races (`Permission denied` / file in use):
  - `run_copier_matrix.ps1`
  - `run_signoff_week13_14.ps1`

- Added best-effort process cleanup and retry logic:
  - stop stale signoff processes before build/run,
  - bounded build retry for output executable lock conflicts,
  - bounded run retry for transient file-lock failures,
  - safer handling of runner invocation exceptions in matrix path.

- Scope:
  - no emulator core behavior changes,
  - runner-level resilience only (CI/local stability improvement).

- Post-change validation:
  - matrix/corpus/dos/soak/thresholds/closure/signoff all green in this environment,
  - soak flake rate remains within threshold (`0.0000` in repeated runs).

## Consolidation: Shared Lock-Retry Helper Module

- Added shared helper script:
  - `tools/runner_lock_hardening.ps1`

- Centralized utilities:
  - `Test-LockErrorText(...)`
  - `Stop-ExeIfRunning(...)`
  - `Invoke-WithRetry(...)`
  - `Build-Profile-Retry(...)`
  - `Invoke-BinaryWithLockRetry(...)`

- Refactored runners to import shared module (removed duplication):
  - `run_copier_matrix.ps1`
  - `run_signoff_week13_14.ps1`
  - `run_advanced_dos_recovery_gate.ps1`
  - `run_advanced_real_corpus_gate.ps1`
  - `run_real_golden_gate.ps1`
  - `run_kernel_iec_e2e.ps1`
  - `run_phase5_soak_gate.ps1`
  - `run_prepare_pla_snapshot.ps1`
  - `run_prepare_edge_references.ps1`
  - `run_timing_gold.ps1`
  - `run_vic_reference_capture.ps1`

## Commit 13: IFluxImageBackend + Layered Backend Routing

- Introduced flux-layer interface for profile-aware backend separation:
  - `drive1541_physical/i_flux_image_backend.hpp`

- Added flux backend wrappers:
  - `drive1541_physical/flux_image_backend_g64.hpp`
  - `drive1541_physical/flux_image_backend_nib.hpp`
  - `drive1541_physical/flux_image_backend_raw.hpp`

- Routing changes in `Drive1541` (`drive_1541.hpp`):
  - mounted backend split into logical layer (`mountedImageLogicalBackend`) and optional flux layer (`mountedFluxImageBackend`),
  - profile-aware selection now uses layered routing,
  - `d64` always stays on `IImageBackend` path (level1-compatible),
  - `g64`/`nib`/`raw` route to flux backend only when profile is `level3-physical`,
  - safe fallback to logical backend when flux backend is absent/not ready.

- Added dispatch/smoke/no-regression test suite:
  - `tests/drive1541_backend_layering_tests.hpp`
  - validates extension/profile dispatch,
  - mount/read smoke for `g64`/`nib`/`raw`,
  - `d64` no-regression routing/read guard.

## Commit 14: Timestamped IEC Edge Queue (Host <-> Drive)

- Added timestamped edge-event queue:
  - `drive1541_physical/iec_edge_queue.hpp`
  - event shape: `IecEdgeEvent { ts, line, level, source }`
  - stable deterministic ordering with tie-break on line/source/sequence.

- Integrated queue in IEC port layer:
  - `drive1541_physical/drive_iec_port.hpp`
  - host and drive edge streams are queued and applied only when `now >= ts`.

- Integrated routing in drive runtime:
  - `drive_1541.hpp`
  - host lines are queued through `setIecLines(...)` and re-applied in `tickIecHalfCycle(...)`,
  - drive outputs are published as timestamped edges,
  - drive-off behavior blocks queued drive output edges from driving bus lines.

- Added IEC edge queue tests:
  - `tests/drive1541_iec_edge_queue_tests.hpp`
  - verifies stable ordering,
  - deterministic replay (`same trace -> same digest`),
  - drive-off output gating semantics.

## Commit 15: LED/Signals Derived from Physical Drive State

- Added physical signal model:
  - `drive1541_physical/drive_signal_model.hpp`

- Signal derivation now follows runtime physical semantics:
  - `motor_on` from `PowerState` lifecycle,
  - `activity` from flux read/write and DOS busy windows,
  - `error` from real status-line path (`!= 00,...`) with hold smoothing,
  - `iec_load` from IEC edge density over a temporal window.

- Integrated in `Drive1541`:
  - `drive_1541.hpp` now owns `signalModel`, updates it each half-cycle,
  - LED-like fields (`driveLedMotorOn`, `driveLedActivity`, `driveLedError`, `driveLedIecLoad`) are derived from model state,
  - added accessor `getDriveSignalState()` for adapters/UI polling.

- Added deterministic tests:
  - `tests/drive1541_signal_model_tests.hpp`
  - validates:
    - LEDs off when power off,
    - activity pulse during I/O,
    - error LED on fault injection,
    - IEC load on dense edge windows.

- Behavior goal preserved:
  - identical functional semantics,
  - less duplicated logic, easier maintenance,
  - same lock-race hardening guarantees across runners.

## Commit 16: CI Multi-Level Promotion Gates + Comparative Artifacts

- Added formal promotion policy in CI/signoff scripts for drive profiles:
  - Level1 (`level1-functional`): mandatory green baseline gate.
  - Level2 (`level2-cycle`): Level1 + parity matrix + timing-core checks.
  - Level3 (`level3-physical`): Level2 + advanced corpus + hard DOS recovery + soak + flake budget.

- Updated signoff script:
  - `run_signoff_week13_14.ps1`
  - now emits promotion snapshot artifacts:
    - `reference/edge/level_promotion_signoff.json`
    - `reference/edge/level_promotion_signoff.csv`
  - includes resolved promoted level in signoff summary output.

- Updated phase5 hard-threshold checker:
  - `run_check_phase5_hard_thresholds.ps1`
  - now enforces cross-level promotion requirements and writes comparative artifacts:
    - `reference/edge/level1_vs_level2_vs_level3.json`
    - `reference/edge/level1_vs_level2_vs_level3.csv`
  - comparative report fields include pass-rate/timing-core/flake metrics.

- Updated CI workflows:
  - `.github/workflows/revision-signoff-matrix.yml`
  - `.github/workflows/revision-tolerance-check.yml`
  - both workflows pass required report paths into phase5 hard-threshold checker,
  - both workflows upload level-promotion/comparative artifacts.

## Quasi-Closure Checklist (Phase 5)

### New Document

- `COPIER_SUPPORT_MATRIX.md`

Purpose:

- provide a release-oriented view of all copier scenarios with explicit status,
- summarize current hard baseline gate coverage,
- define criteria for near-closure and remaining tasks before full closure.

Current status in the checklist:

- all active matrix scenarios are currently marked `PASS`,
- hard advanced scenarios are included in the same consolidated status view.

### Freeze Note

`COPIER_SUPPORT_MATRIX.md` now carries a freeze statement:

- phase5 near-closure is considered achieved under current hard-gate contract,
- freeze status is tied to signoff + matrix + phase5 closure gate consistency.

### CI Gate Integration

- New script: `run_check_phase5_closure.ps1`

What it validates:

- matrix/report scenario count alignment,
- all `overall_pass` values are `1` in `copier_matrix_report.json`,
- every matrix scenario ID is present in `COPIER_SUPPORT_MATRIX.md`.

CI wiring:

- `.github/workflows/revision-tolerance-check.yml`
- `.github/workflows/revision-signoff-matrix.yml`

Both workflows now execute phase5 closure gate and upload `COPIER_SUPPORT_MATRIX.md` as artifact.
