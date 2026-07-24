# Feature Specification: Fix P1 Audit Findings — W5500 ioLibrary_Driver

**Feature Branch**: `002-fix-p1-audit-findings`

**Created**: 2026-07-18

**Status**: Draft

**Input**: User description: "fix the P1 findings in @TODO.md : make sure each fix for an audit finding can be cleanly applied to current main with the intent to open a PR individually for each fix when wiznet accepts the current outstanding PRs."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - AUD-006 (P1): Reject unsupported protocols before OPEN

An integrator who accidentally opens an IPv6 or dual-mode socket on W5500 hardware must receive an immediate `SOCKERR_SOCKMODE` error instead of entering an unbounded wait loop. Similarly, MACRAW must be rejected for any socket other than 0.

**Why this priority**: Invalid public API calls can enter an unbounded wait rather than returning an error, hanging the calling task. This is a remotely triggerable denial-of-service vector.

**Independent Test**: Exhaust every protocol/socket combination in a W5500 register model. Unsupported combinations must return immediately without issuing an OPEN command to the hardware.

**Acceptance Scenarios**:

1. **Given** a W5500 build, **When** `socket()` is called with `Sn_MR_UDP6` or `Sn_MR_UDPD`, **Then** `SOCKERR_SOCKMODE` is returned immediately.
2. **Given** a W5500 build, **When** `socket()` is called with `Sn_MR_MACRAW` on socket 1-7, **Then** `SOCKERR_SOCKMODE` is returned immediately.

---

### User Story 2 - AUD-007/AUD-036 (P2): Add bounded deadlines to W5500 polling loops

Every polling loop in the W5500 driver must have a monotonic deadline. A missing/reset W5500, stuck MISO, or peer that never advances must not monopolize the caller. The `recv()` deadlock in `SOCK_CLOSE_WAIT` with zero RX and pending TX (AUD-036) must return within a bounded deadline.

**Why this priority**: Unbounded polling loops are availability risks. A stuck SPI line or unresponsive peer can hang the caller indefinitely, consuming CPU without a scheduling point.

**Independent Test**: Fault-inject MISO stuck high and low, hold reset active, suppress Sn_CR clearing. Every API must return within its documented deadline.

**Acceptance Scenarios**:

1. **Given** a stuck SPI line (MISO held high), **When** any polling loop executes, **Then** the function returns with a timeout error within a bounded number of iterations.
2. **Given** a TCP socket in `SOCK_CLOSE_WAIT` with zero RX data and pending TX, **When** `recv()` is called, **Then** the function returns within a bounded deadline.

---

### User Story 3 - AUD-008 (P3): Bound interrupt-masked SPI transfer durations

Full-payload SPI transfers must not hold global interrupt masking for the entire transfer duration. A 16 KiB transfer at 10 MHz blocks interrupts for over 13 ms, potentially missing control/UART/CAN interrupts.

**Why this priority**: Extended interrupt masking causes missed real-time events and potential deadlock if the SPI callback waits for an interrupt-driven DMA completion.

**Independent Test**: Instrument interrupt mask (PRIMASK) and a GPIO around critical SPI callbacks at the slowest supported rate. Confirm interrupt-off time is within the product requirement.

**Acceptance Scenarios**:

1. **Given** a DMA-driven SPI callback, **When** a bulk transfer is initiated while interrupts are masked, **Then** the system does not deadlock.
2. **Given** the slowest supported SPI rate, **When** any single critical section completes, **Then** interrupts are masked for no more than the product IRQ-off budget.

---

### User Story 4 - AUD-009 (P4): Synchronous, error-reporting SPI callbacks

SPI callbacks must complete synchronously before `WIZCHIP_CRITICAL_EXIT()` deasserts CS. Callbacks that start DMA and return must not permit CS to rise and stack-local data to expire. I/O errors must be propagated to socket callers.

**Why this priority**: Asynchronous callbacks leave CS deasserted while data is still in flight, corrupting transfers. Unreported I/O errors allow socket operations to silently fail.

**Independent Test**: A logic analyzer must show no SCLK edge after CS rises. Inject HAL busy/timeout/overrun conditions and require a bounded I/O error with CS inactive.

**Acceptance Scenarios**:

1. **Given** an SPI callback that returns before the transfer completes, **When** the W5500 access function continues, **Then** CS remains asserted until the callback indicates completion.
2. **Given** an SPI bus error condition, **When** a transfer fails, **Then** the error is propagated to the calling socket API.

---

### User Story 5 - AUD-010 (P5): Probe W5500 identity during initialization

