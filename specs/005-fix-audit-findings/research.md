# Research: Resolve Production Audit Findings

**Feature Branch**: `005-fix-audit-findings`
**Date**: 2026-07-23
**Scope**: W5500 core driver plus the RP2040/W55RP20 PIO, DMA, SPI, and GPIO integration

## Evidence Baseline

- The root driver is C11 source under `Ethernet/`; its public socket API and callback registration live in `Ethernet/socket.h` and `Ethernet/wizchip_conf.h`.
- The supported audited transport is the W55RP20/RP2040 path in `WIZnet-PICO-C/port/ioLibrary_Driver/`, built against Pico SDK 2.2.0.
- The W5500 has eight sockets. The RP2040 has two PIO blocks, four state machines per block, 32 shared instruction words per PIO block, and 12 DMA channels.
- `tests/Makefile` currently builds only `test_w5500_atomic_pointer_write`; the broader correctness and public API sources are not part of the default target.
- `test_w5500_correctness.c` contains a GNU nested function that creates an executable-stack trampoline and crashes under AddressSanitizer. `test_public_api_sanitizer.c` currently has eight assertion failures because its all-zero SPI fake does not model valid W5500 state transitions.
- `test_w5500_model.c` and `test_cbmc_model.c` replicate driver behavior instead of analyzing production source, so they cannot satisfy FR-023 as written.
- The full diagnostic under `tests/hardware/rp2040_w5500_diag_full/` already provides provenance, watchdog journaling, USB control, and staged network tests. It is the stronger basis for release evidence; the lean probe remains useful as a smoke gate.

## Decision 1: Feature Scope and Shared-Code Boundary

**Decision**: Implement and verify W5500 behavior and the RP2040/W55RP20 transport. Keep shared source compilable for W6300 and preserve established W6300 behavior, but do not extend this feature into W6300-only QSPI semantics.

**Rationale**: The feature specification explicitly includes the root W5500 path and nested RP2040 transport while excluding W6300-only behavior. `wizchip_conf.*`, `socket.*`, and the PIO transport contain shared preprocessor branches, so compile checks are required even when runtime acceptance targets W5500.

**Alternatives considered**:

- Split or fork the W5500 implementation: rejected because it duplicates shared ioLibrary logic and makes future fixes diverge.
- Remediate every W6300 runtime path: rejected as scope expansion without an audited W6300 hardware requirement.

## Decision 2: Lock Domains and Ordering

**Decision**: Use four distinct synchronization domains:

1. A global configuration lock for chip-wide configuration, lifecycle, callback registration, and the ephemeral-port allocator.
2. One non-recursive lock per socket for socket-local lifecycle and software state.
3. A non-recursive SPI bus mutex for a complete CS-active transaction.
4. A short RP2040 interrupt-safe critical section only for ISR-visible flags and transport state publication.

The total order is: global configuration lock, socket locks in ascending socket-number order, SPI bus mutex, then short interrupt-safe state protection. Normal socket operations take only their socket lock and the SPI bus mutex. Operations that need an ephemeral port finish the global-lock operation before entering ordinary socket work. Reset and memory-layout changes take the global lock and all socket locks in ascending order before touching hardware.

**Rationale**: Per-socket locks preserve independent-socket concurrency. Replacing the current shared bitfields with per-socket arrays eliminates cross-socket read-modify-write races. A Pico `mutex_t` leaves interrupts enabled and is intended for non-IRQ mutual exclusion (`pico/mutex.h:16-35`), unlike the current `critical_section_t`, which disables interrupts on the Cortex-M0+ calling core for the whole SPI frame.

**Alternatives considered**:

- One global lock around every socket call: rejected because it unnecessarily serializes all eight sockets and hides concurrency defects from tests.
- Keep payload-wide `critical_section_t`: rejected because `wizchip_conf.h:366-384` already warns against interrupt masking around DMA and because RP2040 lacks BASEPRI.
- Recursive socket locks: rejected because they conceal nested public-call defects and make ownership harder to prove.

## Decision 3: Socket Lock Ownership and Recovery

**Decision**: Validate pure arguments before acquiring a socket lock. After acquisition, use one cleanup label and exactly one unlock. Add internal lock-aware helpers, including a close/recovery helper that assumes the caller owns the socket lock; locked code must never call a public operation that reacquires the same lock.

