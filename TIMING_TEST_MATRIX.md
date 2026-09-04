# Timing/Subcycle Test Matrix

Use `external_tests_timing_gold.json` as the practical matrix for CPU + C64 timing coverage.

Run:

```powershell
./run_timing_gold.ps1
```

Notes:

- `cpu_klaus_6502_functional` and `cpu_klaus_6502_decimal` are wired as required.
- `all-suite-a`, Lorenz C64 tests (`BRKN`, `JSRW`, `RTIN`), and VICE CPU/CIA/VICII samples are wired to local `.prg` assets.
- `lorenz_beqr.prg` is intentionally excluded from the gold manifest because it is a loader that asks for Disk2.
- `instr_test-v5` from `nes-test-roms` has been removed from the gold manifest because it is NES-specific (`.nes`) and not a valid 6510/C64 compliance gate.
- Missing placeholder files were replaced with concrete C64 PRG proxies in the gold manifest:
  - `cpu_timing_branch_pagecross_proxy` -> `roms/vice_testprogs_cpu.prg` (branch/timing-sensitive CPU path)
  - `cpu_timing_rmw_dummy_reads_proxy` -> `roms/Illegal_Codes.prg` (RMW/illegal opcode bus-sensitive stress)
  - `cpu_timing_irq_nmi_delay_proxy` -> `roms/lorenz_cpu_irq_illegal.prg` (IRQ/BRK/illegal interaction timing path)
- These proxies keep timing pressure in CI while dedicated C64 timing PRGs are not yet available.
- When dedicated assets are available, replace proxies with deterministic per-test criteria (`pass_pc`, `fail_pc`, `expect_mem_*`, `require_pc*`).
