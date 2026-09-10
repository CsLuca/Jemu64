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
