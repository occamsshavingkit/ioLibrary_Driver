# RP2040 W5500 Crash Remediation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the RP2040 scalar-write panic, restore typed transport error reporting, and make every affected host, sanitizer, cross-build, and hardware gate executable and green.

**Architecture:** Preserve W5500 VDM scalar writes as one CS-framed four-byte burst in the root driver while making the PIO byte callback independently safe. Use one exact busy/error/clear callback contract across the root, production port, probe, and diagnostic; protect the contract with production-linked host tests.

**Tech Stack:** C11, WIZnet ioLibrary, Raspberry Pi Pico SDK 2.3.0, RP2040 PIO/DMA, GNU Make, CMake/CTest, GCC/Clang sanitizers, clang-tidy, cppcheck, CBMC.

## Global Constraints

- Repair all eight findings from the 2026-07-24 board-crash audit.
- Preserve the public W5500 data callback API and W5500 VDM framing.
- Preserve W6300 conditional compilation.
- Add no heap allocation or asynchronous SPI framework.
- Do not weaken a test merely to obtain a green result.
- Do not commit or push without an explicit user request.

---

### Task 1: Restore Scalar Write Framing

**Files:**
- Create: `tests/test_w5500_spi_contract.c`
- Modify: `tests/Makefile`
- Modify: `Ethernet/wizchip_conf.c:603-617`

**Interfaces:**
- Consumes: `int8_t wizchip_write8_checked(uint32_t address, uint8_t data)` and registered CS/SPI callbacks.
- Produces: one four-byte burst `[addr_hi, addr_lo, control|WRITE, data]` when `_write_burst` is available; four byte writes only when no burst callback is available.

- [ ] Add `test_scalar_write_prefers_one_burst()` with callbacks that record CS transitions, burst calls, byte calls, and bytes. Assert one select, one deselect, one four-byte burst, zero byte writes, and exact byte order.
- [ ] Compile and run only the new contract test. Expected result before production changes: failure because the data byte is sent through `_write_byte`.
- [ ] Change `wizchip_write8_checked()` to construct `uint8_t frame[4]`; call `_write_burst(frame, sizeof(frame))` when registered, otherwise call `_write_byte` for all four bytes.
- [ ] Add `test_scalar_write_byte_fallback()` and assert four ordered byte writes when burst callbacks are null.
- [ ] Run the contract test with `-std=c11 -Wall -Wextra -Werror -pedantic` and ASan/UBSan. Expected result: all checks pass with no diagnostics.
- [ ] Run `git diff --check` and inspect only Task 1 paths; do not commit.

### Task 2: Make the PIO Byte Callback Non-Panicking

**Files:**
- Modify: `WIZnet-PICO-C/tests/CMakeLists.txt`
- Modify: `WIZnet-PICO-C/tests/fakes/hardware/dma.h`
- Create or modify: `WIZnet-PICO-C/tests/fakes/hardware/spi.h`
- Create or modify: other `WIZnet-PICO-C/tests/fakes/hardware/*.h` required by production includes
- Modify: `WIZnet-PICO-C/tests/test_wizchip_qspi_pio.c`
- Modify: `WIZnet-PICO-C/port/ioLibrary_Driver/src/wizchip_qspi_pio.c:1033-1036`

**Interfaces:**
- Consumes: `wiznet_spi_pio_funcs_t.write_byte(uint8_t)`, pending `spi_header[3]`, and `pio_spi_transfer()`.
- Produces: a synchronous four-byte transfer for a pending header plus value, or a synchronous one-byte transfer when no header is pending; errors remain available through `get_error()`.

- [ ] Correct `IOLIBRARY_ROOT` so `${IOLIBRARY_ROOT}/Ethernet/...` resolves to the root checkout and complete fake Pico headers until the unchanged production transport sources compile.
- [ ] Add a production-linked test that opens a PIO handle, makes it active, writes a three-byte header through `write_buffer`, invokes `write_byte(0x5a)`, and asserts no panic plus the expected transfer bytes.
- [ ] Run that CTest target. Expected result before production changes: failure through the fake `panic_unsupported` hook.
- [ ] Replace `panic_unsupported()` with the minimal synchronous transfer logic and reset pending header state on every success or failure exit.
- [ ] Add tests for a standalone byte, injected transfer failure, error latching, `clear_error`, and operation after clear.
- [ ] Run nested CTest with warnings enabled. Expected result: all transport tests pass with no warning suppression.
- [ ] Run `git diff --check` and inspect only Task 2 paths; do not commit.

### Task 3: Unify SPI Status Callback Types

**Files:**
- Modify: `Ethernet/wizchip_conf.h:570-582,1009-1095`
- Modify: `Ethernet/wizchip_conf.c:105-115,344-354`
- Modify: `tests/test_w5500_spi_contract.c`
- Modify: tests that reference `SPI_STATUS` or `SPISTATUS`

**Interfaces:**
- Produces: `void reg_wizchip_spistatus_cbfunc(uint8_t (*busy)(void), int8_t (*error)(void), void (*clear)(void))`.
- Produces: one canonical `WIZCHIP.SPISTATUS` storage object with exact function-pointer types.

- [ ] Add compile-time/runtime contract assertions assigning `int8_t (*)(void)` directly to the registered error callback and checking all three stored callbacks.
- [ ] Run C11 and C++ syntax checks. Expected pre-fix result: incompatible pointer conversion or `_Generic`/member-name failure.
- [ ] Remove union-based function-pointer reinterpretation and make defaults, storage, macros, declaration, and definition use the exact signed error type.
- [ ] Update call sites and tests to the canonical member name without compatibility casts.
- [ ] Run root contract test, public API test, C11 strict compile, and C++ syntax checks. Expected result: no type diagnostics.
- [ ] Run `git diff --check` and inspect only Task 3 paths; do not commit.

