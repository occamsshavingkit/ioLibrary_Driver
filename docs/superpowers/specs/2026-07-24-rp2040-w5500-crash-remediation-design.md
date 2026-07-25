# RP2040 W5500 Crash Remediation

## Context

The RP2040 probe reaches transport initialization and reads `VERSIONR == 0x04`,
then stops inside `wizchip_init()`. The current root driver splits a scalar
W5500 write into a three-byte burst header followed by `_write_byte`. The PIO
transport's `_write_byte` callback calls `panic_unsupported()`, so the first
reset-register write terminates the firmware. The previous root implementation
sent scalar writes as one four-byte burst and did not exercise that callback.

The verification system cannot currently protect this boundary. The root test
target names a missing configuration test, the nested transport suite does not
compile against its Pico SDK fakes, two nested tests are scaffolds, and no host
test combines the production checked write path with the PIO callback table.
Several root tests that do compile also fail against the current lifecycle and
register-model behavior.

## Goal

Restore a non-panicking, error-reporting W5500 SPI transaction contract across
the root driver, RP2040 PIO transport, normal port initialization, hardware
probe, and full diagnostic. Restore executable host and cross-build gates that
detect this class of integration regression before hardware flashing.

## Runtime Design

### Scalar Writes

`wizchip_write8_checked()` will preserve the W5500 VDM transaction shape used
before the regression. When a burst-write callback is registered, it will send
the three-byte address/control header and one-byte value in a single four-byte
burst while chip select remains asserted. If no burst callback exists, it will
fall back to four byte-callback invocations.

The PIO transport's `_write_byte` callback will also become safe. If a
three-byte header is pending, the callback will combine it with the supplied
byte and execute one synchronous transfer. Without a pending header, it will
execute a one-byte synchronous transfer. It will never call
`panic_unsupported()` for a valid callback invocation.

### Status and Error Propagation

SPI status callbacks will use one exact type contract:

- Busy callback: `uint8_t (*)(void)`.
- Error callback: `int8_t (*)(void)`.
- Clear callback: `void (*)(void)`.

The implementation will remove incompatible function-pointer reinterpretation.
The normal Pico port, smoke probe, and full diagnostic will register transport
busy, error, and clear callbacks. Checked root transactions will convert any
nonzero transport error into the existing root I/O failure and FAULTED state.

The full diagnostic's weak registrar declaration will use the production symbol
name and complete three-callback signature. Its callback-layout stage will
verify status registration without altering the data callback table.

## Verification Infrastructure

The nested CMake project will resolve the root ioLibrary path consistently from
its documented `IOLIBRARY_ROOT`. Pico fake headers will have valid include
guards and paths, and the fake SDK will provide every header and primitive
needed by the linked production transport sources.

Scaffolding tests will be replaced with behavior tests. The root suite will add
the missing configuration test rather than removing the declared gate. Blanket
`-w` suppression will be removed from hardware transport targets; any necessary
third-party warning suppression must be narrow and documented.

Existing root failures will be resolved by comparing each expectation with the
approved lifecycle, locking, deadline, and register-model contracts. Tests will
not be weakened merely to obtain a green result.

## TDD Sequence

1. Add a root regression proving scalar writes use one four-byte burst and do
   not call `_write_byte` when burst support exists; verify it fails.
2. Repair scalar framing and verify that regression and existing checked-I/O
   tests pass.
3. Add a transport regression invoking the real PIO function table's
   `_write_byte` with a pending header; verify the current panic path fails.
4. Repair the callback and verify synchronous transfer and error latching.
5. Add status-registration tests, then repair callback typing and all three
   hardware/port integrations.
6. Repair the nested build foundation and replace scaffolds with lifecycle,
   dispatch, timeout, error, and recovery tests.
7. Add the missing root configuration test and resolve root regression groups
   one at a time, rerunning the affected test after each change.
8. Run full host, sanitizer, thread-sanitizer, static-analysis, C++ syntax,
   evidence, and cross-compilation gates.
9. Flash the smoke probe and full diagnostic, then capture complete hardware
   transcripts.

## Scope

- Repair all eight findings in the 2026-07-24 board-crash audit.
- Preserve the public W5500 data callback API and existing W5500 VDM framing.
- Preserve W6300 conditional behavior unless a shared-source compile correction
  is required.
- Keep runtime state static; add no heap allocation or new framework.
- Make no unrelated socket, protocol, or example refactors.

## Non-Goals

- Do not redesign ioLibrary around a new asynchronous SPI API.
- Do not remove compatibility solely to simplify the repair.
- Do not mark hardware evidence complete from host or cross-build results.
- Do not commit or push changes without an explicit user request.

## Acceptance Criteria

- Scalar W5500 writes complete without panic and retain one CS-framed command.
- PIO read, write, burst, timeout, error, clear, lifecycle, and recovery paths
  have production-linked host coverage.
- Probe and full diagnostic install independent status callbacks successfully.
- Root `make test`, ASan/UBSan, and TSan complete with zero failures.
- Nested CMake/CTest configures, builds, and completes with zero failures.
- Static analysis and C++ compatibility checks fail the gate on real errors.
- RP2040 probe and diagnostic cross-build without blanket warning suppression.
- Hardware probe reaches memory initialization, socket open, TX pointer update,
  close, and final state without panic or hang.
- Full diagnostic completes its callback, transport, lifecycle, PHY, GPIO,
  concurrency, and latency stages with captured evidence.