Represent software state per socket instead of in shared bitfields:

- I/O mode
- send-in-progress state
- cached protocol mode
- remaining packet state
- faulted state

When command acceptance, completion, or transport status becomes ambiguous, mark that socket faulted and return `SOCKERR_DEADLINE` or `SOCKERR_IO`. Only close, status query, or chip reset is allowed while faulted. A bounded close attempts to return the socket to `SOCK_CLOSED`; failure leaves it faulted rather than reporting false success or resetting unrelated sockets automatically.

**Rationale**: `socket()->close()`, `listen()->close()`, and malformed MACRAW cleanup currently recurse into the same non-recursive lock. Direct-return validation macros also bypass unlocks. A per-socket fault state makes retry rules explicit after partially issued commands.

**Alternatives considered**:

- Automatically software-reset the entire chip after one socket timeout: rejected because it destroys unrelated live sockets.
- Leave timed-out sockets in their cached mode: rejected because TX/RX pointers or command acceptance may already have changed.

## Decision 4: Platform-Neutral Deadlines

**Decision**: Add a root callback contract for a monotonic microsecond clock and an optional wait hook:

```c
typedef uint64_t (*wizchip_now_us_cb_t)(void);
typedef void (*wizchip_wait_hook_cb_t)(void);

void reg_wizchip_time_cbfunc(
    wizchip_now_us_cb_t now_us,
    wizchip_wait_hook_cb_t wait_hook);
```

Add a timeout configuration with separate command-acceptance, socket-operation, and PHY deadlines. On RP2040, register `time_us_64()` from the pinned Pico SDK. If no clock callback is registered, retain `_WIZCHIP_POLL_MAX_` as a bounded compatibility fallback and still return `SOCKERR_DEADLINE` on exhaustion. Every wait uses both a real deadline when available and an iteration failsafe. The poll-only path has no portable wall-clock guarantee and cannot satisfy release timing evidence; every release-acceptance target must register the monotonic callback.

Default policy:

- Command-register acceptance: 10 ms engineering limit.
- Socket operation/data wait: at least 2 seconds and never shorter than the configured W5500 retransmission window, computed from RTR and RCR plus a 100 ms margin.
- PHY readback/reset completion: 10 ms after the required reset hold interval.
- Transport transfer: 100 ms default ceiling, configurable per transport instance and checked against a length-and-clock-derived expected duration.
- DMA abort retirement: 1 ms after disabling the PIO state machine and requesting channel abort.

**Rationale**: W5500 documentation says Sn_CR clearing means command acceptance, while completion is observed in Sn_IR or Sn_SR; it gives no host-visible maximum. The RP2040 DMA and PIO completion time also depends on DREQ rate and contention. Configurable engineering deadlines avoid invented normative limits while ensuring bounded behavior.

**Alternatives considered**:

- Pico `absolute_time_t` in root code: rejected because the core driver is platform-neutral.
- Iteration counts only: rejected on the audited RP2040 target because optimization and clock speed change elapsed time.
- Fail immediately without a clock callback: rejected because it would break existing source integrations that are still safely bounded by the poll fallback.

## Decision 5: Transport Resource Transaction and Lifecycle

**Decision**: Keep one active ioLibrary transport instance because the root `_WIZCHIP` callback table is a singleton. Preserve the existing handle type for source compatibility, but add status-returning lifecycle entry points and document the single-instance contract.

The transport state machine is:

```text
FREE -> OPENING -> READY -> TRANSFERRING -> READY
                    |             |
                    v             v
                 SLEEPING       FAULTED
                    |             |
                    +-----> CLOSING -> FREE
```

Open behavior:

1. Validate the configuration and reserve the state slot under short state protection.
2. Copy `wiznet_spi_config_t` into the state; do not borrow caller storage.
3. Use Pico SDK 2.2.0 `pio_claim_free_sm_and_add_program()` so state-machine claim and program placement succeed or unwind together (`hardware/pio.h:1993-2003`, `hardware/pio.c:404-462`).
4. Claim output and input DMA channels without panicking.
5. Configure GPIO and the state machine only after all resources are owned.
6. Publish the READY handle last.
7. On failure, unwind only owned resources in reverse order and return a concrete Pico error.

