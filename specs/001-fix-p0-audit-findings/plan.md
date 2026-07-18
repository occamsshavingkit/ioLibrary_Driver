# Implementation Plan: Fix P0 Audit Findings — W5500 ioLibrary_Driver

**Branch**: `001-fix-p0-audit-findings` | **Date**: 2026-07-18 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/001-fix-p0-audit-findings/spec.md`

## Summary

Apply 5 surgical P0 (deployment-blocker) bug fixes to the Wiznet ioLibrary_Driver C codebase, targeting the W5500 Ethernet chip in SPI mode. Each fix is a single, atomic code change addressing a specific confirmed defect: buffer overrun in UDP loopback, wrong output width in a socket-option getter, uninitialized SPI callback union member, inverted check order in nonblocking TCP recv(), and unconditional NULL dereference in ctlwizchip(). Each fix gets its own PR from the occamsshavingkit fork to Wiznet/ioLibrary_Driver master.

## Technical Context

**Language/Version**: C (C99, embedded cross-compiled with arm-none-eabi-gcc)

**Primary Dependencies**: Wiznet ioLibrary_Driver (W5500 chip driver, SPI mode); no external libraries.

**Storage**: N/A (no persistent storage; fixes are in-memory correctness issues)

**Testing**: ASan/UBSan on x86-64 host for host-compilable paths; hardware-in-loop on W5500 EVB for board-level verification.

**Target Platform**: ARM Cortex-M bare-metal embedded MCUs with W5500 attached via SPI.

**Project Type**: C library (embedded TCP/IP chip driver)

**Performance Goals**: No performance change; fixes correct erroneous behavior without altering hot-path characteristics.

**Constraints**: Fixes must compile under `_WIZCHIP_ == 5500` with `_WIZCHIP_IO_MODE_` in SPI mode. Must not break W5100, W5200, W5300, W6100, or W6300 builds.

**Scale/Scope**: 5 atomic changes across 3 source files (`Application/loopback/loopback.c`, `Ethernet/socket.c`, `Ethernet/wizchip_conf.c`). Each change is ~2-10 lines.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

The constitution is a placeholder template with no principles defined. No gates apply. All fixes follow existing code conventions in their respective files.

**Result**: PASS (no applicable gates)

## Project Structure

### Documentation (this feature)

```text
specs/001-fix-p0-audit-findings/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (N/A — no external interfaces)
├── tasks.md             # Phase 2 output (not created by /speckit.plan)
└── checklists/
    └── requirements.md  # Spec quality checklist
```

### Source Code (repository root)

The fixes touch existing files only:

```text
Application/loopback/loopback.c    # AUD-001
Ethernet/socket.c                  # AUD-002, AUD-004
Ethernet/wizchip_conf.c            # AUD-003, AUD-005
```

**Structure Decision**: Single C library project. No new files or directories needed.

## Complexity Tracking

No constitution violations. No complexity to justify.
