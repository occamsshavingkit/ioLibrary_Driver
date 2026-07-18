# Quickstart: Validating P2 Audit Fixes — W5500 ioLibrary_Driver

## Prerequisites
- arm-none-eabi-gcc for cross-compile
- gcc/clang with -fsanitize=address for ASan tests
- fork/master at audit commit 39fae86

## Per-Fix Verification

Each fix follows pattern: create branch from fork/master → apply fix → compile for W5500 → push.

### Code fixes (AUD-019, 020, 021, 022, 023, 024, 025, 038, 042, 043, 044, 045, 049)
Verify single-commit correctness per TODO.md verification criteria. Compile for W5500 only (non-structural changes).

### AUD-026 (explicit build)
Build without -D_WIZCHIP_ must fail with `#error`. Build with -D_WIZCHIP_=5500 must succeed.

### AUD-027 (callback registration)
Call reg_wizchip_bus_cbfunc() in SPI build; must return error immediately without hang.

### AUD-028 (strict-C)
Compile with gcc/clang -std=c99 -Wall -Wextra -Wpedantic -Wundef -Wformat=2 -Werror. Must pass without errors. Verify legacy IINCHIP_WRITE_BUF alias compiles.

## System-Level
For each fix branch: `git log --oneline fork/master..HEAD | wc -l` must be 1.