Close, sleep, wake, and reinitialize are serialized by the bus mutex and are idempotent in already-satisfied states. Close disables and clears the state machine, retires DMA safely, deasserts CS, clears cached framing, removes the PIO program, releases claims, clears `active_state`, and returns to FREE.

**Rationale**: The current code publishes `funcs` before allocation succeeds, reads an uninitialized slot pointer on exhaustion, borrows the config pointer, and unclaims still-active resources. Explicit state and ownership bits make every unwind auditable.

**Alternatives considered**:

- Preserve configurable multiple instances: rejected because the global `active_state` and root callback singleton cannot select independent concurrent instances safely.
- Allocate state dynamically: rejected because this embedded port already uses a fixed static pool and needs deterministic ownership.

## Decision 6: DMA and PIO Timeout Recovery

**Decision**: Do not call the unbounded Pico SDK `dma_channel_abort()` on a failure path. Implement a local bounded abort request using the same documented register mechanism: write the channel bit to `dma_hw->abort`, then poll that channel's BUSY bit against the absolute deadline. Honor RP2040-E13 by disabling the channel completion IRQ during abort and clearing any spurious completion status afterward (`hardware/dma.h:718-764`).

Recovery order is:

1. Deassert CS and disable the PIO state machine to stop further DREQ production.
2. Request abort for each owned DMA channel and wait within the abort deadline.
3. Clear FIFOs, debug flags, framing state, and pending DMA IRQ state after channels retire.
4. Mark the transport FAULTED and expose the original timeout or AHB error.
5. Permit explicit recover or close. Recover reinitializes the already-owned SM and channels without reusing partial data.

If a DMA channel remains BUSY after the abort deadline, keep it claimed and quarantine the transport. Close returns timeout and must not unclaim that active channel. A later close/recover may retry after the hardware retires. Do not reset the global DMA block because that would corrupt unrelated users.

**Rationale**: Pico SDK 2.2.0 `dma_channel_abort()` writes the abort bit and then waits indefinitely for BUSY to clear. The SDK has no per-channel reset bit; cooperative unclaim only clears bookkeeping (`hardware/dma/dma.c:34-51`). Quarantine is safer than handing a still-active channel to another subsystem.

**Alternatives considered**:

- A per-channel reset bit: rejected because RP2040 exposes no such facility.
- Reset the whole DMA peripheral: rejected because DMA is shared with USB, timers, and other application components.
- Unclaim after a timed-out abort: rejected as a use-after-release hazard.

## Decision 7: Error Propagation Without Replacing Legacy Data Callbacks

**Decision**: Keep the existing byte and burst callback signatures, but move SPI status storage out of the `_WIZCHIP.IF` union into an independent sibling structure and add a registrar:

```c
void reg_wizchip_spistatus_cbfunc(
    uint8_t (*check_busy)(void),
    int8_t (*get_error)(void),
    void (*clear_error)(void));
```

The RP2040 transport maintains a sticky last-error value until explicitly cleared. Low-level root transactions clear status before a callback, check busy/error before releasing transaction ownership, and move the root lifecycle to FAULTED on failure. The fault remains sticky so concurrent callers cannot overwrite the initiating error with a later success.

Add `SOCKERR_IO` after the existing `SOCKERR_DEADLINE`. Existing socket APIs propagate those negative values. Configuration and PHY operations that currently return status continue to do so; state-changing `void` APIs receive status-returning replacements or return-type upgrades where ordinary call statements remain source-compatible. High-level RP2040 initialization, reset, check, close, sleep, and wake functions return Pico error codes and all examples check them.

**Rationale**: `SPI_STATUS` currently contains `_check_busy` and `_get_error`, but that struct occupies the same `_WIZCHIP.IF` union storage as the SPI `_read_byte` and `_write_byte` callbacks. Registering either view can therefore overwrite the other view's function-pointer bytes. Replacing all legacy callbacks would break every port, while an independent sticky channel adds failure reporting without changing data callback call sites.

**Alternatives considered**:

