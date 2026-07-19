# RP2040 W5500 Diagnostic Firmware Design

Date: 2026-07-19
Status: Implemented; accepted final design decisions recorded

## Context

The existing temperature-server firmware is not a suitable diagnostic target. It combines TinyUSB, W5500 initialization, DHCP, HTTP, OPC UA, DNS, SNTP, sensors, interrupts, and an event scheduler. A failure in one driver operation can therefore appear as unrelated USB or network failure.

The diagnostic firmware will be a new standalone program in this repository. It will use the current ioLibrary working tree directly and will not link, copy, flash, or depend on the temperature-server firmware.

Two concrete driver hazards motivate the harness:

1. `setSn_TX_WR` and `setSn_RX_RD` acquired an outer WIZCHIP critical section and then called `WIZCHIP_WRITE`, which acquires the same critical section. The RP2040 port uses a non-recursive `critical_section_t`, so the second acquisition deadlocks.
2. `SPI_STATUS` was added as a member of the `WIZCHIP.IF` union. It occupies the same storage as `SPI`, so `_check_busy` aliases `_read_byte` and `_get_error` aliases `_write_byte`. `_spi_status_check` consequently invokes transport callbacks through incompatible status callback signatures while chip select and the critical section are active. Removing status checks hides this defect; the correct design requires independent callback storage and registration.

The harness must make these failures observable without relying on UART or SWD.

## Goals

- Identify the exact driver operation that hangs, times out, or returns incorrect data.
- Preserve and test intended improvements such as atomic pointer updates and SPI error propagation instead of reverting them to upstream behavior.
- Exercise byte transfers, burst transfers, pointer accessors, socket state, static UDP, and DHCP independently.
- Support automatic smoke testing and repeatable command-driven experiments in one UF2.
- Recover from a blocking call and report the last active stage using the RP2040 hardware watchdog and scratch registers.
- Produce machine-readable transcripts tied to the exact driver revision under test.
- Build and run independently of all application firmware.

## Non-Goals

- Temperature sensing or ADC testing.
- HID keyboard status output.
- HTTP, OPC UA, DNS, SNTP, TCP, or application-level service testing.
- General-purpose board firmware.
- Hiding driver failures with retries, callback substitutions, or compatibility shims.
- Changing production driver behavior inside the harness.

## Project Location

The project will live under:

```text
tests/hardware/rp2040_w5500_diag/
```

Planned components:

```text
CMakeLists.txt                 Standalone Pico SDK firmware build
README.md                      Build, flash, and diagnostic usage
src/main.c                     Boot, USB service loop, and command dispatch
src/diag_runner.c              Stage state machine and result reporting
src/diag_runner.h
src/diag_protocol.c            Line-oriented command and event protocol
src/diag_protocol.h
src/diag_watchdog.c            Scratch journal and watchdog recovery
src/diag_watchdog.h
src/w5500_diag_board.c         RP2040 PIO transport and callback registration
src/w5500_diag_board.h
src/usb_descriptors.c          CDC-only diagnostic USB descriptors
host/diag_host.py              Pi controller, transcript recorder, and UDP echo
host/test_diag_host.py         Host protocol tests
```

The exact split may be reduced during planning if two adjacent modules remain clearer as one file.

## Build Boundary

The firmware CMake project will accept:

- `PICO_SDK_PATH` for the installed Pico SDK.
- `WIZNET_PICO_C_PATH` for the WIZnet RP2040 PIO transport implementation only.
- `IOLIBRARY_ROOT` for this repository's current checkout.

It will compile `wizchip_conf.c`, `socket.c`, W5500 sources, and DHCP sources from `IOLIBRARY_ROOT`. It must not resolve those sources from the installed WIZnet package.

The build will embed:

- Git commit SHA.
- Dirty flag for the synchronized diagnostic, `Ethernet`, and
  `Internet/DHCP` source set.
- A 64-hex SHA-256 of the complete `git diff --binary HEAD` stream over that
  source set, including staged and unstaged tracked changes.
- Build timestamp.
- Diagnostic protocol version.

`DIAG_DIFF_SHA256` is present for both clean and dirty builds. A clean source
set hashes the empty binary diff and uses
`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`; a dirty
source set hashes the complete scoped stream. Synchronization rejects untracked
source files. Ignored generated files must be explicitly excluded from both the
provenance check and synchronization, and any other ignored source file is
rejected. Therefore every synchronized source byte is represented by `HEAD` or
the embedded diff hash.

The diagnostic USB device will have a unique product string and PID, distinct from the temperature server, so host automation cannot select the wrong UF2 or serial device.

## Board Transport

The harness will implement a small purpose-built board adapter using the public Pico SDK and WIZnet PIO transport API. It will retain the W55RP20-EVB-PICO wiring and non-recursive critical-section semantics:

- CS: GPIO 20
- SCK: GPIO 21
- MISO: GPIO 22
- MOSI: GPIO 23
- INT: GPIO 24
- RESET: GPIO 25

The adapter will explicitly register CS, byte SPI, burst SPI, and critical-section callbacks. It will register SPI-status callbacks only through an independent public API exposed by the driver revision under test. This avoids the production port's combined initialization routine and unbounded PHY wait while preserving the transport behavior relevant to the bug.

Status callbacks must occupy storage independent from the SPI transport union. A structural preflight stage will report offsets and callback addresses before the first W5500 transfer. Revisions with aliased storage or no safe registration API will fail that stage explicitly; the harness will not write status callbacks into aliased storage.

## Execution Model

### Boot

1. Initialize clocks, TinyUSB CDC, watchdog state, and no W5500 facilities.
2. Read reset cause and watchdog scratch journal.
3. Enumerate as the diagnostic USB device.
4. If the previous reset occurred during a stage, emit a `TIMEOUT` event for that exact stage and enter recovery command mode.
5. Otherwise run the local smoke stages through `pointer-api`, stopping when a failure invalidates a later stage's prerequisite.
6. Wait for the Pi controller to supply network configuration and request the full suite.
7. Remain in command mode after completion.

This prevents a failed stage from causing an automatic reset loop and avoids hard-coding a potentially conflicting static IP address.

### Commands

The CDC command set will be deliberately small:

```text
help
status
list
run <stage>
run all
repeat <stage> <count>
net <device-ip> <mask> <gateway> <host-ip> <host-port>
reboot
```

Commands are newline-terminated ASCII. Malformed commands return a protocol error and never invoke a stage.

### Events

Every state transition emits one newline-terminated, key-value record:

```text
DIAG protocol=1 seq=17 stage=pointer-api event=START timeout_ms=2000
DIAG protocol=1 seq=17 stage=pointer-api event=PASS elapsed_us=418
DIAG protocol=1 seq=17 stage=pointer-api event=FAIL code=readback expected=1234 actual=1200
DIAG protocol=1 seq=17 stage=pointer-api event=TIMEOUT reset=watchdog phase=set-tx-wr
```

Values that can contain spaces will not be emitted. This keeps parsing deterministic without adding a JSON library to the firmware.

The accepted final record limit is 256 bytes including the newline. The
mandatory fixed-width provenance event is 194 bytes including its newline, and
both firmware and host framing tests cover that event without truncation.

## Watchdog Journal

Before each potentially blocking driver call, the firmware will:

1. Write a journal magic value, protocol version, stage ID, sequence number, and phase ID to watchdog scratch registers.
2. Emit and flush the `START` event while servicing TinyUSB.
3. Arm the hardware watchdog for the stage's blocking-call deadline.
4. Invoke exactly one driver operation.
5. Clear or update the journal only after the operation returns.

Primitive calls use a short hardware deadline. UDP and DHCP are non-blocking state machines with longer software deadlines; they feed the watchdog only between bounded driver calls.

After a watchdog reset, startup reports the preserved journal as `TIMEOUT`, clears it, and waits in recovery mode. The failing operation is not automatically retried.

## Diagnostic Stages

Stages run in this order for `run all`. Each stage may also run independently after its prerequisites are satisfied.

### `callback-layout`

- Report the addresses or offsets of SPI transport and SPI status callback storage.
- Verify status storage does not alias byte or burst transport callbacks.
- Verify the revision exposes a safe, independent status callback registration contract.
- Perform no W5500 transfer.

### `transport-init`

- Initialize the PIO transport.
- Initialize the non-recursive RP2040 critical section.
- Register transport, CS, and critical-section callbacks.
- Register status callbacks only after `callback-layout` confirms an independent registration contract.
- Report callback registration state.

### `chip-reset`

- Drive the hardware reset pin through the required timing sequence.
- Leave the W5500 deselected.

### `version`

- Read `VERSIONR` using one byte transfer.
- Require value `0x04`.

### `memory-init`

- Configure 2 KiB TX and RX memory for each of the eight sockets through `wizchip_init`.
- Do not use an unbounded PHY-link loop.

### `phy-link`

- Poll PHY link with a software deadline while servicing USB.
- Report link up, link down, or transfer failure distinctly.

### `single-register`

- Save an 8-bit writable register.
- Write and read a safe pattern using byte operations.
- Restore the saved value.
- Report exact readback on mismatch.

### `burst-register`

- Save a multi-byte writable register block.
- Write and read a pattern with `WIZCHIP_WRITE_BUF` and `WIZCHIP_READ_BUF`.
- Restore the saved bytes.
- Report the first mismatching byte.

### `pointer-sequential`

- On a closed socket, save TX write and RX read pointers.
- Write known 16-bit values with explicit high-byte and low-byte operations.
- Read back and restore both pointers.

