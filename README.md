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