- A global last-error that is cleared by every caller: rejected because different sockets could erase one another's failures.
- Sentinel byte values for failed reads: rejected because all 256 byte values are valid register data.
- Keep diagnostic-only error logging: rejected because FR-010 requires the initiating caller to receive failure.

## Decision 8: SPI Serialization and Interrupt Responsiveness

**Decision**: Register a Pico non-recursive `mutex_t` as the ioLibrary CRIS/bus-serialization callback. Initialize it, the global configuration mutex, and eight socket mutexes exactly once; register all lock callbacks during transport initialization. Do not use `critical_section_t` for a complete SPI transaction.

Measure interrupt responsiveness in the authoritative diagnostic during a maximum supported 16 KiB W5500 transfer. The mandatory release measurement schedules USB SOF service and derives its acceptance deadline from the USB 2.0 full-speed 1 ms frame interval. Every event scheduled by that measurement must be observed and serviced within the derived deadline; record event count, worst latency, missed events, and comparison with the pre-fix blackout. Additional application interrupt sources become acceptance obligations only when the diagnostic schedules them with their own documented source and derivation. The driver itself does not invent a universal RP2040 interrupt-latency maximum.

**Rationale**: RP2040 is Cortex-M0+ and lacks BASEPRI, so `critical_section_t` globally masks ordinary interrupts on that core. The bus must remain serialized through CS deassertion, but that does not require interrupt masking.

**Alternatives considered**:

- Release the lock during a VDM frame: rejected because another user could corrupt CS-active transaction framing.
- Promise a fixed universal latency number: rejected because neither RP2040 nor Pico SDK defines one for every application interrupt source.

## Decision 9: GPIO Ownership and Deferred Dispatch

**Decision**: Replace the core-wide generic callback registration with a raw handler dedicated to `PIN_INT`. The raw ISR checks only `PIN_INT`, acknowledges the falling-edge event as required by the raw-handler contract (`hardware/gpio.h:578-611`), records a pending flag, disables that edge source, and returns. It performs no SPI, blocking lock, or user callback work.

Add task-context registration and dispatch APIs with per-socket callback, event mask, and context storage:

```c
typedef void (*wizchip_gpio_irq_cb_t)(
    uint8_t sn, sockint_kind events, void *context);

int wizchip_gpio_interrupt_register(
    uint8_t sn, sockint_kind mask,
    wizchip_gpio_irq_cb_t callback, void *context);
int wizchip_gpio_interrupt_unregister(uint8_t sn);
int wizchip_gpio_interrupt_dispatch(void);
bool wizchip_gpio_interrupt_pending(void);
```

Registration validates all arguments before hardware access and merges masks rather than replacing other users' masks. Dispatch acquires ordinary task-context locks, reads SIR and each asserted Sn_IR, clears only owned write-1-to-clear bits, invokes only matching registered callbacks, and rearms the GPIO after the W5500 INT line is drained. Removing the final registration disables the edge source and removes the raw handler.

**Rationale**: The current handler ignores `gpio` and `events`, replaces the single generic callback for the whole core, and cannot identify sockets. Direct SPI in an ISR would conflict with the bus mutex and violates the documented no-socket-API-from-ISR contract.

**Alternatives considered**:

- Filter the current generic callback only: rejected because installing it still owns the core-wide generic callback slot.
- Read W5500 interrupt registers in the raw ISR: rejected because SPI may be busy and mutex acquisition is invalid in IRQ context.

## Decision 10: W5500 Socket Flag Contract

**Decision**: Validate protocol and flag arguments independently before locking or register access. Reject protocol bits outside the low-nibble mode value. Treat `SF_IO_NONBLOCK` as a software flag for every supported mode and strip it before writing Sn_MR.

W5500 hardware-option masks are:

| Mode | Accepted hardware options |
|------|---------------------------|
| TCP | `SF_TCP_NODELAY` |
| UDP | `SF_MULTI_ENABLE`, `SF_IGMP_VER2`, `SF_BROAD_BLOCK`, `SF_UNI_BLOCK` |
| MACRAW | `SF_ETHER_OWN`, `SF_BROAD_BLOCK`, `SF_MULTI_BLOCK`, `SF_IPv6_BLOCK` |
| IPRAW | none |

