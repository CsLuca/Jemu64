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