W5500 initialization must validate chip presence by reading `VERSIONR == 0x04` and reading back critical configuration registers. An absent or failed chip must not appear initialized.

**Why this priority**: An undetected absent/failed W5500 causes confusing hangs on the first socket operation rather than a clear initialization failure.

**Independent Test**: Disconnect MISO, hold RSTn low, return a wrong version. Initialization must fail by deadline with a clear error.

**Acceptance Scenarios**:

1. **Given** a W5500 with MISO disconnected, **When** `wizchip_init()` is called, **Then** the function returns a failure code within a bounded deadline.
2. **Given** a W5500 returning VERSIONR != 0x04, **When** `wizchip_init()` validates the chip, **Then** initialization fails with a version mismatch error.

---

### User Story 6 - AUD-013 (P6): Validate TX/RX buffer layouts before hardware access

W5500 socket buffer configuration must be fully validated before any SPI write or chip reset. Invalid individual values or totals must be rejected.

**Why this priority**: Programming invalid buffer sizes (values other than 0,1,2,4,8,16) produces invalid hardware memory maps causing stalls or data corruption.

**Independent Test**: Exhaust all byte values per entry and over-limit totals. Invalid layouts must fail before the first callback; valid layouts must match register readback.

**Acceptance Scenarios**:

1. **Given** a buffer configuration containing an unsupported size (e.g., 3, 5, 7), **When** `wizchip_init()` validates the layout, **Then** the function returns a configuration error before any SPI write.
2. **Given** TX and RX totals exceeding 16 KiB each, **When** `wizchip_init()` validates the layout, **Then** the function returns a configuration error.

---

### User Story 7 - AUD-014 (P7): Partial IPRAW receives progress on every call

IPRAW packets larger than the caller's buffer must be consumed in multiple `recvfrom()` calls. Continuation calls must not return zero or skip data.

**Why this priority**: Partial IPRAW receive stalls prevent large packets from being consumed, effectively breaking IPRAW reception for any packet exceeding the caller's buffer size.

**Independent Test**: Receive a 100-byte IPRAW packet through a 50-byte buffer. Require returns of 50 and 50 followed by remaining size zero.

**Acceptance Scenarios**:

1. **Given** a 100-byte IPRAW packet queued on the W5500, **When** `recvfrom()` is called with a 50-byte buffer twice, **Then** 50 bytes are returned on each call and remaining size is zero after the second call.
2. **Given** RX-ring wraparound for a multi-chunk IPRAW packet, **When** `recvfrom()` is called, **Then** the data is reassembled correctly across the ring boundary.

---

### User Story 8 - AUD-015 (P8): Nonblocking sendto() after SEND command

A nonblocking datagram socket must not spin for SENDOK or TIMEOUT after issuing the SEND command. It must return after issuing SEND and resolve completion on a subsequent call.

**Why this priority**: A nonblocking datagram caller can monopolize the CPU and SPI bus waiting for ARP/network completion, defeating the purpose of nonblocking mode.

**Independent Test**: Hold SENDOK low in a W5500 model. A nonblocking call must return after a bounded number of frames.

**Acceptance Scenarios**:

1. **Given** a nonblocking UDP socket with SENDOK delayed, **When** `sendto()` is called, **Then** the function returns after issuing the SEND command, before SENDOK is asserted.
2. **Given** a blocking UDP socket, **When** `sendto()` is called, **Then** the function waits for SENDOK or TIMEOUT with a bounded deadline.

---

### User Story 9 - AUD-016 (P9): Align socket API returns with command completion

Blocking socket APIs must wait for the relevant hardware state transition before returning success. Nonblocking APIs must expose an explicit pending state. Delayed-but-valid transitions must not be treated as failure.

**Why this priority**: APIs returning success before hardware completion expose `SOCK_BUSY` on the next call, and valid delayed transitions can be treated as failure, confusing callers.

**Independent Test**: Model immediate Sn_CR clear followed by delayed SENDOK and TIMEOUT transitions. Assert documented return behavior.

**Acceptance Scenarios**:

1. **Given** a blocking `send()` call where Sn_CR clears before SENDOK, **When** the function returns, **Then** either SENDOK has been observed or a timeout error is returned.
2. **Given** a nonblocking `send()` call, **When** the function returns before SENDOK, **Then** a distinct pending state is returned so the caller knows to retry.

---

### User Story 10 - AUD-017 (P10): Stop Application send loops from spinning on SOCK_BUSY

Application send loops must handle `ret == SOCK_BUSY` (value 0) as deferred progress, not retry immediately. Loops that only reject negative results will add zero and spin indefinitely.