### `pointer-burst`

- Repeat the pointer test with one two-byte buffer operation per pointer.
- This directly tests W5500 VDM auto-increment behavior independently of the public macros.

### `pointer-api`

- Invoke the actual `setSn_TX_WR` and `setSn_RX_RD` macros under watchdog protection.
- Read back and restore both values.
- The known nested critical-section revision is expected to time out in this stage.

### `socket-open`

- Apply host-supplied static network configuration.
- Open one UDP socket.
- Verify socket state, TX free space, and RX received size.
- Snapshot relevant socket registers on failure.

### `udp`

- Send a sequence-numbered payload to the Pi controller's UDP echo socket.
- Receive and verify the echoed payload.
- Report send and receive pointer values before and after transfer.
- Apply bounded ARP, send, and receive deadlines.

### `dhcp`

- Reset chip network and socket state.
- Reinitialize W5500 TX/RX memory and perform bounded PHY preparation inside
  every invocation.
- Run the repository's DHCP client on one socket.
- Service the DHCP time handler and USB from a bounded loop.
- Log DHCP state changes and socket register snapshots.
- Pass only after a complete lease provides IP, subnet, gateway, and DNS values.

This self-contained boundary is the accepted repeat behavior: every
`repeat dhcp` iteration performs its own reset, memory initialization, and PHY
preparation rather than replaying the earlier catalog stages.

No HTTP, TCP, or application service starts after DHCP.

## Pi Host Controller

`host/diag_host.py` will use Python's standard library where practical. It will:

- Locate the diagnostic CDC device by its unique USB identity or accept an explicit device path.
- Record raw events with host timestamps and firmware build provenance.
- Send network configuration derived from explicit command-line arguments.
- Reject network options with `status`; status remains read-only.
- Run a UDP echo endpoint for the `udp` stage.
- Request automatic or individual stage runs.
- Detect sequence gaps, malformed events, and device reconnects.
- Reconnect after watchdog reset and collect the recovered `TIMEOUT` event.
- Exit nonzero if any requested stage fails or times out.

The controller will never infer success solely from USB enumeration or DHCP address presence.

## Error Handling

- Ordinary validation failures return `FAIL` with a stable code and relevant expected/actual values.
- A stage restores saved registers after ordinary failure where hardware access remains valid.
- Destructive groups begin with a chip reset rather than depending on previous socket state.
- Blocking calls are never retried inside a stage.
- Pointer and UDP repeats restart from their documented catalog prerequisite
  boundaries; DHCP repeats invoke the self-contained DHCP reset/memory/PHY
  preparation described above.
- Protocol errors do not reset hardware or alter network configuration.
- Link-down is reported separately from SPI failure.
- USB disconnect does not cancel an active hardware operation, but the watchdog journal remains authoritative.

## Verification

### Host Verification

- Compile protocol and state-machine code with warnings as errors.
- Test command parsing, event formatting, stage prerequisite checks, journal encoding, and timeout recovery as pure host-side logic where possible.
- Test the Python transcript parser and UDP echo controller.
- Verify the firmware links ioLibrary sources from `IOLIBRARY_ROOT`, not the installed package.

### Hardware Acceptance

1. The UF2 enumerates with the diagnostic product identity and unique PID.
2. The transcript includes the exact driver commit, dirty flag, and diff hash.
3. An isolated revision containing the nested outer CRIS wrappers but corrected status callback storage reports `TIMEOUT` at `pointer-api` without a permanent reset loop.
4. A revision with aliased SPI status storage reports a deterministic `callback-layout` failure.
5. A corrected revision passes byte, burst, sequential pointer, burst pointer, and public pointer API stages.
6. The corrected revision completes 100 pointer API repetitions without timeout or readback mismatch.
7. The corrected revision completes 20 UDP echo exchanges without payload, pointer, or socket-state mismatch.
8. The corrected revision acquires three DHCP leases across independent chip resets.
9. USB remains responsive throughout all non-hanging stages.

The full transcript is the hardware evidence used to accept or reject each driver correction.

## Constraints And Risks

- Watchdog scratch registers survive watchdog resets but not power removal; host transcripts remain the durable record.
- A transport failure before USB enumeration cannot produce a live log, so W5500 initialization starts only after USB is available.
- Static UDP depends on explicit, non-conflicting network configuration supplied by the Pi controller.
- The PIO adapter must preserve the production port's pin assignment, clocking, and non-recursive lock behavior or results will not be representative.
- Tests must vary one driver behavior at a time. A failed experiment returns to evidence collection rather than accumulating additional source changes.

## Deliverable Boundary

The first deliverable is the diagnostic firmware, host controller, tests, and usage documentation. Driver corrections discovered with it remain separate changes and require their own tests and hardware transcripts.