Require `SF_MULTI_ENABLE` when `SF_IGMP_VER2` or `SF_UNI_BLOCK` is present. MACRAW remains restricted to socket 0 by the W5500 contract. Any unknown bit rejects the request before side effects.

**Rationale**: The header documents these context-dependent aliases, while the current implementation rejects all nonzero MACRAW/IPRAW flags and accepts some undocumented UDP low bits.

**Alternatives considered**:

- Write the whole caller flag byte to Sn_MR: rejected because `SF_IO_NONBLOCK` overlaps the protocol nibble and is not a hardware option.
- Continue accepting undocumented bits: rejected because they are silently discarded or change mode semantics.

## Decision 11: PHY Reset, Power, and Link Notification

**Decision**: Implement PHY mode changes as read-modify-write operations that preserve unrelated fields, keep PHYCFGR.RST asserted high outside reset, use a real delay callback/clock rather than an optimizable empty loop, and verify exact masked readback within the configured PHY deadline. Use the existing vendor convention of a 200 microsecond PHYCFGR reset hold as an explicitly non-normative engineering default; tests identify it as a configured policy value rather than a datasheet minimum. Retain and separately verify the existing hardware reset-pin delays against the W5500 reset-timing minimum.

State-changing PHY helpers return status. Link sampling passes the observed `PHY_LINK_ON` or `PHY_LINK_OFF` value to the callback, and callback registration/reset initializes the previous-link state coherently.

**Rationale**: W5500 Datasheet v1.1.0e section 2.4 requires PHYCFGR.RST to be restored to one but does not define an exact bit-reset settle time. Current power helpers overwrite the whole register and leave RST low; the callback always reports zero.

**Alternatives considered**:

- Claim the poll-count loop represents a microsecond delay: rejected because optimization can remove or change it.
- Treat any non-power-down value as normal mode: rejected because unsupported mode values must fail validation.

## Decision 12: Cache and Multi-Register Coherence

**Decision**: Make buffer-size setters authoritative functions that update hardware and the byte-count caches together after successful transfer/readback while preserving existing call syntax. Clear all per-socket software state on close. After a verified software reset, reset socket software state and repopulate cache values from hardware defaults/readback.

Protect logical configuration transactions with the global lock, including:

- network identity and DNS/DHCP cache
- RTR/RCR timeout tuple
- interrupt masks
- eight-socket TX/RX memory layout
- reset save/restore
- PHY and mode-register read-modify-write operations

Use existing `wizchip_write16_5500()` for all eight remaining split 16-bit setters. Keep stable double-sampling for Sn_TX_FSR and Sn_RX_RSR, but return deadline/I/O failure rather than silently using an unstable final value.

**Rationale**: SPI transaction serialization protects one frame, not a logical series of frames. Direct buffer macros and reset currently leave cached values inconsistent with hardware.

**Alternatives considered**:

- Remove caches and read hardware every time: rejected because it increases bus traffic and does not solve logical-transaction interleaving.
- Keep two independent byte writes under separate transactions: rejected because readers can observe a torn 16-bit value.

## Decision 13: Zero-Length and Maximum Transfer Semantics

**Decision**: A zero-length public or low-level read/write succeeds as a no-op before locking, buffer access, CS assertion, framing-cache changes, or callback invocation. A null buffer is permitted only when length is zero.

For W5500, the supported payload maximum for one operation is the configured socket buffer capacity, up to 16 KiB. The transport retains a `uint16_t` length but rejects payloads that exceed the caller's configured socket capacity or overflow framing arithmetic. W6300 shared code receives overflow guards but no new W6300-only behavior.

Lifecycle calls serialize with transfers through the bus mutex. Close/sleep/reset wait for the current bounded transfer to finish; they do not asynchronously unclaim resources. Repeated close, sleep, and wake calls are idempotent. Transfers in SLEEPING, FAULTED, CLOSING, or FREE state return an invalid-state error.

**Rationale**: Current zero-length callbacks still transmit headers and can underflow counters. The W5500 socket memory contract, not the wider DMA counter, is the usable transfer limit.

**Alternatives considered**:

- Treat zero length as `SOCKERR_DATALEN`: rejected by FR-011.
- Allow the transport's full 65,535-byte type range for W5500 sockets: rejected because no socket can own that much W5500 memory.

## Decision 14: Production-Linked Verification

**Decision**: Build all host tests through `tests/Makefile` with strict C11 warnings. Create a reusable SPI/register fake that models hardware register side effects only; tests link unmodified production `socket.c`, `wizchip_conf.c`, and `w5500.c`. Do not copy driver algorithms into the fake.

Verification lanes are:

- GCC and Clang strict builds.
- AddressSanitizer plus UndefinedBehaviorSanitizer.
- ThreadSanitizer in a separate pthread concurrency binary because TSan cannot be combined with ASan.
- Production-linked fault injection for stuck Sn_CR, stalled status, transport I/O failure, and retry/cleanup.
- A CBMC harness that compiles production sources with nondeterministic hardware callbacks and lock instrumentation; no handwritten replica of socket state transitions.
- `cppcheck`, `clang-tidy`/Clang analyzer, and ARM `arm-none-eabi-gcc` W5500/W6300 compile checks.
- Optimized disassembly checks that confirm PHY delays call observable time functions and no executable stack is requested.

The GNU nested callback in `test_w5500_correctness.c` moves to file scope. All prebuilt test binaries remain generated artifacts and are not evidence unless rebuilt by the documented command.

**Rationale**: Current model tests can pass while production remains defective, and the sanitizer-named binary is not actually sanitizer-linked. Separate lanes provide evidence for memory, undefined behavior, races, and target compilation without incompatible instrumentation.

**Alternatives considered**:

- Expand only the handwritten model: rejected by FR-023.
- Use hardware-only evidence: rejected because deterministic fault and concurrency coverage is impractical on hardware alone.

## Decision 15: Hardware Harness and Latency Evidence

**Decision**: Revive `rp2040_w5500_diag_full` as the authoritative release harness and make it compile the same high-level transport, GPIO, locking, and root production sources as shipped. Remove warning suppression and repair stale clock/path macros. Keep `rp2040_w5500_probe` as a fast smoke gate for VERSIONR, three address-assignment cycles, 100 datagram exchanges, and receive-pointer advancement.

The full diagnostic adds resource-exhaustion/retry, zero-length, timeout/recovery, close/sleep/wake/reopen, real multicore socket locks, PHY cycles, GPIO filtering/dispatch, maximum-transfer latency, and status propagation. It records source revision, dirty state, build time, thresholds, measured values, and per-stage results.

**Rationale**: The full diagnostic already has staged protocol, provenance, watchdog, and host-controller infrastructure. Extending it avoids creating a third hardware harness and makes SC-003 through SC-005 and SC-009 reproducible.

**Alternatives considered**:

- Put all release checks in the lean probe: rejected because it lacks staged evidence and fault orchestration.
- Keep the full diagnostic archived: rejected because no other harness covers the required hardware failure and lifecycle paths.

## Decision 16: Audit Reconciliation and Release Gate

**Decision**: Generate `specs/005-fix-audit-findings/evidence.md` during implementation with one row for every `AUD-001` through `AUD-073` and every `CUR-001` through `CUR-019` current category. Each row follows the exact Markdown schema in `contracts/verification-evidence.md` and contains finding, requirement, both source revisions, method, command, evidence location, observed result, observation time, scope/supersession notes, and release-blocking status.

A finding is resolved only when its required production-linked host and hardware evidence passes. Replica-model-only evidence is labeled non-production and cannot satisfy FR-023. Hardware unavailability leaves hardware-dependent criteria blocked. Update `TODO.md`, `AUDIT-RESOLVED.md`, and `docs/security/SECURITY-REVIEW-2026-07-21.md` from the evidence record; do not preserve contradicted completion claims.

**Rationale**: Current documents claim sanitizer and verification success despite a crashing harness, eight assertion failures, and model-only proofs. A machine-checkable evidence matrix prevents narrative status from outrunning observed results.

**Alternatives considered**:

- Keep separate narrative summaries without a canonical matrix: rejected because they have already diverged.
- Mark historical items resolved from commit messages alone: rejected because FR-027 requires current evidence.

## Decision 17: Toolchain and Build Integration

