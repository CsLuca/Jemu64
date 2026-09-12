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

Terminal summary marker:

- `[COPIER-MATRIX] profile=... pass=X/Y ...`

Example:

```powershell
.\run_copier_matrix.ps1 -Profile fast -Manifest external_tests_manifest.json
```

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