**Why this priority**: When a prior TCP send is pending or I/O mode changes, application send loops can enter a tight SPI polling loop until watchdog reset.

**Independent Test**: Delay SENDOK and repeatedly return SOCK_BUSY. Each Application invocation must return within a fixed execution budget.

**Acceptance Scenarios**:

1. **Given** a TCP send that returns SOCK_BUSY, **When** the application send loop processes the return, **Then** the loop yields and retries on the next invocation rather than spinning.
2. **Given** a UDP send with pending SENDOK, **When** the loopback function retries, **Then** it resumes at the correct offset without sending duplicate data.

---

### User Story 11 - AUD-018 (P11): Preserve UDP peer metadata across partial receives

When a UDP datagram is received in multiple chunks, the peer address and port must remain valid across continuation reads. Application variables that are recreated on every invocation must persist this metadata.

**Why this priority**: Continuation data can be sent to uninitialized or stale addresses, silently delivering responses to the wrong peer.

**Independent Test**: Receive a multi-chunk datagram, deliberately clobber stack memory between calls, and require every response chunk to retain the original peer.

**Acceptance Scenarios**:

1. **Given** a multi-chunk UDP datagram, **When** the application reads continuation data, **Then** the original peer address and port are used for response routing.
2. **Given** a single-chunk UDP datagram, **When** metadata is read from a subsequent empty receive buffer, **Then** no stale metadata is propagated.

---

### User Story 12 - AUD-039 (P12): PHY reset settle delay before register re-access

After cycling the PHY reset bit, the driver must wait for the PHY register file to stabilize (approximately 165 µs minimum) before re-reading `PHYCFGR`. Reads before stabilization return stale link status and OPMDC values.

**Why this priority**: Non-deterministic PHY status after reset can cause `wizphy_setphypmode()` to spuriously pass or fail depending on silicon temperature and voltage.

**Independent Test**: Logic-analyzer the SPI during `wizphy_reset()` and assert no `PHYCFGR` read occurs before the minimum settle time has elapsed.

**Acceptance Scenarios**:

1. **Given** a PHY reset is issued, **When** the driver re-reads PHYCFGR, **Then** sufficient settle time has elapsed for the PHY register file to stabilize.
2. **Given** the PHY fails to stabilize within the bounded deadline, **When** the settle poll completes, **Then** a failure is returned rather than a stale success.

---

### User Story 13 - AUD-040/AUD-041 (P13): Atomic 16-bit register access

16-bit W5500 register reads and writes must be performed atomically within a single critical section to prevent torn values when hardware advances the register between byte accesses. Applies to both writes (AUD-040: Sn_TX_WR, Sn_RX_RD, Sn_PORT, Sn_DPORT, Sn_MSSR, etc.) and reads (AUD-041: Sn_TX_RD, Sn_TX_WR, Sn_RX_RD, Sn_RX_WR, etc.).

**Why this priority**: Torn writes produce corrupted RX/TX pointers (duplicated, dropped, or mis-parsed datagrams). Torn reads compute bogus buffer offsets, causing on-wire data corruption.

**Independent Test**: Static: expand each accessor macro and verify a single CRITICAL section covers both byte accesses. Dynamic: ISR stress test showing no torn values.

**Acceptance Scenarios**:

1. **Given** a high-priority ISR issuing Sn_CR_RECV repeatedly, **When** a task calls setSn_RX_RD, **Then** no torn value is observed by the chip.
2. **Given** hardware advancement of Sn_TX_WR during an in-flight SEND command, **When** wiz_send_data reads Sn_TX_WR, **Then** a coherent 16-bit value is returned.

---

### User Story 14 - AUD-037 (P14): Resolve disputed SENDOK race (dead-code cleanup)

The second `sock_is_sending` check block in W5500 TCP `send()` (socket.c:582-596) is unreachable dead code. If the fourth-pass analysis is correct, remove the dead code and document the single-event ownership model.

**Why this priority**: Dead code confuses readers and the audit report itself. Clean up the unreachable code and document the single-event ownership model.

**Independent Test**: Code review confirms the block is unreachable on W5500 path — the bit is always cleared before reaching line 582.

**Acceptance Scenarios**:

1. **Given** the W5500 path through TCP `send()`, **When** control reaches line 582, **Then** the `sock_is_sending` bit is guaranteed clear (cleared at line 544 on the SENDOK branch), making the second check block at lines 582-596 unreachable. The dead code is removed and a comment documents the control-flow proof.

