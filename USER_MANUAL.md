# Jemu64 User Manual

## Emulator Console IEC Cable Workflow

This section documents the physical-style IEC cable workflow in the emulator console.

### 1) Start Console Mode

PowerShell:

```powershell
$env:JEMU_EMULATOR_CONSOLE = "1"
.\c64_11.exe
```

You will see the `jemu>` prompt.

### 2) Create and Connect Cable

Use one cable name (example: `IEC0`) and connect host/device ends explicitly.

```text
CABLE CREATE IEC0
CABLE IEC0 CONNECT HOST C64
CABLE IEC0 CONNECT DRIVE 8
```

### 3) Mount Media and Power On

```text
DRIVE 8 ATTACH "C:\path\to\disk.d64"
DRIVE 8 POWER ON
C64 ON
```

### 4) Run Emulation and Inspect State

```text
STEP 5000
CABLE IEC0 STATE
DRIVE 8 STATE
STATUS
```

### 5) Hotplug Scenarios (Realistic Cable Events)

```text
CABLE IEC0 DISCONNECT DRIVE 8
STEP 500
CABLE IEC0 CONNECT DRIVE 8
STEP 2000
STATUS
```

### 6) Shutdown Sequence

```text
C64 OFF
DRIVE 8 POWER OFF
CABLE IEC0 DISCONNECT DRIVE 8
CABLE IEC0 DISCONNECT HOST C64
CABLE IEC0 STATE
QUIT
```

## Cable Command Reference

- `CABLE CREATE <name>`
- `CABLE <name> CONNECT HOST C64`
- `CABLE <name> DISCONNECT HOST C64`
- `CABLE <name> CONNECT DRIVE <8..11>`
- `CABLE <name> DISCONNECT DRIVE <8..11>`
- `CABLE <name> STATE`
- `CABLE STATE` (quick global cable status)

## Compatibility Notes

- Legacy command `DRIVE <unit> CABLE ON|OFF` remains available.
- Preferred workflow is explicit cable endpoint operations via `CABLE ...` commands.
