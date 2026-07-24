# Implementation Plan: Fix P1 Audit Findings — W5500 ioLibrary_Driver

**Branch**: `002-fix-p1-audit-findings` | **Date**: 2026-07-18 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/002-fix-p1-audit-findings/spec.md`

## Summary

Apply 18 P1 audit fixes to the Wiznet ioLibrary_Driver C codebase. Fixes range from simple localized changes (protocol validation, PHY settle delay) to multi-location polling deadline additions. Each fix is on a separate branch off `fork/master`, independent of other P1 fixes and pending P0 PRs. Complex architectural findings (AUD-008, AUD-009, AUD-011, AUD-012) are addressed with minimum viable changes (documentation + targeted guards) rather than full redesigns.

## Technical Context

**Language/Version**: C (C99, embedded cross-compiled with arm-none-eabi-gcc)

**Primary Dependencies**: Wiznet ioLibrary_Driver (W5500 chip driver, SPI mode)

**Storage**: N/A

**Testing**: ASan/UBSan on x86-64 host for logic defects; multi-chip compile check across 7 `_WIZCHIP_` values

**Target Platform**: ARM Cortex-M bare-metal embedded MCUs with W5500 attached via SPI

**Project Type**: C library (embedded TCP/IP chip driver)

**Performance Goals**: Polling deadline additions should not alter hot-path throughput; bounded iterations use ~100-10000 cycle defaults

**Constraints**: Each fix branch must apply cleanly to `fork/master` (commit 39fae86). Fixes touching same file must be ordered by AUD number and use non-overlapping regions. All fixes compile under all 7 `_WIZCHIP_` values.

**Scale/Scope**: 18 P1 findings across 6 source files. Simple fixes: ~2-20 lines each. Polling deadline fixes: central helper + per-loop changes (~100-200 lines total). Architectural fixes: documentation + minimal guards (~20-50 lines each).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

The constitution is a placeholder template with no principles defined. No gates apply.

**Result**: PASS

## Project Structure

### Documentation (this feature)

```text
specs/002-fix-p1-audit-findings/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── tasks.md          # Phase 2 output
└── checklists/
    └── requirements.md
```

### Source Code (affected files)

```text
Ethernet/socket.c                  # AUD-006, AUD-007/036, AUD-014, AUD-015, AUD-016, AUD-037
Ethernet/socket.h                  # AUD-015 (nonblocking contract), AUD-016 (return semantics)
Ethernet/wizchip_conf.c            # AUD-010, AUD-013, AUD-039
Ethernet/wizchip_conf.h            # AUD-008 (IRQ masking guidance), AUD-011 (concurrency docs)
Ethernet/W5500/w5500.c             # AUD-007 (FSR/RSR loops), AUD-008, AUD-040/041
Ethernet/W5500/w5500.h             # AUD-040/041 (16-bit accessor macros)
Application/loopback/loopback.c    # AUD-017, AUD-018
Application/multicast/multicast.c  # AUD-017, AUD-018
```

**Structure Decision**: Single C library project. No new files. Architectural findings addressed with targeted changes + documentation.

## Complexity Tracking

No constitution violations. Three findings (AUD-008, AUD-011, AUD-012) are architectural but addressed minimally — full redesigns deferred.

| Finding | Why Minimal | Full Fix Deferred Because |
|---------|------------|--------------------------|
| AUD-008 | Document + add scheduling yield hook | Full mutex-based SPI locking requires RTOS integration decisions beyond library scope |
| AUD-009 | Document synchronous requirement in header comments | Status return API changes break all existing callback implementations |
| AUD-011 | Document single-task assumption + add assertions | Per-socket locking requires RTOS porting layer not present in library |
| AUD-012 | Add ISR-safe event snapshot wrapper | Full interrupt ownership model depends on RTOS integration |