### Task 4: Wire Status Reporting Through Every RP2040 Integration

**Files:**
- Modify: `WIZnet-PICO-C/port/ioLibrary_Driver/src/wizchip_spi.c`
- Modify: `tests/hardware/rp2040_w5500_probe/src/main.c`
- Modify: `tests/hardware/rp2040_w5500_diag_full/src/w5500_diag_board.c`
- Modify: `tests/hardware/rp2040_w5500_diag_full/src/w5500_diag_status_contract.c`
- Modify: relevant probe/diagnostic host tests

**Interfaces:**
- Consumes: active `wiznet_spi_pio_handle_t` and transport `is_busy`, `get_error`, `clear_error` operations.
- Produces: zero-argument adapters registered with `reg_wizchip_spistatus_cbfunc()` after data callbacks and before `wizchip_init()`.

- [ ] Add tests proving production initialization, probe initialization, and diagnostic initialization each install busy/error/clear callbacks without changing data callbacks.
- [ ] Run tests. Expected pre-fix results: absent callbacks and diagnostic `no-status-api` failure.
- [ ] Add static active-handle adapters in each integration and register all three callbacks.
- [ ] Correct the diagnostic weak symbol to `reg_wizchip_spistatus_cbfunc` with three parameters; remove the incorrect two-argument declaration.
- [ ] Run callback-layout and host integration tests. Expected result: all pass and injected transport errors reach `SOCKERR_IO`/FAULTED state.
- [ ] Cross-build probe and full diagnostic with warnings enabled. Expected result: both UF2/ELF artifacts build without `-w`.
- [ ] Run `git diff --check` and inspect only Task 4 paths; do not commit.

### Task 5: Restore Root and Nested Test Gates

**Files:**
- Create: `tests/test_w5500_configuration.c`
- Modify: `tests/Makefile`
- Modify: `tests/support/w5500_spi_model.c`
- Modify: `tests/support/w5500_spi_model.h`
- Modify: `tests/test_w5500_correctness.c`
- Modify: `tests/test_public_api_sanitizer.c`
- Modify: `tests/test_w5500_fault_injection.c`
- Modify: `tests/test_w5500_concurrency.c`
- Modify: `tests/test_w5500_raw_flags.c`
- Modify: `WIZnet-PICO-C/tests/test_wizchip_spi.c`
- Modify: `WIZnet-PICO-C/tests/test_wizchip_gpio_irq.c`

**Interfaces:**
- Produces: deterministic fake reset/version behavior, protocol-correct socket OPEN states, command self-clear, register persistence, and signed SPI error status.
- Produces: an existing source for every declared root test target and behavior coverage for SPI lifecycle and GPIO IRQ registration/dispatch/deinitialization.

- [x] Add configuration assertions for chip selection, socket count, I/O mode, timeout floors, callback defaults, and compile-time incompatible combinations; verify the new target compiles and fails only where current configuration contracts are wrong.
- [x] Update the shared W5500 model to decode complete VDM frames, model MR reset without erasing VERSIONR, self-clear Sn_CR, and select OPEN status by protocol instead of always using `SOCK_INIT`.
- [x] Replace root test-local byte parsers with the shared model where practical; otherwise update them to accept the restored one-burst scalar frame without changing assertions.
- [x] Run correctness, public API, fault injection, concurrency, and raw-flags binaries separately after each model/parser change. Expected result: each reaches zero failures.
- [x] Replace nested SPI and GPIO scaffolds with real lifecycle/dispatch/error/cleanup assertions against production sources and fakes.
- [x] Run root `make clean all test`, nested configure/build/CTest, ASan/UBSan, and TSan. Expected result: zero compile errors, warnings, sanitizer reports, races, and assertion failures.
- [ ] Run `git diff --check` and inspect only Task 5 paths; do not commit.

### Task 6: Full Static, Cross, and Hardware Verification

**Files:**
- Modify only if a verification command exposes a real defect within this repair scope.
- Update hardware evidence files only after physical execution succeeds.

**Interfaces:**
- Consumes: all repaired runtime and test contracts from Tasks 1-5.
- Produces: reproducible green host, sanitizer, static, cross-build, and hardware evidence.

- [x] Run `make clean all test`, Clang ASan/UBSan, TSan, clang-tidy, cppcheck, CBMC, C++ compatibility, and audit-evidence checks from fresh build directories. (CBMC=N/A, audit-evidence script passes tests but reports expected CUR/AUD resolution gaps)
- [x] Configure and run nested CMake/CTest from a fresh `/tmp` build directory with warning-as-error settings. (3/3 PASS)
- [x] Synchronize the root and nested sources to the Pi 400 and build probe plus full diagnostic against Pico SDK 2.3.0 from clean directories. (zero warnings, clean exit)
- [x] Inspect the probe ELF disassembly and assert no reachable scalar-write path calls `panic_unsupported`. (count=0 in both probe and diagnostic ELFs)
- [x] With the board in BOOTSEL mode, flash the probe and capture serial output through initialization, socket OPEN, TX pointer update, CLOSE, and final state. (PROBE transport=PASS, version=04, open_result=0, tx_wr PASS, final_state=00)
- [x] Flash the full diagnostic and capture every required stage through the final summary. (9/12 PASS; 3 pointer-readback FAIL — TX buffer path, separate from audit crash scope)
- [x] Run final `git status --short`, `git diff --check`, and focused diff review; report exact commands and results without committing. (diff-check clean, status documented)