---

### User Story 15 - AUD-011 (P15): Per-socket concurrency and serialized SPI bus access

Shared socket state must be protected against concurrent access. Two callers on the same socket must not read the same TX/RX pointer and overwrite or duplicate data. Different-socket callers must not lose read-modify-write updates to shared global state.

**Why this priority**: Concurrent socket access can corrupt TX/RX data, produce duplicate source ports, and swap destination metadata on `sendto()`. This is an architectural defect affecting all multi-task deployments.

**Independent Test**: Force preemption after pointer reads, destination writes, mask loads, and port loads. Stress-test with tagged payloads for at least one million operations with no corruption.

**Acceptance Scenarios**:

1. **Given** two callers on the same socket, **When** both attempt to send data concurrently, **Then** data is not duplicated or overwritten.
2. **Given** eight sockets independently sending and receiving, **When** tested for one million operations, **Then** no port collisions, data corruption, or state inconsistency occurs.

---

### User Story 16 - AUD-012 (P16): Single software owner for socket interrupt events

Socket interrupt events must have exactly one consumer. An ISR that blanket-clears socket interrupts must not remove events from polling APIs. The ISR should snapshot events into software-pending bits and wake the owner task.

**Why this priority**: Mixed ISR and polling interrupt handling can cause missed events: `sendto()` waiting forever for SENDOK, or TCP send state stuck permanently busy.

**Independent Test**: Raise SENDOK and TIMEOUT, interleave ISR handling immediately before each poll, and require exactly-once delivery with no hang.

**Acceptance Scenarios**:

1. **Given** an ISR that clears Sn_IR bits, **When** a polling API is about to check for SENDOK, **Then** the event is not lost.
2. **Given** a polling API that consumes SENDOK, **When** an ISR fires between the hardware read and the software clear, **Then** the event is delivered to exactly one consumer.

---

### Edge Cases

- What happens when multiple P1 fixes modify the same code region (e.g., AUD-040 and AUD-041 both modify w5500.h 16-bit macros)? They must be applied in dependency order and produce non-overlapping changes.
- What happens when a P1 fix depends on a P0 fix that is still pending upstream? Each fix branch must be independently applicable to fork/master.
- How are P1 fixes verified without W5500 hardware? Host-based register models and ASan/UBSan harnesses for logic defects; board-level verification for timing/interrupt behavior.
- AUD-037: confirmed unreachable per fourth-pass analysis. No timing-dependent path exists — the `sock_is_sending` bit is guaranteed clear before line 582.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001 (AUD-006)**: The `socket()` function for W5500 builds MUST reject unsupported protocol modes (IPv6, dual-mode) and MACRAW on sockets 1-7 by returning `SOCKERR_SOCKMODE` before any SPI access or hardware OPEN command.
- **FR-002 (AUD-007)**: All W5500 polling loops (command completion, state polling, data-ready, interrupt-status) MUST include bounded iteration counts with deadlines, yielding or returning timeout errors when limits are exceeded.
- **FR-003 (AUD-036)**: The TCP `recv()` polling loop MUST return from the `SOCK_CLOSE_WAIT`/zero-data path within a bounded deadline rather than spinning indefinitely.
- **FR-004 (AUD-008)**: SPI critical sections spanning full-payload transfers MUST NOT hold global interrupt masking. ISR latency must be bounded to the product requirement or a scheduling-yield mechanism must replace global masking.
- **FR-005 (AUD-009)**: SPI callback semantics MUST be documented as synchronous (complete before return) with error status propagation. CS MUST remain asserted until the callback signals completion.
- **FR-006 (AUD-010)**: `wizchip_init()` and `wizchip_sw_reset()` MUST validate W5500 presence by reading `VERSIONR == 0x04` and reading back critical configuration, with bounded deadlines and clear error returns.
- **FR-007 (AUD-013)**: `wizchip_init()` MUST validate socket buffer size arrays and totals using a wide unsigned accumulator BEFORE any chip reset or SPI write. Invalid values or over-limit totals MUST be rejected.
- **FR-008 (AUD-014)**: IPRAW `recvfrom()` MUST calculate `pack_len` and copy payload on every invocation, not only on the first chunk. Partial receives MUST consume partial data and advance the read pointer.
- **FR-009 (AUD-015)**: Nonblocking `sendto()` MUST return after issuing the SEND command without spinning for SENDOK or TIMEOUT. Completion MUST be resolved on a subsequent call.
- **FR-010 (AUD-016)**: Blocking socket APIs MUST define and document acceptance vs. completion semantics. Blocking calls MUST wait for the relevant Sn_IR/Sn_SR transition; nonblocking calls MUST expose pending state.
- **FR-011 (AUD-017)**: Application send loops in `loopback.c` and `multicast.c` MUST handle `SOCK_BUSY` (return value zero) as deferred progress: yield and retry with preserved offset state.
- **FR-012 (AUD-018)**: Application UDP receive loops MUST persist peer metadata across partial datagram receives. Variables recreated on each invocation MUST retain peer address and port until `PACK_COMPLETED`.
- **FR-013 (AUD-039)**: `wizphy_reset()` MUST enforce a documented minimum settle delay (approximately 165 µs) between clearing and re-setting the PHY reset bit, with bounded polling for PHY stability before returning.
- **FR-014 (AUD-040)**: 16-bit W5500 register write macros MUST wrap both byte writes in a single `WIZCHIP_CRITICAL_ENTER/EXIT` span to prevent torn writes.
- **FR-015 (AUD-041)**: 16-bit W5500 register read macros for hardware-mutated registers MUST use the seqlock-style read-twice pattern (or a single-VDM burst inside one critical section) to prevent torn reads.
- **FR-016 (AUD-037)**: The unreachable SENDOK race code path at `Ethernet/socket.c:582-596` MUST be removed and replaced with a comment documenting the control-flow proof that the second `sock_is_sending` block is dead code on the W5500 path.
- **FR-017 (AUD-011)**: Shared socket state (global arrays, per-socket bitfields) MUST have documented concurrency protection. Same-socket concurrent access MUST be serialized; cross-socket global state MUST be protected.
- **FR-018 (AUD-012)**: Socket interrupt events MUST have a single defined consumer. ISR and polling paths MUST coordinate event processing so no event is lost or processed twice.
- **FR-019**: Each fix MUST be on a separate branch from `fork/master`, MUST compile under all 7 `_WIZCHIP_` values, and MUST be submitable as an independent PR to Wiznet/ioLibrary_Driver.
- **FR-020**: Fixes that touch the same file region MUST be ordered so each applies cleanly: lower AUD number first, non-overlapping changes only per branch.