**Decision**: Use C11 for root and port C sources, C++17 only where Pico SDK build infrastructure requires it, GNU Make for root host tests, and CMake 3.20 or newer for hardware diagnostics. Pin hardware evidence to Pico SDK 2.2.0 and `arm-none-eabi-gcc` 13.2.1 or a documented newer compatible version. Add `pico_sync`/mutex and existing `pico_time`, PIO, DMA, clocks, GPIO, TinyUSB, and watchdog libraries to the relevant targets.

Keep the nested top-level application's existing CMake minimum unchanged unless required by a concrete target feature; the test harnesses already require CMake 3.20. Do not add heap allocation or an RTOS dependency.

**Rationale**: These are the versions and build systems already present. The remediation needs no new runtime package, and static ownership remains appropriate for embedded determinism.

**Alternatives considered**:

- Introduce a new unit-test framework: rejected because the existing tests are self-contained C and the key gap is production linkage, not assertion syntax.
- Require an RTOS mutex: rejected because Pico SDK supplies a bare-metal multicore mutex.

## Resolved Unknowns

| Unknown | Resolution |
|---------|------------|
| Exact PHYCFGR reset settle time | No normative value exists; use the documented 200 microsecond vendor convention, observable delay callback, exact readback, and 10 ms configurable completion deadline. |
| Maximum W5500 command time | No fixed maximum exists; derive network completion deadline from RTR/RCR and retain configurable command/data limits. |
| RP2040 interrupt-latency maximum | No universal maximum exists; the diagnostic declares the serviced source and derives its deadline from that source, using the USB full-speed 1 ms frame interval for the release measurement. |
| DMA abort partial-data behavior | Discard partial data, disable PIO, request bounded abort, clear state after retirement, and quarantine any channel that remains BUSY. |
| Concurrent Sn_IR access | Per-socket locks serialize each Sn_IR; different sockets use distinct register addresses while the SPI bus mutex serializes frames. |
| Transport instance count | One active instance, matching the singleton root callback table. |
| GPIO callback context | Raw ISR records/acks only; task-context dispatch reads and clears W5500 status and invokes per-socket callbacks. |
| API compatibility | Preserve legacy data callback signatures; use additive status/time/GPIO contracts and source-compatible return-status upgrades for high-level operations. |
| Maximum transfer | Zero through configured socket capacity, at most 16 KiB for one W5500 socket operation. |
| Authoritative hardware harness | Full diagnostic for release evidence; lean probe as prerequisite smoke test. |

## Authoritative References

- WIZnet, **W5500 Datasheet v1.1.0e**, sections 2.3, 2.4, and 2.5: `https://docs.wiznet.io/img/products/w5500/W5500_ds_v110e.pdf`
- Raspberry Pi, **RP2040 Datasheet**, sections 2.5, 2.19, 3, and 4.1: `https://pip.raspberrypi.com/documents/RP-008371-DS-rp2040-datasheet.pdf`
- ARM, **ARMv6-M Architecture Reference Manual**, ARM DDI 0419.
- USB-IF, **USB 2.0 Specification**, section 11.18.2.
- Pinned Pico SDK 2.2.0:
  - `hardware/dma.h:718-764`
  - `hardware/dma/dma.c:23-51,73-84`
  - `hardware/pio.h:1938-2003`
  - `hardware/pio/pio.c:404-469`
  - `hardware/gpio.h:570-611,648-683,785-798`
  - `hardware/gpio/gpio.c:153-170,218-247`
  - `pico/mutex.h:16-35,117-184`
  - `pico/time.h:55-70,130-163`
- Production source evidence:
  - `Ethernet/socket.c`
  - `Ethernet/socket.h`
  - `Ethernet/wizchip_conf.c`
  - `Ethernet/wizchip_conf.h`
  - `Ethernet/W5500/w5500.c`
  - `Ethernet/W5500/w5500.h`
  - `WIZnet-PICO-C/port/ioLibrary_Driver/src/wizchip_qspi_pio.c`
  - `WIZnet-PICO-C/port/ioLibrary_Driver/src/wizchip_spi.c`
  - `WIZnet-PICO-C/port/ioLibrary_Driver/src/wizchip_gpio_irq.c`