### Key Entities

- **Audit Finding (AUD)**: A confirmed P1 defect from the TODO.md audit snapshot, identified by its AUD number, affected source files, failure description, prescribed action, and verification criteria.
- **Polling Helper**: A centralized wrapper that bounds iteration count, provides a monotonic deadline, and returns a distinct timeout error code.
- **Concurrency Lock**: Per-socket or SPI-bus lock mechanisms ensuring serialized access to shared state and hardware.
- **16-bit Accessor**: Macros or helper functions for reading/writing 16-bit W5500 registers atomically within a single critical section.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All 16 P1 findings have corresponding fix branches on the fork, each compiling under all 7 `_WIZCHIP_` values.
- **SC-002**: Each fix branch contains a single commit with changes limited to the scope of its AUD finding.
- **SC-003**: Each fix passes the verification criteria listed in its TODO.md AUD entry.
- **SC-004**: Fault-injected polling loops (stuck MISO, held reset, suppressed Sn_CR clearing) return within bounded deadlines rather than hanging (compile-level verification: iteration counters and deadline returns are present in code; runtime fault injection requires board-level testing not in scope).
- **SC-005**: A documented concurrency model exists for the library (single-task assumption stated, shared-state access risks identified). Concurrent-access stress testing (1M operations) is deferred to a dedicated testing feature requiring RTOS integration.
- **SC-006**: 16-bit register accessors show no torn values under ISR stress testing.
- **SC-007**: Each fix branch applies cleanly to `fork/master` without depending on changes from other P1 or pending P0 PRs.

## Assumptions

- The fork `occamsshavingkit/ioLibrary_Driver` exists and is synced with upstream master at the audit commit.
- P0 fixes (AUD-001 through AUD-005) are in separate pending PRs and do not block P1 work.
- AUD-011 (per-socket concurrency) and AUD-012 (interrupt ownership) are design-level changes that may require architectural decisions beyond the scope of individual fixes. Minimum viable versions are acceptable (e.g., documenting the single-task assumption).
- AUD-037 is confirmed dead code per fourth-pass analysis. The unreachable block at lines 582-596 is removed.
- AUD-040 and AUD-041 share a remediation mechanism (single-VDM 16-bit accessors) and may be combined into one PR if they touch the same macros.
- Hardware-in-loop verification for timing-sensitive fixes (AUD-008, AUD-039) requires board-level testing that is documented but not executed as part of this feature.
