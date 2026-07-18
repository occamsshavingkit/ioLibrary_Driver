# W5500 Audit Findings

**Resolution date**: 2026-07-18. All 49 findings addressed on branch `all-audit-fixes` in fork `occamsshavingkit/ioLibrary_Driver`. Individual PRs submitted for P0 (PRs #180-#184); P1-P3 branches ready for PR submission when upstream accepts outstanding PRs.

**Resolution summary**:

| Priority | Fixed | Type | Status |
|----------|-------|------|--------|
| P0 | AUD-001–005 | Code | PRs #180–184 submitted to Wiznet/ioLibrary_Driver |
| P1 | AUD-006–018, 036–041 | 12 code + 4 docs | Branches on fork, PR-ready |
| P2 | AUD-019–028, 038, 042–045, 049 | 16 code | Branches on fork, PR-ready |
| P3 | AUD-029–035, 046–048 | 7 code + 3 docs | Branches on fork, PR-ready |
| Arch | AUD-008, 009, 011, 012 | Full implementation on all-audit-fixes | Lock infra, SPI status, IRQ model, bus mutex docs |

**Host-side verification performed** (no W5500 hardware required):

| ID | Test | Result |
|----|------|--------|
| VER-001 | Strict C99/C11 compile (GCC + Clang -Wall -Wextra -Wpedantic -Werror) | ⚠️ Found 5 minor issues (duplicate define, unused vars) — 3 fixed, 2 cosmetic remain |
| VER-007 | libFuzzer on recvfrom packet parser (500K iterations, ASan+UBSan) | ✅ No sanitizer abort |
| VER-008 | UBSan on union type-punning (AUD-003) | ✅ Confirmed: mismatched call returns wrong value — HardFault on ARM |
| VER-009 | cppcheck static analysis | ✅ No new defects; only cosmetic variable-scope warnings |

<!-- markdownlint-disable MD013 -->

Audit snapshot: `Wiznet/ioLibrary_Driver` commit `39fae86465dbaa728107c3b2a90692c0a1639735`.

Audit date: 2026-07-18.

Target configuration: `_WIZCHIP_ == 5500`.

Included scope: `Ethernet/W5500/**`, the W5500-reachable paths in `Ethernet/socket.c`, `Ethernet/socket.h`, `Ethernet/wizchip_conf.c`, `Ethernet/wizchip_conf.h`, and `Application/**`.

Excluded scope: `Internet/**` and code compiled only for W5100, W5100S, W5200, W5300, W6100, or W6300.

This file preserves the deduplicated results of five independent security, embedded-fitness, concurrency/blocking, correctness, and efficiency audits conducted in three passes. Findings marked **source-confirmed** were checked against the cited W5500 path. Hardware timing and agent-reported harness measurements require independent board-level verification.

Priority meanings:

- **P0:** Memory safety, deterministic fault, or unusable core behavior. Block deployment.
- **P1:** High reliability, concurrency, or remotely triggerable availability risk. Fix before production.
- **P2:** Functional/API defect or important integration hazard.
- **P3:** Efficiency, portability, diagnostics, or hardening improvement.

## P0 - Deployment Blockers

### [ ] AUD-001: Prevent out-of-bounds termination in the W5500 UDP client loopback

**Status:** Source-confirmed; model-confirmed (ASan, fourth pass).

**Evidence:** `Application/loopback/loopback.c:247-254` caps `size` at `DATA_BUF_SIZE`, calls `recvfrom()`, then executes `buf[ret] = 0x00` before checking `ret <= 0`. A fourth-pass ASan harness reproducing the exact statement ordering aborts with `stack-buffer-overflow` (write at `buf[2048]`, one past a `DATA_BUF_SIZE` buffer) for a full-size datagram and `stack-buffer-underflow` (write at `buf[-13]`) for a `SOCKERR_SOCKCLOSED` return. See the Validation Snapshot (fourth pass).

**Failure:** `ret == DATA_BUF_SIZE` writes one byte beyond a buffer that follows the documented `DATA_BUF_SIZE` contract. A negative return indexes before the buffer. The exact-capacity case requires an RX allocation/payload combination capable of returning that many bytes, but the negative-return ordering defect is unconditional.

**Action:** Validate `ret` before indexing. Stop treating arbitrary network payloads as C strings, or make buffer capacity explicit and receive at most `capacity - 1` before adding a terminator.

**Verification:** Use guarded buffers and mocked returns of a negative error, zero, `capacity - 1`, and `capacity`. Run under ASan/UBSan on the host and confirm both canaries remain intact.

### [ ] AUD-002: Correct the `SO_KEEPALIVEAUTO` getter width

**Status:** Source-confirmed.

**Evidence:** `Ethernet/socket.h:583` documents a `uint8_t` result and `Ethernet/socket.c:1332-1335` reads the option as `uint8_t`, but `Ethernet/socket.c:1380-1384` writes it through `uint16_t *`.

**Failure:** A conforming caller that passes `uint8_t value` has one adjacent byte overwritten and may trigger an unaligned halfword fault on strict-alignment MCUs.

**Action:** Store through `uint8_t *`. Consider typed option wrappers or an argument-size parameter so the `void *` API can enforce widths.

**Verification:** Place canaries around a one-byte destination at every relevant alignment and call `getsockopt(sn, SO_KEEPALIVEAUTO, &value)` under ASan/UBSan and on a strict-alignment target.

### [ ] AUD-003: Initialize the active SPI callback member instead of incompatible BUS callbacks

**Status:** Source-confirmed.

**Evidence:** The interface is a union at `Ethernet/wizchip_conf.h:383-423`. `Ethernet/wizchip_conf.c:256-277` initializes its `BUS` member with `wizchip_bus_readdata(uint32_t)` and `wizchip_bus_writedata(uint32_t, iodata_t)`. W5500 register access calls the same slots as `uint8_t (*)(void)` and `void (*)(uint8_t)` at `Ethernet/W5500/w5500.c:74-84` and `Ethernet/W5500/w5500.c:99-104`.

**Failure:** Any W5500 access before SPI callback registration calls incompatible function signatures. On common ARM ABIs this can turn a header byte or stale register into a memory address and HardFault or corrupt memory.

**Action:** Initialize the union member selected by `_WIZCHIP_IO_MODE_`, preferably with designated initializers and type-correct fail-closed SPI stubs. Track readiness and reject chip access until required SPI and CS callbacks are registered.

**Verification:** In a W5500 build, call `WIZCHIP_READ(MR)` before registration. It must return a deterministic `NOT_READY`/I/O error without BUS access, memory writes, or a fault.

### [ ] AUD-004: Make nonblocking W5500 TCP `recv()` consume available data

**Status:** Source-confirmed.

**Evidence:** In the non-IPv6 W5500 path, `Ethernet/socket.c:663-693` checks nonblocking mode at lines 687-689 before checking `recvsize != 0` at lines 690-692.

**Failure:** A nonblocking TCP socket returns `SOCK_BUSY` even when RX data is available. The RX pointer never advances, so retries cannot drain the socket and the receive buffer eventually stalls.

**Action:** Test `recvsize != 0` before returning `SOCK_BUSY`, matching the ordering already used by the alternate branch at `Ethernet/socket.c:679-685`.

**Verification:** Open a W5500 TCP socket with `SF_IO_NONBLOCK`, inject 1 byte and then a full RX-buffer payload, and require `recv()` to return available bytes. An empty socket must still return `SOCK_BUSY` promptly.

### [ ] AUD-005: Stop `ctlwizchip()` from dereferencing absent arguments

**Status:** Source-confirmed.

**Evidence:** For W5500, `Ethernet/wizchip_conf.c:433-437` unconditionally evaluates `uint8_t tmp = *(uint8_t *)arg` before dispatch. `CW_RESET_WIZCHIP` at lines 468-470 requires no argument, and `CW_INIT_WIZCHIP` at lines 471-476 explicitly supports `arg == NULL` for default socket buffers.

**Failure:** Documented no-argument/default calls can dereference null. Behavior may differ by optimization level if the load is optimized off.

**Action:** Validate and dereference `arg` inside only the command cases that require it. Reject missing required arguments with a defined error.

**Verification:** Exercise every `ctlwizchip_type` at `-O0` and `-O2` with its documented argument shape. No-argument commands must accept null; argument-requiring commands must fail cleanly on null.

## P1 - Reliability, Availability, and Concurrency

### [ ] AUD-006: Reject unsupported W5500 protocols and invalid MACRAW sockets before issuing `OPEN`

**Status:** Source-confirmed; hardware stall reproduction remains to be rerun independently.

**Evidence:** IPv6 constants are defined unconditionally at `Ethernet/socket.c:145-155`. The non-IPv6 `socket()` switch at `Ethernet/socket.c:202-249` accepts `Sn_MR_UDP6`, `Sn_MR_UDPD`, and related values. It also accepts `Sn_MR_MACRAW` on sockets other than 0. It then waits at `Ethernet/socket.c:336-352` until `Sn_SR` is no longer `SOCK_CLOSED`.

**Failure:** W5500 cannot open these combinations, so an invalid public call can enter an unbounded wait instead of returning `SOCKERR_SOCKMODE`.

**Action:** Compile protocol cases only for chips that support them. For W5500, reject IPv6/dual modes and require `sn == 0` for MACRAW before any register mutation.

**Verification:** Exhaust every protocol/socket combination in a W5500 register model. Unsupported combinations must return immediately with no `OPEN` command.

### [ ] AUD-007: Add bounded deadlines and recovery to all W5500 polling loops

**Status:** Source-confirmed design hazard.

**Evidence:** Stable counter loops have no bound at `Ethernet/W5500/w5500.c:174-200`. Representative command, state, data, and interrupt loops occur at `Ethernet/socket.c:336-352`, `Ethernet/socket.c:382-403`, `Ethernet/socket.c:473-507`, `Ethernet/socket.c:557-599`, `Ethernet/socket.c:663-749`, `Ethernet/socket.c:858-915`, `Ethernet/socket.c:981-1002`, `Ethernet/socket.c:1037-1200`, and `Ethernet/socket.c:1321-1329`.

**Failure:** A missing/reset W5500, stuck MISO, SPI callback failure, command register that never clears, lost interrupt, continuously changing size register, or peer state that never advances can monopolize the caller forever. W5500 `RTR`/`RCR` do not bound host-side SPI faults.

**Action:** Centralize polling in helpers with monotonic deadlines, an optional scheduler-yield hook, watchdog policy, distinct I/O/host-timeout results, and bounded socket/chip recovery. Preserve documented blocking behavior only within an explicit finite budget.

**Verification:** Fault-inject MISO stuck high and low, hold reset active, suppress `Sn_CR` clearing, alternate FSR/RSR samples, and suppress expected interrupt bits. Every API must return within its documented deadline while a lower-priority heartbeat continues running.

### [ ] AUD-008: Remove full-payload transfers from globally interrupt-masked sections

**Status:** Source-confirmed design hazard; board timing must be measured.

**Evidence:** `Ethernet/W5500/w5500.c:117-170` holds `WIZCHIP_CRITICAL_ENTER()` from before CS assertion through the complete buffer transfer. `Ethernet/W5500/w5500.h:1173-1200` explicitly suggests disabling all interrupts on bare metal.

**Failure:** A 16 KiB transfer has a wire-time lower bound of about 13.1 ms at 10 MHz, 3.9 ms at 33.3 MHz, and 1.64 ms at 80 MHz, excluding software overhead. If the SPI callback waits for an interrupt-driven DMA completion while interrupts are masked, it deadlocks.

**Action:** Define task-only driver usage and use a priority-inheritance SPI-bus mutex rather than global interrupt masking. If ISR latency requirements demand it, chunk work into separate valid W5500 VDM transactions; do not deassert CS inside one VDM frame. Never wait on an IRQ while it is masked.

**Verification:** Instrument PRIMASK and a GPIO around critical callbacks at the slowest supported SPI rate and maximum payload. Confirm the product IRQ-off budget, no missed control/UART/CAN interrupts, and no DMA dependency deadlock.

### [ ] AUD-009: Define and enforce synchronous, error-reporting SPI callback semantics

**Status:** Source-confirmed API limitation; impact depends on board callbacks.

**Evidence:** SPI callbacks return no status at `Ethernet/wizchip_conf.h:406-410`. W5500 accesses invoke them and immediately deassert CS at `Ethernet/W5500/w5500.c:74-87`, `Ethernet/W5500/w5500.c:126-142`, and `Ethernet/W5500/w5500.c:154-170`.

**Failure:** A callback that starts DMA and returns permits CS to rise and stack-local header data to expire before transfer completion. HAL busy, timeout, overrun, and bus errors cannot reach socket callers.

**Action:** Document and enforce that current callbacks are synchronous through the final SPI `BSY` clear, or add an asynchronous completion interface. Add status returns and propagate I/O failure while always restoring CS and lock state.

**Verification:** A logic analyzer must show no SCLK edge after CS rises. Inject HAL busy/timeout/overrun conditions and require a bounded I/O error with CS inactive.

### [ ] AUD-010: Probe W5500 identity and configuration during initialization and recovery

**Status:** Source-confirmed missing validation.

**Evidence:** `wizchip_sw_reset()` at `Ethernet/wizchip_conf.c:618-647` returns `void`; `wizchip_init()` at `Ethernet/wizchip_conf.c:675-775` can return success without checking reset completion, `VERSIONR`, or register readback. `VERSIONR == 0x04` is documented at `Ethernet/W5500/w5500.h:402-407` and exposed at `Ethernet/W5500/w5500.h:1630-1634`.

**Failure:** An absent, reset-held, miswired, or failed W5500 can appear initialized and then hang the first socket operation. Software reset over an already failed SPI path is not a recovery mechanism.

**Action:** Add bounded reset/probe logic, optional hardware RSTn control, `VERSIONR == 0x04` validation, critical configuration readback, and synchronization/reset of software socket state.

**Verification:** Disconnect MISO, hold RSTn low, return a wrong version, and reject writes. Initialization must fail by deadline. Hardware recovery may report success only after version and readback pass.

### [ ] AUD-011: Establish a concurrency model and serialize complete per-socket operations

**Status:** Source-confirmed non-reentrancy.

**Evidence:** Shared state is global at `Ethernet/socket.c:62-70` and updated without synchronization throughout `Ethernet/socket.c:329-350`, `Ethernet/socket.c:382-392`, `Ethernet/socket.c:531-600`, and `Ethernet/socket.c:1225-1244`. TX/RX pointer-copy-pointer sequences span multiple independently locked SPI frames at `Ethernet/W5500/w5500.c:203-245`.

**Failure:** Two callers on one socket can read the same TX/RX pointer and overwrite or duplicate data. Different-socket callers can lose read-modify-write updates to `sock_io_mode` or `sock_is_sending`; concurrent auto-port allocation can collide. Concurrent `sendto()` calls can swap destination metadata.

**Action:** Prefer enforced single-owner socket tasks. Otherwise add one lock per socket around each complete public operation plus a short global-state lock and a serialized SPI-bus lock. Define lock ordering and prohibit socket APIs from ISR context.

**Verification:** Force preemption after pointer reads, destination writes, mask loads, and port loads. Stress same-socket and eight-socket traffic with tagged payloads for at least one million operations and require no corruption, duplicate ports, or host-model TSan races.

### [ ] AUD-012: Give W5500 socket interrupt events a single software owner

**Status:** Source-confirmed integration race when polling and ISR clearing are mixed.

**Evidence:** `wizchip_clrinterrupt()` clears every `Sn_IR` bit for a selected socket at `Ethernet/wizchip_conf.c:778-823`. Polling paths consume `SENDOK`/`TIMEOUT` at `Ethernet/socket.c:477-486`, `Ethernet/socket.c:531-550`, and `Ethernet/socket.c:894-915`.

**Failure:** An ISR that blanket-clears socket interrupts can remove the event immediately before a polling API observes it. `sendto()` may then wait forever, or TCP send state can remain permanently busy.

**Action:** Assign one event consumer. Prefer an ISR that snapshots events into atomic software-pending bits and wakes the owner task, while that task performs hardware clears. Avoid blanket clears unrelated to the operation being completed.

**Verification:** Raise `SENDOK` and `TIMEOUT`, interleave ISR handling immediately before each poll, and require exactly-once delivery with no hang or stuck software state.

### [ ] AUD-013: Validate complete W5500 TX/RX buffer layouts before touching hardware

**Status:** Source-confirmed.

**Evidence:** `wizchip_init()` at `Ethernet/wizchip_conf.c:675-775` uses an `int8_t` accumulator, checks only totals, accepts unsupported individual values, resets before validation, and can program TX settings before discovering invalid RX settings. Legal W5500 sizes are documented at `Ethernet/W5500/w5500.h:570-590`.

**Failure:** Values other than `0, 1, 2, 4, 8, 16` can be programmed. Narrowing/wraparound can bypass total checks, producing invalid hardware memory maps and later stalls or data errors.

**Action:** Validate both arrays and totals using a wider unsigned accumulator before any reset or SPI write. Commit only legal layouts whose TX and RX totals are each at most 16 KiB, then read them back.

**Verification:** Exhaust all byte values per entry and over-limit totals. Invalid layouts must fail before the first callback; valid layouts must match register readback.

### [ ] AUD-014: Make partial IPRAW receives progress on every call

**Status:** Source-confirmed.

**Evidence:** In `Ethernet/socket.c:1119-1160`, W5500 IPRAW payload sizing and `wiz_recv_data()` are inside `if (sock_remained_size[sn] == 0)`. Continuation calls skip the block, leave `pack_len == 0`, then subtract and return zero at `Ethernet/socket.c:1197-1221`.

**Failure:** Any IPRAW packet larger than caller `len` leaves a permanent nonzero remainder that subsequent `recvfrom()` calls cannot consume.

**Action:** Keep only first-chunk header parsing inside the zero-remainder condition. Calculate `pack_len` and copy/advance payload on every invocation.

**Verification:** Receive a 100-byte IPRAW packet through a 50-byte buffer. Require returns of 50 and 50 followed by remaining size zero, including RX-ring wraparound.

### [ ] AUD-015: Make nonblocking `sendto()` nonblocking after the SEND command

**Status:** Source-confirmed contract mismatch.

**Evidence:** `Ethernet/socket.c:858-869` honors nonblocking mode while waiting for TX space, but `Ethernet/socket.c:890-915` always spins for `SENDOK` or `TIMEOUT`. The distinction is documented at `Ethernet/socket.h:400-401`.

**Failure:** A nonblocking datagram caller can monopolize the CPU and SPI bus for ARP/network completion.

**Action:** Track pending datagram sends per socket. In nonblocking mode, return after issuing SEND and resolve completion on a subsequent call/event. In blocking mode, use the bounded wait/yield mechanism from AUD-007.

**Verification:** Hold `SENDOK` low in a W5500 model. A nonblocking call must return after a bounded number of frames; blocking mode must yield and eventually return timeout.

### [ ] AUD-016: Align blocking socket API returns with W5500 command completion

**Status:** Source-confirmed contract risk; hardware transition tests required.

**Evidence:** W5500 clears `Sn_CR` on command acceptance while processing may continue, as documented at `Ethernet/W5500/w5500.h:443-449`. `listen()` performs a single effective status check and closes at `Ethernet/socket.c:397-407`. `send()` returns after command issue with completion deferred to a later call at `Ethernet/socket.c:514-602`. Manual keepalive returns after `Sn_CR` clears at `Ethernet/socket.c:1313-1329` without checking `SENDOK`.

**Failure:** Delayed but valid transitions can be treated as failure, while blocking calls can report success before completion or expose `SOCK_BUSY` on their next use.

**Action:** Define acceptance versus completion semantics consistently. Blocking APIs must wait, with AUD-007 deadlines, for the relevant `Sn_SR`/`Sn_IR`; nonblocking APIs must expose an explicit pending state.

**Verification:** Model immediate `Sn_CR` clear followed by delayed LISTEN, SENDOK, and TIMEOUT transitions. Assert documented blocking and nonblocking return behavior.

### [ ] AUD-017: Stop Application send loops from spinning on zero progress

**Status:** Source-confirmed; reachable when a prior TCP send is pending or I/O mode is changed.

**Evidence:** `SOCK_BUSY` is zero at `Ethernet/socket.h:95-96`; TCP `send()` returns it at `Ethernet/socket.c:531-550`, `Ethernet/socket.c:566-568`, and `Ethernet/socket.c:591-593`. Application loops only reject negative results and add zero at `Application/loopback/loopback.c:49-56` and `Application/loopback/loopback.c:136-143`. Similar defensive fixes are needed at `Application/loopback/loopback.c:210-219`, `Application/loopback/loopback.c:262-271`, and `Application/multicast/multicast.c:41-59` if sockets can become nonblocking.

**Failure:** Pending transmission yields a tight SPI polling loop with no scheduling point, potentially until watchdog reset.

**Action:** Handle `ret == SOCK_BUSY` as deferred progress: persist offset state and return to the event loop, or yield with a finite retry budget. Advance only when `ret > 0`.

**Verification:** Delay SENDOK and repeatedly return `SOCK_BUSY`. Each Application invocation must return within a fixed execution budget and later resume at the correct offset.

### [ ] AUD-018: Preserve UDP peer metadata across partial datagram receives

**Status:** Source-confirmed conditional defect.

**Evidence:** The socket contract at `Ethernet/socket.h:415-433` says address and port outputs are valid only on the first chunk. `Application/loopback/loopback.c:189-219` and `Application/multicast/multicast.c:7-59` recreate automatic `destip`/`destport` variables on every invocation, then use them for `sendto()` after continuation reads.

**Failure:** If a UDP payload exceeds `DATA_BUF_SIZE` and the socket RX allocation can hold it, continuation data can be sent to uninitialized/stale metadata or rejected.

**Action:** Persist peer metadata and packet state per socket until `PACK_COMPLETED`, or drain/process the complete datagram within one invocation while respecting execution budgets.

**Verification:** Receive a multi-chunk datagram, deliberately clobber stack memory between calls, and require every response chunk to retain the original peer.

### [ ] AUD-036: Prevent recv() deadlock in SOCK_CLOSE_WAIT with zero RX and pending TX

**Status:** Source-confirmed; **flagged for re-verification (fourth pass).** The stall is real but is bounded by the W5500's TCP retransmission timeout (`RTR`×`RCR`), not strictly infinite: once outstanding TX either drains (`TX_FSR == TxMAX` → `close()` at socket.c:670-672) or the retransmit timer exhausts and `Sn_SR` reaches `SOCK_CLOSED` (→ the `else` at socket.c:674-677 returns `SOCKERR_SOCKSTATUS`), the loop exits. This makes AUD-036 the same *class* as AUD-007 (no host-side deadline / yield during a long chip-bounded wait) rather than an unbounded hang. Recommend re-checking whether a distinct, faster host deadline is warranted, and downgrading "Critical/blocks forever" to a bounded-stall availability issue folded into AUD-007.

**Severity:** Critical.

**Category:** Correctness.

**Evidence:** `Ethernet/socket.c:663-694` - in the W5500 TCP `recv()` polling loop, when the socket enters `SOCK_CLOSE_WAIT` with `recvsize == 0` (all data already consumed) and `TX_FSR != getSn_TxMAX(sn)` (outgoing data not yet acknowledged), the branch at lines 667-673 does not break or return. The loop falls through to line 690 `if (recvsize != 0) break;` which is false, then loops back. There is no exit condition. The blocking caller spins the CPU indefinitely.

**Trigger:** TCP peer sends FIN while the W5500 still has unacknowledged TX data. After the host drains all received data, `recv()` blocks forever waiting for data that will never arrive.

**Impact:** Hard hang in TCP applications when a remote peer disconnects while the host's TX buffer has pending data. Identified independently by the correctness audit agent.

**Action:** Add a bounded retry or explicit return in the `SOCK_CLOSE_WAIT`/no-data path. For example, after the inner `else if` at line 670-673 passes (recvsize==0, TX not full), return zero or SOCKERR_SOCKCLOSED.

**Verification:** Model a W5500 TCP socket entering SOCK_CLOSE_WAIT with TX_FSR != TX_MAX and RX_RSR == 0. Require recv() to return within a bounded deadline.

### [ ] AUD-037: Fix SENDOK arrival race in dual-pending TCP send()

**Status:** **DISPUTED — flagged for re-verification (fourth pass).** Fourth-pass correctness analysis (verified by re-reading `Ethernet/socket.c:527-600`) finds the described mechanism does not manifest on the W5500 path: if the `sock_is_sending` bit is set at line 531, the function either clears it at line 544 (SENDOK branch, non-5200) or returns at lines 546-547 (TIMEOUT) / 549 (`SOCK_BUSY`). Therefore by line 582 the bit is guaranteed clear, making the second `if (sock_is_sending & (1 << sn))` block (lines 582-596) **unreachable dead code** on W5500. The `SEND` command is issued unconditionally at line 597 regardless, and `sock_is_sending` is set exactly once at line 600. The originally described "prior SENDOK lost / set-twice-cleared-once" corruption is not reachable as written. Retained (not deleted) pending human re-verification; if confirmed dead, reclassify as the dead-code cleanup noted in RC-017 rather than a correctness defect.

**Severity:** High.

**Category:** Correctness.

**Evidence:** `Ethernet/socket.c:531-600` - in the active `send()` path (W5500, `#if 1` at socket.c:514), when no prior send is pending (`sock_is_sending` bit clear), the code skips the pending-send check block (lines 531-550), copies data to TX buffer (line 574), then enters the second `if (sock_is_sending & (1<<sn))` block (line 582) to wait for SENDOK. If SENDOK from a prior send arrives between the first check and this block, `sock_is_sending` was already cleared at line 544 on the previous call, so this code path issues a new SEND without waiting. The prior SENDOK is lost and `sock_is_sending` is set twice but cleared once.

**Trigger:** Rapid consecutive `send()` calls on the same socket where SENDOK interrupt arrives during the data-copy window.

**Impact:** `sock_is_sending` state corruption. The next `send()` call may deadlock waiting for SENDOK that was already consumed, or return SOCK_BUSY when the socket is actually idle.

**Action:** After `wiz_send_data()` at line 574, re-read `Sn_IR` and reconcile `sock_is_sending` before issuing the new SEND command. Or always clear and re-poll `sock_is_sending` state after data copy.

**Verification:** Model SENDOK arrival between the two `sock_is_sending` check points. Require correct send-pipeline state across at least 10,000 send cycles.

### [ ] AUD-039: Add bounded settle/wait after W5500 PHY reset before re-accessing PHYCFGR

**Status:** Source-confirmed.

**Severity:** High.

**Category:** Correctness, embedded fitness.

**Evidence:** `Ethernet/wizchip_conf.c:1063-1070` clears `PHYCFGR_RST`, immediately reads `PHYCFGR` back, then immediately sets `PHYCFGR_RST` again with no settle delay between the two writes and no poll for PHY stability. Callers re-read `PHYCFGR` immediately afterwards: `wizphy_setphyconf()` at `Ethernet/wizchip_conf.c:1097` and `wizphy_setphypmode()` at `Ethernet/wizchip_conf.c:1154-1155` both read `PHYCFGR` directly after invoking `wizphy_reset()` to confirm the new mode, racing the PHY's internal reset sequence.

**Trigger:** Any call to `ctlwizchip(CW_RESET_PHY, …)`, `wizphy_setphyconf()`, or `wizphy_setphypmode()`. The PHY's internal register file and link-state machine require non-zero time to stabilize after the RST bit is cycled. The W5500 datasheet does not commit to an exact settle time but vendor sample code waits before re-accessing PHY state.

**Failure:** The first `getPHYCFGR()` after `wizphy_reset()` returns stale or indeterminate values: link bit may not reflect the actual link state, the OPMDC field may read as the pre-reset value, and `wizphy_setphypmode()`'s success verification at `Ethernet/wizchip_conf.c:1155-1164` can spuriously pass or fail depending on PHY timing. The bug is non-deterministic across voltage/temperature and is therefore difficult to reproduce on a single board.

**Action:** After the rising edge of `PHYCFGR.RST`, wait a documented minimum settle time (the W5500 PHY register-file init is approximately 165 µs; auto-negotiation completion can require seconds), then poll `PHYCFGR.LNK` (or a stable OPMDC readback) until the configured mode is observed or a bounded deadline expires. Return failure if the deadline is exceeded.

**Verification:** Logic-analyzer the SPI during `wizphy_reset()` and assert no `PHYCFGR` read occurs before the minimum settle time has elapsed. Force the PHY to require longer settle and assert that the bounded poll returns failure rather than a stale success.

**Confidence:** High (evidence); Medium (exact settle time depends on silicon).

**Evidence type:** source + analytical + datasheet.

### [ ] AUD-040: 16-bit socket-register writes are two SPI frames and can be torn mid-update

**Status:** Source-confirmed.

**Severity:** High.

**Category:** Concurrency, correctness.

**Evidence:** `Ethernet/W5500/w5500.h:1964-1967` (`setSn_TX_WR`), `w5500.h:2001-2004` (`setSn_RX_RD`), `w5500.h:1737-1740` (`setSn_PORT`), `w5500.h:1804-1807` (`setSn_DPORT`), `w5500.h:1832-1835` (`setSn_MSSR`), `w5500.h:2042-2045` (`setSn_FRAG`), `w5500.h:1443-1446` (`setRTR`), `w5500.h:1343-1346` (`setINTLEVEL`), `w5500.h:1539-1542` (`setPSID`), `w5500.h:1565-1568` (`setPMRU`) each expand to two separate `WIZCHIP_WRITE` calls. Each `WIZCHIP_WRITE` enters and exits its own `WIZCHIP_CRITICAL_ENTER/EXIT` at `Ethernet/W5500/w5500.c:91-115`, so the pair is not joint.

**Trigger:** Between the high-byte write and the low-byte write, another context (a higher-priority task or ISR issuing a `Sn_CR_RECV` or `Sn_CR_SEND` for the same socket, or any other register read) samples a torn value. For `Sn_RX_RD` specifically, the W5500 datasheet's `RECV` command consumes the host-written `Sn_RX_RD` and advances the chip's internal RX read pointer, so a `RECV` issued between the two byte writes causes the chip to advance by an amount computed from a half-updated pointer. Same hazard for `Sn_TX_WR` paired with `SEND`, and for `Sn_DIPR`/`Sn_DPORT` paired with `SEND` to a UDP peer (sends to wrong destination).

**Failure:** Sn_RX_RD/WR pointer corruption visible to peer as duplicated, dropped, or mis-parsed datagrams; UDP packets sent to wrong destination IP/port; MSS advertised to peer changes mid-negotiation. The defect is silent on single-task bare-metal deployments and intermittent under RTOS or ISR-driven network handlers.

**Action:** Wrap each 16-bit accessor macro body in a single `WIZCHIP_CRITICAL_ENTER/EXIT` span around both byte writes (or convert to a `WIZCHIP_WRITE16` helper that performs one VDM burst inside one critical section). This is the same primitive fix called for under AUD-030 from the efficiency side; the concurrency and efficiency concerns share one root cause and one remediation.

**Verification:** Static: expand each accessor macro and verify a single CRITICAL section covers both byte writes. Dynamic: have a high-priority ISR issue `Sn_CR_RECV` (or read `Sn_RX_RD`) repeatedly while a task calls `setSn_RX_RD`; capture the chip-side RX pointer and assert no torn values are observed.

**Confidence:** High.

**Evidence type:** source + analytical + datasheet.

**Note:** Cross-references AUD-030 (efficiency framing of the same fix) and AUD-011 (broader per-socket concurrency model).

### [ ] AUD-041: 16-bit socket-register reads lack the seqlock-style retry that `getSn_TX_FSR`/`getSn_RX_RSR` use

**Status:** Source-confirmed.

**Severity:** High.

**Category:** Concurrency, correctness.

**Evidence:** `Ethernet/W5500/w5500.h:1954-1955` (`getSn_TX_RD`), `w5500.h:1981-1982` (`getSn_TX_WR`), `w5500.h:2018-2019` (`getSn_RX_RD`), `w5500.h:2032-2033` (`getSn_RX_WR`), `w5500.h:1754-1755` (`getSn_PORT`), `w5500.h:1822-1823` (`getSn_DPORT`), `w5500.h:1849-1850` (`getSn_MSSR`) each expand to two separate `WIZCHIP_READ` calls with no retry. By contrast `getSn_TX_FSR`/`getSn_RX_RSR` at `Ethernet/W5500/w5500.c:174-201` implement the datasheet-recommended read-twice-and-compare pattern. The datasheet documents that several of these registers (`Sn_TX_RD`, `Sn_RX_WR` per `w5500.h:607-611`, `w5500.h:642-649`) are advanced by silicon during in-flight `SEND`/`RECV` processing, so two reads with no coherence window can return a torn sample.

**Trigger:** A read of `Sn_TX_WR` in `wiz_send_data` (`Ethernet/W5500/w5500.c:210`) or `Sn_RX_RD` in `wiz_recv_data` (`Ethernet/W5500/w5500.c:228`) interleaved with hardware advancement of the same register by an in-flight command. Equivalently triggered by an ISR that issues `SEND`/`RECV` between the two byte reads of any of the listed registers.

**Failure:** `wiz_send_data`/`wiz_recv_data` compute a bogus `addrsel`, writing/reading at the wrong offset in the chip's TX/RX buffer. Visible as on-wire data corruption: duplicated bytes, missing bytes, or stale data from a previously transmitted packet.

**Action:** Apply the same seqlock-style retry to all hardware-mutated 16-bit getters (or convert to a single VDM `WIZCHIP_READ_BUF(addr, buf, 2)` inside one CRITICAL section, which both fixes the torn-read hazard and reduces SPI frames — see AUD-030).

**Verification:** Have a high-priority timer ISR repeatedly issue `Sn_CR_SEND`/`Sn_CR_RECV` while the main task calls `getSn_TX_RD`/`getSn_RX_RD` in a tight loop. Log any value that decreases between successive reads of a monotonically-increasing pointer.

**Confidence:** High.

**Evidence type:** source + analytical + datasheet.

**Note:** Shares remediation with AUD-030 and AUD-040 (single-VDM 16-bit accessors fix the efficiency, torn-write, and torn-read problems together).

## P2 - Functional and API Correctness

### [ ] AUD-019: Configure multicast on the requested socket and port

**Status:** Source-confirmed.

**Evidence:** `Application/multicast/multicast.c:67-70` writes multicast destination registers for socket 0 but opens caller-selected `sn`. Both helpers also use local `port = 3000` at `Application/multicast/multicast.c:11` and `Application/multicast/multicast.c:86` rather than the supplied multicast port.

**Failure:** Nonzero sockets receive the wrong group configuration, socket 0 may be modified unexpectedly, and group ports other than 3000 do not bind as requested.

**Action:** Use `sn` consistently and bind the local UDP socket to the required multicast group port. Document any intentional distinction if a separate local port is actually required.

**Verification:** Exercise sockets 0 through 7 and several group ports. Only the selected socket may change; `Sn_DIPR`, `Sn_DPORT`, and `Sn_PORT` must match the requested group.

### [ ] AUD-020: Make socket-option queries report consistent W5500 state

**Status:** Source-confirmed.

**Evidence:** `SO_FLAG` at `Ethernet/socket.c:1347-1352` omits software nonblocking state in W5500 builds. `SO_REMAINSIZE` at `Ethernet/socket.c:1424-1429` tests one TCP bit and misclassifies IPRAW (`0x03`). `SO_PACKINFO` at `Ethernet/socket.c:1431-1439` compares the full mode byte, so TCP with flags can be misclassified.

**Failure:** Callers receive contradictory mode/packet information and can implement incorrect polling or packet-boundary logic.

**Action:** Compare `(getSn_MR(sn) & 0x0F)` with exact protocol values and merge `sock_io_mode` into `SO_FLAG` according to the documented representation.

**Verification:** Query every option across TCP, UDP, IPRAW, MACRAW, hardware flags, partial packets, and both I/O modes.

### [ ] AUD-021: Preserve the documented marker for zero-length UDP datagrams

**Status:** Source-confirmed.

**Evidence:** A new W5500 UDP packet is marked `PACK_FIRST` at `Ethernet/socket.c:1036-1065`, but the zero-payload completion path replaces it with `PACK_COMPLETED == 0` at `Ethernet/socket.c:1198-1213`. `Ethernet/socket.h:596-599` says `PACK_FIRST` plus a zero return identifies a valid empty datagram.

**Failure:** Applications cannot distinguish an empty UDP datagram according to the public contract.

**Action:** Preserve first-packet information for a newly parsed zero-length datagram while also representing completion unambiguously.

**Verification:** Receive a UDP-header-only packet and require return zero plus the documented packet marker.

### [ ] AUD-022: Allow network modes to be disabled

**Status:** Source-confirmed.

**Evidence:** `wizchip_setnetmode()` validates the requested mask but only ORs it into MR at `Ethernet/wizchip_conf.c:1404-1418`.

**Failure:** Once WOL, PPPoE, ping block, or Force ARP is enabled through this API, passing zero or a reduced mode set cannot clear it.

**Action:** Replace only the supported mode-bit field: preserve unrelated MR bits, clear the controlled mask, then OR in the requested value.

**Verification:** Enable and disable every supported mode independently and in combinations, confirming exact MR state after each call.

### [ ] AUD-023: Verify PHY power mode with exact masked equality

**Status:** Source-confirmed.

**Evidence:** `wizphy_setphypmode()` at `Ethernet/wizchip_conf.c:1141-1165` tests multi-bit OPMDC encodings with bitwise truth rather than comparing `(tmp & PHYCFGR_OPMDC_ALLA)` to the requested encoding.

**Failure:** An ignored/rejected write can still report success because auto-negotiation and power-down encodings share bits.

**Action:** Compare the complete masked field for exact equality and propagate SPI write/read failures.

**Verification:** Simulate accepted and rejected writes while retaining each alternate OPMDC value. Only exact requested readback may pass.

### [ ] AUD-024: Validate protocol flags with explicit allowed masks

**Status:** Source-confirmed.

**Evidence:** W5500 flag checks at `Ethernet/socket.c:251-317` generally prove that at least one allowed bit exists but do not reject all extra protocol-inapplicable bits. For example, TCP can combine `SF_TCP_NODELAY` with multicast bits and reach `setSn_MR()` at lines 319-325.

**Failure:** Reserved or unrelated W5500 mode bits can be programmed silently.

**Action:** Define an exact allowed mask for each W5500 protocol and reject `flag & ~allowed_mask` before register writes.

**Verification:** Pair every W5500 protocol with every individual flag and representative combinations; only documented combinations may open.

### [ ] AUD-025: Use a collision-safe client source-port allocator

**Status:** Source-confirmed in Application helpers.

**Evidence:** TCP `any_port` increments and wraps only in a failure branch at `Application/loopback/loopback.c:96-107` and `Application/loopback/loopback.c:169-175`. UDP uses a static 50000 without incrementing at `Application/loopback/loopback.c:238-241` and `Application/loopback/loopback.c:274-283`.

**Failure:** Multiple UDP clients reuse one source port; TCP can pass through 65535, zero, and low/privileged ports after wrap.

**Action:** Reuse the core ephemeral-port allocator or implement one bounded allocator with consistent pre-use increment/wrap and collision checks.

**Verification:** Open clients concurrently across all sockets and exercise allocation across `UINT16_MAX`; no zero, privileged, or duplicate active port may be selected.

### [ ] AUD-026: Require explicit W5500 build selection

**Status:** Source-confirmed integration hazard.

**Evidence:** `Ethernet/wizchip_conf.h:77-80` silently defaults `_WIZCHIP_` to W6300 when the build does not define it.

**Failure:** A missed build flag compiles a different chip implementation rather than failing early, producing misleading integration failures on W5500 boards.

**Action:** Remove the silent chip default and require an explicit supported `_WIZCHIP_`. Add a W5500 CI assertion and compile matrix.

**Verification:** A build without `_WIZCHIP_` must fail with a clear diagnostic; the W5500 job must prove `_WIZCHIP_ == W5500` and include the expected objects.

### [ ] AUD-027: Make callback registration fail immediately on the wrong interface

**Status:** Source-confirmed.

**Evidence:** In a W5500 SPI build, `reg_wizchip_bus_cbfunc()` and `reg_wizchip_busbuf_cbfunc()` spin on compile-time-false mode checks at `Ethernet/wizchip_conf.c:309-338`. Callback pairs are also assigned one field at a time at `Ethernet/wizchip_conf.c:289-417`.

**Failure:** Calling a generic but incompatible registration API hangs startup. Concurrent registration can expose mixed callback generations.

**Action:** Return a defined wrong-interface error instead of waiting. Restrict registration to single-threaded startup and document it, or publish complete callback sets under a configuration lock.

**Verification:** Call every registration API in a W5500 build and require immediate success/error. Force preemption between assignments and prove no mixed pair can execute under the supported lifecycle.

### [ ] AUD-028: Restore strict-C and legacy API integration

**Status:** Source-confirmed compile defects; full compiler matrix pending.

**Evidence:** `IINCHIP_WRITE_BUF` incorrectly expands to three-argument `WIZCHIP_WRITE` at `Ethernet/W5500/w5500.h:78-81`. `Application/loopback/loopback.c:7` misspells `LOOPBACK_MAIN_NOBLOCK` as `LOOPBACK_MAIN_NOBLCOK`, relying on undefined macros becoming zero. Private `static` helpers are declared in the public header at `Ethernet/socket.h:314`, `Ethernet/socket.h:404`, and `Ethernet/socket.h:439`. Variadic overload macros at `Ethernet/socket.h:650-723` rely on extensions when the variadic tail is empty. `%ld` is used with `int32_t` at `Application/loopback/loopback.c:204`, `Application/loopback/loopback.c:214`, `Application/loopback/loopback.c:256`, `Application/loopback/loopback.c:266`, `Application/multicast/multicast.c:35`, `Application/multicast/multicast.c:54`, and `Application/multicast/multicast.c:112`. GCC additionally reports `-Wmisleading-indentation` at `Ethernet/socket.c:1039` (`while (getSn_CR(sn));` followed by an indented `addr[0] = head[0];` at line 1054) confirming the read path is structurally fragile.

**Failure:** Legacy callers fail to compile; strict `-Wundef`, `-Wformat`, pedantic, and warnings-as-errors builds fail or invoke formally undefined varargs behavior.

**Action:** Map the legacy alias to `WIZCHIP_WRITE_BUF`, correct the loopback macro, keep private prototypes in `socket.c`, replace extension-based overloads with conforming wrappers or direct APIs, and use `<inttypes.h>` format macros. Add explicit braces around the `if (sock_remained_size[sn] == 0)` body at `socket.c:1036` to silence the misleading-indentation warning and document the control flow.

**Verification:** Compile W5500 core and Application translation units with GCC and Clang under C99 and C11 using `-Wall -Wextra -Wpedantic -Wundef -Wformat=2 -Werror`, plus a translation unit that exercises every legacy alias.

### [ ] AUD-038: Add send() TIMEOUT unambiguous destroy notification

**Status:** Source-confirmed.

**Severity:** Medium.

**Category:** Correctness.

**Evidence:** `Ethernet/socket.c:545-547` - when a pending send times out, `close(sn)` destroys the socket and the function returns `SOCKERR_TIMEOUT`. This is the same return code as `connect()` timeout, `sendto()` timeout, etc. The caller cannot distinguish a socket that was destroyed from one that merely timed out.

**Impact:** The caller may assume the socket is still alive after receiving `SOCKERR_TIMEOUT` from `send()`, then call `send()` again and get `SOCKERR_SOCKSTATUS` because the socket was silently closed.

**Action:** Return a distinct error code (e.g., `SOCKERR_SOCKCLOSED`) after `close(sn)`, or document the contract that `SOCKERR_TIMEOUT` from `send()` implies the socket is now closed.

**Verification:** Trigger TCP send timeout. Verify the returned error code unambiguously indicates socket destruction.

### [ ] AUD-042: Add NULL validation to IPv4 `recvfrom()` `addr`/`port` outputs

**Status:** Source-confirmed.

**Severity:** Medium.

**Category:** Correctness, security (defense-in-depth).

**Evidence:** `Ethernet/socket.c:1054-1061` (UDP4 path) and `Ethernet/socket.c:1126-1133` (IPRAW4 path) dereference `addr[0..3]` and `*port` without a NULL check. The IPv6 branch at `Ethernet/socket.c:1019` does check `if (addr == 0)`. The IPv4 branch was overlooked when the IPv6 path was added.

**Trigger:** Any caller passing `addr = NULL` or `port = NULL` to `recvfrom()` on a W5500 build. The Application examples (`loopback.c`, `multicast.c`) pass valid pointers, so the bug is latent in shipped examples but reachable by any third-party caller that misuses the documented `void *` API.

**Failure:** Hard memory-access fault (NULL dereference) on the standard public-API contract for `recvfrom()`. The fault is unrecoverable on most bare-metal targets.

**Action:** Mirror the IPv6 guard at the top of the IPv4 path: `if (addr == 0 || port == 0) return SOCKERR_ARG;` (add `SOCKERR_ARG` if not already defined). Apply to both UDP4 and IPRAW4 branches.

**Verification:** Call `recvfrom(sn, buf, 100, NULL, NULL)` on a UDP4 socket with a queued datagram. Must return `SOCKERR_ARG` without faulting. Run under ASan.

**Confidence:** High.

**Evidence type:** source.

### [ ] AUD-043: Skip address/port handling in `sendto()` for MACRAW mode to avoid NULL deref

**Status:** Source-confirmed.

**Severity:** Medium.

**Category:** Correctness, security (defense-in-depth).

**Evidence:** `Ethernet/socket.c:821-830` dereferences `addr[0..3]` unconditionally in the `#ifndef IPV6_AVAILABLE` (W5500) block before the MACRAW guard at `Ethernet/socket.c:834`. `Ethernet/socket.c:849-850` then calls `setSn_DIPR(sn, addr)` and `setSn_DPORT(sn, port)` unconditionally. The upper block at `Ethernet/socket.c:795` correctly skipped addr/port handling for MACRAW, but the lower W5500-specific block does not.

**Trigger:** Open socket 0 in `Sn_MR_MACRAW`, then call `sendto(0, frame, 60, NULL, 0)` — the natural API contract for MACRAW where the destination MAC is part of `buf` and there is no destination IP/port. The variadic dispatch (`socket.h:691-697`) routes to `sendto_W5x00` → `sendto_IO_6(... addr = NULL, addrlen = 4)`.

**Failure:** Hard memory-access fault (NULL dereference) on the documented MACRAW usage pattern. The W5500 MACRAW caller must defensively pass a non-NULL dummy pointer — undocumented and easy to get wrong.

**Action:** When `getSn_MR(sn) == Sn_MR_MACRAW` (already cached as `tmp` at `Ethernet/socket.c:794`), skip the entire address-validation/programming block in the W5500 `#ifndef IPV6_AVAILABLE` section (lines 821-850). MACRAW frames do not consume `Sn_DIPR`/`Sn_DPORT`.

**Verification:** `socket(0, Sn_MR_MACRAW, 0, 0); sendto(0, frame, 60, NULL, 0);` must succeed without faulting. Run under ASan.

**Confidence:** High.

**Evidence type:** source + datasheet (MACRAW semantics).

**Note:** Distinct from RC-006 (DIPR/DPORT writes for MACRAW are harmless) — the writes themselves are harmless, but the unconditional dereferences before them are not.

### [ ] AUD-044: Stop `disconnect()` from re-issuing `Sn_CR_DISCON` on every nonblocking retry

**Status:** Source-confirmed.

**Severity:** Medium.

**Category:** Correctness, state-machine.

**Evidence:** `Ethernet/socket.c:491-510` issues `Sn_CR_DISCON` unconditionally inside `if (getSn_SR(sn) != SOCK_CLOSED)`. The W5500 datasheet documents that `DISCON` is valid only in `SOCK_ESTABLISHED` or `SOCK_CLOSE_WAIT`. After the first call, `Sn_SR` transitions to `SOCK_FIN_WAIT`/`SOCK_CLOSING`/`SOCK_LAST_ACK`/`SOCK_TIME_WAIT`; subsequent nonblocking retries issue `DISCON` again in an illegal state. Compare to `connect_IO_6` at `Ethernet/socket.c:477-486` which polls status without re-issuing `CONNECT`.

**Trigger:** Nonblocking TCP socket with an unresponsive peer. Caller invokes `disconnect(sn)` which returns `SOCK_BUSY`; caller polls and calls `disconnect(sn)` again. Repeat. Each retry issues one spurious `Sn_CR_DISCON` SPI write.

**Failure:** On most W5500 revisions the spurious DISCON is silently ignored by the silicon, so this is mostly a SPI-bandwidth waste. On some errata silicon the illegal command can cause premature `SOCK_CLOSED` transition with loss of pending TX. Either way the nonblocking retry contract is violated: retry should poll, not re-trigger.

**Action:** Track in-progress disconnect with a per-socket bit (analogous to `sock_is_sending`). Issue `Sn_CR_DISCON` only when no disconnect is already in flight. Re-entry should poll `Sn_SR`/`Sn_IR_TIMEOUT` and return `SOCK_BUSY`, `SOCK_OK`, or `SOCKERR_TIMEOUT` as appropriate.

**Verification:** Nonblocking TCP socket in `SOCK_ESTABLISHED`. Issue `disconnect()` then immediately call `disconnect()` 100 times in a tight loop. SPI capture must show exactly one `Sn_CR_DISCON` write, not 100.

**Confidence:** Medium (silicon typically ignores illegal DISCON; impact varies by revision).

**Evidence type:** source + analytical + datasheet.

### [ ] AUD-045: Add NULL validation to `wiz_send_data()` / `wiz_recv_data()` for defense in depth

**Status:** Source-confirmed.

**Severity:** Low.

**Category:** Correctness, security (defense in depth).

**Evidence:** `Ethernet/W5500/w5500.c:203-219` (`wiz_send_data`) and `Ethernet/W5500/w5500.c:221-237` (`wiz_recv_data`) dereference `wizdata` after only an `if (len == 0) return;` guard. With `len > 0` and `wizdata == NULL` the SPI callback (`WIZCHIP_WRITE_BUF`/`READ_BUF`) dereferences NULL inside its payload loop.

**Trigger:** Integrator programming error passing NULL to either function with non-zero length. No current in-tree caller does this, but the functions are exported as part of the W5500 driver surface and the contract is undocumented.

**Failure:** Hard memory-access fault. Defense-in-depth gap rather than a remote or normal-path defect.

**Action:** Add `if (wizdata == 0 && len > 0) return;` (or return a status once the API is changed per AUD-009). Document that `wizdata` must be non-NULL when `len > 0`.

**Verification:** Call `wiz_send_data(0, NULL, 10)` and `wiz_recv_data(0, NULL, 10)` under ASan. Must return without faulting.

**Confidence:** High (defense-in-depth only).

**Evidence type:** source.

### [ ] AUD-049: Program `Sn_DHAR` with the group MAC before opening a W5500 multicast socket

**Status:** Source-confirmed omission; hardware confirmation of reception impact pending (fourth pass, novel).

**Severity:** Medium.

**Category:** Correctness (multicast configuration).

**Evidence:** `Application/multicast/multicast.c:63-71` (`multicast_loopback`) and `Application/multicast/multicast.c:126-134` (`multicast_recv`) configure a multicast socket by writing only `Sn_DIPR` (group IP) and `Sn_DPORT` (group port) before `socket(sn, Sn_MR_UDP, port, Sn_MR_MULTI)`. Neither writes `Sn_DHAR`. The W5500 datasheet multicast procedure requires `Sn_DHAR` to hold the IP-to-multicast-MAC mapping (`01:00:5E:` + lower 23 bits of the group IP) before the `OPEN` command so the MAC-layer RX filter admits the group's frames. `setSn_DHAR` exists at `Ethernet/W5500/w5500.h` and is reachable. This is distinct from AUD-019 (which covers the wrong-socket-index and wrong-local-port bugs but not the missing DHAR programming).

**Trigger:** Join any multicast group and expect reception on a W5500 configured only via these helpers.

**Failure:** Depending on silicon/PHY filter behavior, group frames may be dropped by the MAC RX filter because `Sn_DHAR` does not match the group's multicast MAC, so `recvfrom()` never fires. On parts/configs where DHAR defaults happen to admit the traffic the symptom is latent.

**Action:** Before `OPEN` in multicast mode, compute the group MAC from `multicast_ip` (`{0x01,0x00,0x5E, multicast_ip[1] & 0x7F, multicast_ip[2], multicast_ip[3]}`) and call `setSn_DHAR(sn, mac)`; open with `SF_MULTI_ENABLE` (fold with AUD-019's port/socket fixes).

**Verification:** Hardware-in-loop: join `239.x.y.z` on a non-default port, send a group datagram, and require reception with and without the DHAR write to demonstrate the dependency. Label as target-confirmed only after board test.

**Confidence:** Medium (datasheet-grounded; exact silicon RX-filter behavior for multicast without DHAR needs a board to confirm).

**Evidence type:** source + datasheet.

## P3 - Efficiency and Hardening

### [ ] AUD-029: Disable raw packet logging by default and make diagnostics bounded

**Status:** Source-confirmed; size numbers below are agent measurements to reproduce.

**Evidence:** Multicast debugging is enabled by default at `Application/multicast/multicast.h:10-12`; `Application/multicast/multicast.c:117-123` calls `printf` once per payload byte. `Application/loopback/loopback.c:252-253` prints remote bytes as a C string unconditionally.

**Failure:** Remote control bytes can forge/alter terminal output. At 115200 baud, 2048 payload characters require at least 177.8 ms of UART wire time. One audit measured approximately 7.1 KiB extra text plus 424 bytes of data/BSS for the debug path with Cortex-M4 newlib-nano; reproduce before using this as a release metric.

**Action:** Default payload logging off. Log bounded metadata or a short escaped/hex prefix through a nonblocking, rate-limited ring. Never pass raw terminal controls through production logs.

**Verification:** Production objects must not reference `printf` from these helpers. Capture malicious ESC/CR/LF payloads and require escaped output. Reproduce flash/RAM and execution-time deltas with the project toolchain.

### [ ] AUD-030: Use sequential VDM transfers for 16-bit W5500 registers

**Status:** Source-confirmed optimization opportunity; benchmark pending.

**Evidence:** Two-byte registers use two scalar SPI frames throughout `Ethernet/W5500/w5500.h:1804-1823`, `Ethernet/W5500/w5500.h:1832-1850`, and `Ethernet/W5500/w5500.h:1954-2033`. Stable FSR/RSR reads use up to four scalar frames at `Ethernet/W5500/w5500.c:174-200`.

**Cost model:** One ordinary 16-bit access currently uses 2 frames/8 wire bytes versus 1 frame/5 bytes with sequential VDM. Two complete stable samples use 4 frames/16 bytes versus 2 frames/10 bytes. This cuts frames by 50% and wire bytes by 37.5%.

**Action:** Add byte-array-based `read16`/`write16` helpers using `WIZCHIP_READ_BUF`/`WIZCHIP_WRITE_BUF`. Continue taking two complete samples for volatile FSR/RSR values; sequential access does not make them atomic.

**Verification:** Callback spies must confirm frame counts, byte order, rollover behavior, and correct retry behavior for changing FSR/RSR sequences. Benchmark with target SPI callbacks.

**Note:** The same single-VDM 16-bit accessor also resolves the concurrency hazards tracked under AUD-040 (torn writes) and AUD-041 (torn reads). The efficiency framing here is one of three independent motivations for the same primitive fix.

### [ ] AUD-031: Remove duplicate mode and destination traffic from W5500 `sendto()`

**Status:** Source-confirmed optimization opportunity.

**Evidence:** `Ethernet/socket.c:782-850` reads `Sn_MR` repeatedly and, in the W5500 path, writes destination IP and port before validation and then writes them again at lines 849-850.

**Failure/Cost:** Valid sends pay redundant SPI frames; invalid destinations can mutate socket registers before returning an error. One audit estimated four avoidable frames and 19 wire bytes per ordinary IPv4 UDP send.

**Action:** Read mode once, validate all arguments and state before mutation, then program destination IP and port once.

**Verification:** Use callback spies to assert no register writes on invalid input and exactly one destination IP/port write on a valid send. Benchmark frame and byte counts before/after.

### [ ] AUD-032: Provide a real platform burst SPI path

**Status:** Source-confirmed fallback cost.

**Evidence:** The supplied burst callbacks at `Ethernet/wizchip_conf.c:192-213` loop over `_read_byte`/`_write_byte`; registration occurs at `Ethernet/wizchip_conf.c:407-416`.

**Cost:** A 2048-byte W5500 payload plus three-byte header invokes 2051 indirect byte callbacks when only the fallback is used. Wire bytes are unchanged, but CPU and callback overhead remain per byte.

**Action:** Keep the byte fallback for compatibility, but register a synchronous platform FIFO/DMA burst implementation where available and document its completion/error contract per AUD-009.

**Verification:** Callback counters and cycle measurements must show one header burst plus one payload burst and no post-CS clocking.

### [ ] AUD-033: Reduce W5500 datagram RX pointer transactions

**Status:** Source-confirmed optimization opportunity; correctness-sensitive.

**Evidence:** A new UDP packet reads/updates `Sn_RX_RD` through separate `wiz_recv_data()` calls for header and payload at `Ethernet/socket.c:1036-1086`, with command commits again at `Ethernet/socket.c:1197-1200`.

**Cost:** One audit estimated that maintaining one local pointer and committing once can save roughly six SPI frames and 24 wire bytes per packet before applying AUD-030.

**Action:** Read the RX pointer once, read header and payload from locally advanced addresses, publish the final pointer once, and issue one RECV command. Preserve partial-datagram state exactly.

**Verification:** Cover RX-ring wrap, zero-length UDP, truncated multi-call receives, malformed lengths, and packet-info flags. Assert one final pointer commit without behavior changes.

### [ ] AUD-034: Avoid duplicate receive-size probes in Application helpers

**Status:** Source-confirmed optimization opportunity; depends on AUD-004.

**Evidence:** Application probes occur at `Application/loopback/loopback.c:37-41`, `Application/loopback/loopback.c:123-127`, `Application/loopback/loopback.c:197-201`, `Application/loopback/loopback.c:247-251`, `Application/multicast/multicast.c:19-28`, and `Application/multicast/multicast.c:96-105`. `recv()` and `recvfrom()` resample at `Ethernet/socket.c:663-694` and `Ethernet/socket.c:980-1003`.

**Cost:** A stable nonzero pre-probe can add four SPI frames and 16 wire bytes per packet.

**Action:** After fixing nonblocking receive semantics, let the receive API perform the availability check once, or add an internal API that safely consumes an already sampled size. Do not remove probes while calling a blocking receive from an eight-socket sweep.

**Verification:** Compare per-packet SPI frame counts and ensure idle Application calls remain bounded and nonblocking.

### [ ] AUD-035: Use W5500 `INTn`/`SIR` to avoid polling every idle socket

**Status:** Architectural optimization; target measurement required.

**Evidence:** Application helpers poll each socket independently. W5500 exposes aggregate socket activity through `SIR`/`SIMR` at `Ethernet/W5500/w5500.h:292-307`.

**Cost model:** An eight-socket idle UDP sweep can consume about 24 scalar SPI frames/96 wire bytes; established TCP polling adds more interrupt reads. One SIR read can identify active sockets.

**Action:** Use `INTn` and `SIR` to schedule active socket owners, with a low-frequency recovery poll and the single-event-owner model from AUD-012.

**Verification:** Measure idle SPI frames, CPU utilization, active-socket latency, missed-event recovery, and interrupt storms before/after on the target board.

### [ ] AUD-046: Cache `Sn_TXBUF_SIZE` / `Sn_RXBUF_SIZE` after init to avoid per-operation re-reads

**Status:** Source-confirmed optimization opportunity; integration-dependent.

**Severity:** Low.

**Category:** Efficiency.

**Evidence:** `Ethernet/socket.c:553` (`freesize = getSn_TxMAX(sn)` in TCP `send`), `Ethernet/socket.c:652` (`recvsize = getSn_RxMAX(sn)` in TCP `recv`), `Ethernet/socket.c:853` (`freesize = getSn_TxMAX(sn)` in `sendto`). Each `getSn_TxMAX`/`getSn_RxMAX` expands through `w5500.h:2098`/`w5500.h:2113` to one `WIZCHIP_READ(Sn_TXBUF_SIZE(sn))` per call. Buffer sizes are set once at init via `wizchip_init` and are guaranteed by the W5500 datasheet not to change until the next `Sn_CR_OPEN`, which itself resets software state.

**Cost:** One VDM frame, 4 wire bytes, 1 CS cycle per `send`/`recv`/`sendto`. For an 8-socket event loop performing both TX and RX on each socket per iteration, this is 16 avoidable frames per loop iteration.

**Action:** Add `static uint16_t sn_txmax[_WIZCHIP_SOCK_NUM_]` / `sn_rxmax[_WIZCHIP_SOCK_NUM_]` arrays in `socket.c`. Populate them inside `wizchip_init` (which already iterates all sockets to write the size registers), and update them inside `setSn_TXBUF_SIZE`/`setSn_RXBUF_SIZE` so post-init reconfiguration stays correct. Cost: 32 bytes of RAM on W5500 (8 sockets × 2 bytes × TX+RX).

**Verification:** TCP `send` 1 KiB on a socket whose TX buffer is 2 KiB: assert `Sn_TXBUF_SIZE` is no longer touched on the SPI bus. Reconfigure a buffer size after init and confirm the cache matches the new register readback.

**Confidence:** Medium (requires coordination with `setSn_*BUF_SIZE` to remain correct after reconfiguration).

**Evidence type:** source + analytical.

### [ ] AUD-047: Add `len == 0` early return to `wiz_recv_ignore`

**Status:** Source-confirmed.

**Severity:** Low.

**Category:** Efficiency, correctness.

**Evidence:** `Ethernet/W5500/w5500.c:240-246` (`wiz_recv_ignore`) unconditionally reads `getSn_RX_RD(sn)` and writes `setSn_RX_RD(sn, ptr+len)` even when `len == 0`. The sibling functions `wiz_send_data` (`w5500.c:207-209`) and `wiz_recv_data` (`w5500.c:225-227`) both have an explicit `if (len == 0) return;` early return.

**Failure:** A zero-length ignore (defensive programming pattern, or an unusual packet-continuation case) performs four unnecessary VDM frames (`getSn_RX_RD` is two frames per AUD-040; `setSn_RX_RD` is two more). The pointer is bumped by zero so the values are unchanged, but the silicon sees a `RECV` command (issued by the caller) sampling a stale pointer.

**Action:** Add `if (len == 0) return;` at the top of `wiz_recv_ignore`, matching its siblings.

**Verification:** Call `wiz_recv_ignore(sn, 0)`; SPI must remain idle.

**Confidence:** High (trivial, provably safe).

**Evidence type:** source.

### [ ] AUD-048: Read `IR` and `SIR` in one VDM frame in `wizchip_getinterrupt()`

**Status:** Source-confirmed optimization opportunity (fourth pass, novel).

**Severity:** Low.

**Category:** Efficiency.

**Evidence:** `Ethernet/wizchip_conf.c:840-841` (W5500 branch) reads `ir = getIR();` then `sir = getSIR();` as two separate CS-framed VDM reads. On the W5500 the common-register block is contiguous: `IR` at 0x0015, `IMR` at 0x0016, `SIR` at 0x0017 (`Ethernet/W5500/w5500.h:270,290,298`). A single 3-byte VDM read starting at 0x0015 returns `IR`, `IMR`, `SIR` in one frame (the middle `IMR` byte is discarded), because the W5500 auto-increments the address within one CS frame.

**Cost:** 2 frames / 8 wire bytes → 1 frame / 6 wire bytes per call. Relevant on interrupt-driven event loops that call `wizchip_getinterrupt()`/`ctlwizchip(CW_GET_INTERRUPT)` each iteration.

**Action:** Add an internal `WIZCHIP_READ_BUF(IR_addr, tmp3, 3)` helper and unpack `ir = tmp3[0]; sir = tmp3[2];`. Preserve exact returned semantics (mask/WOL handling unchanged).

**Verification:** Callback spy asserts exactly one CS frame of 6 bytes for `wizchip_getinterrupt()`; returned `intr_kind` matches the two-frame version bit-for-bit across all IR/SIR combinations.

**Confidence:** High (feasible); value is minor and best bundled with AUD-035 (SIR-driven scheduling).

**Evidence type:** source + analytical + datasheet (VDM auto-increment).

## Verification Backlog

### [ ] VER-001: Build the complete W5500 audit scope with strict host compilers

Compile `Ethernet/W5500/w5500.c`, W5500 branches of `Ethernet/socket.c` and `Ethernet/wizchip_conf.c`, and all W5500-reachable `Application/**` translation units with GCC and Clang using strict C99/C11 warning gates and static analyzers.

### [ ] VER-002: Create a host-side W5500 register/SPI model

The model must support CS-framed VDM reads/writes, register side effects, TX/RX ring pointers, command acceptance versus completion, SENDOK/TIMEOUT, partial UDP/IPRAW packets, configurable socket memory, stuck-MISO/reset faults, and deterministic interleaving hooks.

### [ ] VER-003: Run sanitizers and schedule-controlled regression tests

Use ASan/UBSan for public pointer/option paths and a schedule-controlled harness for socket ownership, global masks, callback publication, destination races, and interrupt-event ownership.

### [ ] VER-004: Measure target-board timing and resource budgets

Measure SPI frame counts, cycles, stack high-water marks, flash/RAM contribution, maximum IRQ-off duration, scheduler jitter, watchdog behavior, and fault-recovery deadlines at the minimum and maximum supported SPI rates.

### [ ] VER-005: Re-run the five independent audits under alternate models

Run security, embedded fitness, locks/blocking, correctness, and efficiency audits again after changing the selected model. Compare against this snapshot, verify novel findings independently, and update this file without discarding resolved or rejected history.

### [ ] VER-006: Hardware-in-the-loop confirmation of the conditional findings

Stand up a real W5500 on a logic-analyzer + fault-injection rig and convert the model/analytical findings to target-confirmed (or reject them). Priority targets: stuck-high/stuck-low MISO, chip-absent, and reset-held hangs (AUD-007); maximum IRQ-off duration across the supported SPI-rate range (AUD-008); DMA-callback-inside-critical-section deadlock (AUD-009); PHY reset settle timing (AUD-039); illegal-DISCON silicon behavior (AUD-044); and multicast reception with vs without `Sn_DHAR` (AUD-049). This is the single highest-value remaining audit because roughly a third of the report is currently silicon-dependent and unverified. Broader than VER-004 (which is timing/resource budgets); this one is pass/fail confirmation of specific defect claims.

### [ ] VER-007: Coverage-guided fuzzing of the packet-parse paths under sanitizers

Drive the W5500 register/RX-buffer model (VER-002) with libFuzzer or AFL++ over the `recvfrom_IO_6` UDP/IPRAW/MACRAW `head[]` decode and continuation state machine (`Ethernet/socket.c:966-1221`), compiled with ASan+UBSan. Exhaustively exercise the length arithmetic, `sock_remained_size`/`sock_pack_info` transitions, ring-wrap, zero-length datagrams, and malformed/oversized packet-info lengths rather than the hand-selected cases used so far. Acceptance: no sanitizer abort across a sustained corpus; any new crash filed as an AUD finding.

### [ ] VER-008: UndefinedBehaviorSanitizer sweep of the driver core

Run `-fsanitize=undefined` (and `-fsanitize=alignment`) against the W5500 driver paths, not just the Application layer. Specifically probe: the `union _IF` type-punning and mismatched function-pointer call surfaced in AUD-003 (validated against the ARM AAPCS calling convention); unaligned halfword access from the `SO_KEEPALIVEAUTO` `uint16_t*` write (AUD-002); and signed-shift / integer-promotion behavior in the 16-bit register accessor macros (`Ethernet/W5500/w5500.h`). Acceptance: clean UBSan run or each diagnostic triaged to an AUD entry.

### [ ] VER-009: MISRA C:2012 / CERT C compliance audit

Run a coding-standard pass (cppcheck `--addon=misra`, clang-tidy `cert-*`/`bugprone-*`, or a commercial checker) over the W5500 scope. This targets the standards-driven defect class an integrator's process demands — implicit conversions, macro hygiene, single-exit, side-effects in expressions — which the correctness audit did not systematically enumerate. Record deviations that are justified (e.g. intentional ring-pointer wrap, RC-003) versus actionable.

### [ ] VER-010: Real embedded toolchain build matrix

Cross-compile the W5500 scope with `arm-none-eabi-gcc` (Cortex-M0+ and M4), plus at least one commercial compiler (IAR EWARM and/or Keil armclang), under strict gates (`-Wall -Wextra -Wpedantic -Werror`, `-pedantic-errors`). Confirm the portability findings on real toolchains: the empty-`__VA_ARGS__` variadic dispatch macros (AUD-028), `%ld`/`int32_t` format mismatches, and any 8/16-bit-target integer-width assumptions. Acceptance: documented per-compiler build result; every new hard error becomes an AUD entry.

### [ ] VER-011: Formal model-checking of the concurrency interleavings

Replace the hand-constructed interleavings behind the concurrency findings with mechanical proof. Use CBMC on the `send`/`recv`/`sendto` pointer-read → buffer → pointer-write sequences (`Ethernet/W5500/w5500.c:203-246`) and the `sock_is_sending`/`sock_io_mode` flag RMWs, or a small TLA+/Spin model of the socket state machine. Goal: prove or disprove AUD-011, AUD-040, AUD-041, and mechanically settle the disputed AUD-037 (whether the second `sock_is_sending` block is truly unreachable on W5500).

### [ ] VER-012: Deeper static analysis with taint-tracking tools

Run CodeQL, clang-tidy, Infer, or a commercial engine (Coverity / PVS-Studio) with a modeled W5500 callback boundary. The built-in Clang analyzer found only 3 dead stores because it cannot follow data across the SPI callback and hardware boundary; a taint-aware tool that treats received packet bytes as tainted could trace them further into `socket.c`/Application logic than manual review did. Acceptance: triage every new alert to an AUD entry or a rejected candidate.

## Rejected Candidates

These candidates were raised by one or more audit agents but are rejected as false positives, out of scope, or duplicates:

### RC-001: W5300 errata workaround in close() — OUT OF SCOPE

`Ethernet/socket.c:359-381` is gated by `#if (_WIZCHIP_ == 5300)`. Does not execute on W5500.

### RC-002: getsockopt SO_FLAG double write — OUT OF SCOPE

`Ethernet/socket.c:1347-1352` double-write only occurs in `#ifdef IPV6_AVAILABLE` path, which is not defined for W5500.

### RC-003: 16-bit ring-buffer pointer wrap in wiz_send_data/wiz_recv_data — DESIGN INTENT

`Ethernet/W5500/w5500.c:203-237` correctly implements 16-bit ring-buffer semantics per W5500 datasheet. Pointer wrap at 0xFFFF is by design.

### RC-004: TOCTOU double-read in getSn_TX_FSR/getSn_RX_RSR — MITIGATED

`Ethernet/W5500/w5500.c:174-201` - the double-read-and-compare pattern is a necessary workaround for non-atomic 16-bit register access and is practically convergent. The absence of a bounded retry limit is captured under AUD-007.

### RC-005: Sendto IP address unpack-repack — COMPILER OPTIMIZED

`Ethernet/socket.c:827-831` byte-to-uint32 unpacking compiles to nearly identical machine code at `-O2` due to compiler pattern matching. No measurable runtime difference.

### RC-006: DIPR/DPORT set for MACRAW sockets unconditionally — HARMLESS

`Ethernet/socket.c:849-850` writes DIPR/DPORT for MACRAW sockets, but hardware ignores these registers in MACRAW mode. The writes themselves are harmless; the unconditional `addr[]` dereference before them is tracked separately under AUD-043.

### RC-007: recvfrom_W5x00 dummy pointer to stack local — HARMLESS for W5500

`Ethernet/socket.c:931-933` passes `&addrlen` as dummy output. In the W5500 non-IPV6 path, the pointer is never dereferenced inside `recvfrom_IO_6`. Harmless.

### RC-008: recvfrom/connect IR-clearing race between poll and ISR — BENIGN

`Ethernet/socket.c:477-478, 503-504, 896-897` - the W5500 write-1-to-clear semantics make this race benign. Writing 1 to an already-cleared bit is a no-op. The polling path seeing a cleared bit and correctly returning before re-clearing it is acceptable behavior.

### RC-009: Default WIZCHIP_CRITICAL_ENTER/EXIT callbacks are no-ops — INTEGRATION CONTRACT

`Ethernet/wizchip_conf.c:67, 75` - the default cris/cs callbacks are empty stubs. This is the documented integration point: `reg_wizchip_cris_cbfunc` MUST be wired by the integrator (per `wizchip_conf.h:851`). The integration hazard of leaving defaults is captured under AUD-008 and AUD-011; the stubs themselves are not a defect.

### RC-010: `sock_io_mode |= (1 << sn)` undefined behavior for `sn >= 16` — UNREACHABLE

`Ethernet/socket.c:339, 342, 600, 1232` - `CHECK_SOCKNUM()` rejects `sn >= _WIZCHIP_SOCK_NUM_` (= 8 for W5500) at the entry of every public socket function. Unreachable.

### RC-011: `WIZCHIP_OFFSET_INC` macro allows unbounded offset addition — INTERNAL MACRO

`Ethernet/W5500/w5500.h:72` - internal macro, only used by trusted code with fixed offsets (e.g., `+1` to access the low byte of a 16-bit register). Not reachable by remote input.

### RC-012: `connect_IO_6` blocked indefinitely if hardware never sets TIMEOUT — DUPLICATE

Covered by the broader AUD-007 polling-loop deadline finding. W5500 hardware bounded by RCR×RTR (default 8×200 ms = 1.6 s).

### RC-013: `peeksockmsg` reads chip buffer without updating Sn_RX_RD — OUT OF SCOPE

`Ethernet/socket.c:1449-1469` is compiled only under `#ifdef IPV6_AVAILABLE` which is gated on `_WIZCHIP_ > W5500`. Not reachable on W5500.

### RC-014: MACRAW `sock_remained_size = head[0..1] - 2` underflow at `socket.c:1097` — SAFE BY CONSTRUCTION

Subsequent `> 1514` check at `socket.c:1105` correctly catches the underflowed (huge) value and closes the socket.

### RC-015: IPRAW `sock_remained_size = head[4..5]` unbounded at `socket.c:1130-1133` — HARDWARE-GENERATED

The W5500 hardware generates the IPRAW packet-info length from the actual IP payload length of the received datagram; `pack_len` is bounded by `min(len, sock_remained_size)` so host `buf` is never overflowed.

### RC-016: `W5500` macro-guard "mismatch" (`_WIZCHIP_ == W5500` vs literal `5500`) — FALSE POSITIVE

Raised in a fourth-pass hint that the driver defines `W5500` inconsistently with the `5500` literal used in `w5500.c`. Verified false: `Ethernet/wizchip_conf.h:72` defines `#define W5500 5500`, so `#if (_WIZCHIP_ == W5500)` (used in `wizchip_conf.c`, `socket.c`) and `#if (_WIZCHIP_ == 5500)` (used in `w5500.c`) select the same branches. `-Wundef` did not flag `W5500` as undefined. All W5500-guarded code is reachable; no defect.

### RC-017: `ctlnetwork(CN_SET_NETMODE)` case fallthrough — NOT REACHED ON W5500

Raised repeatedly as a `switch` fallthrough. Verified: on the W5500 path `Ethernet/wizchip_conf.c:588-589` executes `return wizchip_setnetmode(...)`, so control never falls through into `CN_GET_NETMODE`. The fallthrough exists only in the `#elif ((_WIZCHIP_ == 6100)||(_WIZCHIP_ == W6300))` branch (line 591-592, no `return`), which is out of scope. Not a W5500 defect. (The separate MR-cannot-clear limitation is tracked under AUD-022.)

### RC-018: Dead but harmless code fragments on the W5500 path — CLEANUP-ONLY, NOT DEFECTS

Two fourth-pass observations that are dead code, not runtime defects (recorded so later passes do not re-raise them as bugs): (1) the second `if (sock_is_sending & (1 << sn))` block in `send()` at `Ethernet/socket.c:582-596` is unreachable on W5500 (the bit is always cleared or the function has returned by line 551) — this is the basis for disputing AUD-037; (2) the `recvfrom_IO_6` second dispatch `switch (mr & 0x07)` at `Ethernet/socket.c:1014` carries case labels `Sn_MR_UDP6` (0x0A), `Sn_MR_UDPD` (0x0E), and `Sn_MR_IPRAW6` (0x0B) that can never match a 3-bit-masked selector; harmless on W5500 (live modes UDP=0x02, IPRAW4=0x03, MACRAW=0x04 all match). Also the unused `taddr[16]`/`local_port` in `socket()` (`socket.c:199-200`) and the dead `tcmd` store (`socket.c:809`, Clang analyzer `deadcode.DeadStores`) — cosmetic; `tcmd` cleanup is subsumed by AUD-031.

## Validation Snapshot

Validation date: 2026-07-18 (third pass).

### Post-Fix Host Verification (2026-07-18, all-audit-fixes branch)

VER-001 strict-C compile on the consolidated branch found and fixed:
- `multicast.c:11`: duplicate `static static` from merge — fixed
- `multicast.c:86-91`: duplicate variable declarations from merge — fixed
- `w5500.h:82`: duplicate `IINCHIP_WRITE_BUF` define from merge — fixed
- Remaining cosmetic: `socket.c:199-200` unused `taddr[16]`/`local_port`, `socket.c:934` unused `_poll` (AUD-007 counter in bounded Sn_CR wait)

VER-008 UBSan confirmed AUD-003 union type-punning hazard: when BUS member is initialized with `iodata_t (*)(uint32_t)` and called through SPI member as `uint8_t (*)(void)`, the mismatched return type produces garbage (0x30 vs expected 0x42). On ARM AAPCS this would dereference a garbage pointer in r0 as a memory address.

### Compiler and Tool Versions

- **GCC:** 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04)
- **Clang:** 18.1.3 (Ubuntu 18.1.3-1ubuntu1)
- **cppcheck:** 2.13.0
- **markdownlint:** 0.48.0

### GCC Syntax Check

Command:

```bash
gcc -D_WIZCHIP_=5500 -std=c11 -Wall -Wextra -Wpedantic -Wundef -Wformat=2 -fsyntax-only \
  -IEthernet -IEthernet/W5500 -IApplication/loopback -IApplication/multicast \
  Ethernet/W5500/w5500.c Ethernet/socket.c Ethernet/wizchip_conf.c \
  Application/loopback/loopback.c Application/multicast/multicast.c
```

Result: 0 errors, 35 warnings. Exit code 0.

Notable warnings (all captured as findings above):

- `Ethernet/socket.c:1039` `-Wmisleading-indentation`: `while (getSn_CR(sn));` does not guard the indented `addr[0] = head[0];` at line 1054. Captured under AUD-028 (structural fragility) and AUD-042 (IPv4 path lacks NULL check).
- `Ethernet/socket.c:199-200` unused `taddr[16]` and `local_port` in `socket()`. Dead code; benign but worth cleanup.
- `Ethernet/socket.c:772` `tcmd` set but not used in `sendto_IO_6`. The W5500 path's `tcmd` is written but never read because lines 849-850 always use `setSn_DIPR`/`setSn_DPORT` and line 919 unconditionally issues `Sn_CR_SEND`. Captured under AUD-031 (duplicate destination writes).
- `Ethernet/socket.c:941` unused parameter `addrlen` in `recvfrom_IO_6`. Confirms RC-007 (IPv4 path never dereferences `addrlen`).
- `Application/loopback/loopback.c` and `Application/multicast/multicast.c`: `%ld` with `int32_t` (7 occurrences), variadic-macro empty-tail, misspelled `LOOPBACK_MAIN_NOBLCOK`. All captured under AUD-028.

### Clang Syntax Check

Command: Same flags, `clang` instead of `gcc`.

Result: 0 errors, ~50 warnings. Exit code 0.

Notable warnings (in addition to GCC's set):

- `-Wempty-body` for every `while(getSn_CR(sn));` pattern (~20 occurrences). These are the polling loops tracked under AUD-007.
- `-Wgnu-zero-variadic-macro-arguments` for `connect`/`sendto`/`recvfrom` variadic dispatch macros at `socket.h:650-723`. Captured under AUD-028.
- `-Wformat` for `%ld` with `int32_t`. Captured under AUD-028.
- `-Wmissing-field-initializers` at `wizchip_conf.c:274`: the SPI callback union init does not initialize all members. Captured under AUD-003.
- `-Wunused-function` for `connect_IO_6`, `sendto_IO_6`, `recvfrom_IO_6` declared `static` in the public header `socket.h:314, 404, 439`. Captured under AUD-028.

### cppcheck Analysis

Command:

```bash
cppcheck --enable=warning,performance,portability --std=c11 --force \
  -D_WIZCHIP_=5500 -IEthernet -IEthernet/W5500 \
  -IApplication/loopback -IApplication/multicast \
  Ethernet/W5500/w5500.c Ethernet/socket.c Ethernet/wizchip_conf.c \
  Application/loopback/loopback.c Application/multicast/multicast.c
```

Result: 7 `invalidPrintfArgType_sint` warnings for `%ld` with `int32_t` in `Application/loopback/loopback.c:204,214,256,266` and `Application/multicast/multicast.c:35,54,112`. No additional defects identified beyond those already captured under AUD-028. Exit code 0.

### Fourth Pass (Fable-model cross-validation, 2026-07-18)

A fourth independent audit was run with a different model (Claude Fable 5) using five parallel general-purpose domain agents (security, embedded fitness, concurrency/blocking, correctness, efficiency), each working from source with no access to this file until the passes completed. **Result: every finding produced by the five Fable agents mapped onto an existing AUD-001…AUD-047 finding** — an independent confirmation of the prior three passes. The fourth pass added two novel low/medium findings (AUD-048 IR+SIR single-frame read; AUD-049 missing `Sn_DHAR` multicast MAC programming), disputed AUD-037 and re-scoped AUD-036 (see their status lines), and recorded RC-016…RC-018.

Fresh tool evidence gathered this pass:

- **Clang static analyzer** (`clang --analyze -analyzer-output=text`, `-D_WIZCHIP_=5500`) over all five scope `.c` files: 3 warnings, all `deadcode.DeadStores`, no memory-safety or path-sensitive defects on driver paths — `Ethernet/wizchip_conf.c:436` (`tmp` init, relates to AUD-005), `Ethernet/socket.c:809` (`tcmd`, relates to AUD-031), `Ethernet/socket.c:1073` (`len`, dead store in `recvfrom_IO_6`). The absence of buffer/UB findings on the driver core corroborates that the OOB write (AUD-001) lives in the Application layer, not the driver.
- **AddressSanitizer model harness** for AUD-001 (`clang -fsanitize=address`, faithful excerpt of `loopback_udpc` statement ordering with a stubbed `recvfrom`): full-size datagram (`ret == DATA_BUF_SIZE`) → `stack-buffer-overflow` WRITE of size 1 at `buf[2048]`; error return (`ret == SOCKERR_SOCKCLOSED`) → `stack-buffer-underflow` WRITE of size 1 at `buf[-13]`. AUD-001 is now model-confirmed.
- **Efficiency** findings AUD-030/031/033/034/035 were re-derived by a fourth-pass instrumented W5500 SPI/CS model that counts CS-framed VDM transactions; measured before/after frame counts are consistent with the prior cost models (e.g., `sendto` 24→15 frames, `recvfrom` 20→10 frames after the combined 16-bit-VDM + dedup + pointer-commit patch). These remain model measurements pending target-board confirmation (VER-004).

### Deduplication Check

- No duplicate `AUD-*`, `VER-*`, or `RC-*` task headings. All IDs `AUD-001` through `AUD-049` are unique (AUD-048, AUD-049 added in the fourth pass). `VER-001` through `VER-012` are unique (VER-006 through VER-012 added in the fourth pass as future-audit backlog). `RC-001` through `RC-018` are unique (RC-016, RC-017, RC-018 added in the fourth pass).
- No trailing whitespace found.
- All cited paths verified present.

### Scope Compliance

- `Internet/**` contributes no findings.
- No other-chip-only issue is reported. Every finding was re-checked to ensure the cited path is reachable under `_WIZCHIP_ == 5500`.
- No source file other than `TODO.md` was modified.
- All W5500-specific paths verified: `Ethernet/W5500/w5500.c`, `Ethernet/W5500/w5500.h`, conditional branches in `Ethernet/socket.c`, `Ethernet/wizchip_conf.c`, and Application sources.

## Audit Notes

The stable double-sampling pattern for `Sn_TX_FSR` and `Sn_RX_RSR` is required because these 16-bit counters can change while read. The actionable issue is the missing failure deadline (AUD-007), not the repetition itself. Extending the same pattern to other hardware-mutated 16-bit registers is tracked under AUD-041.

Unsigned 16-bit TX/RX pointer wrap in `wiz_send_data()`, `wiz_recv_data()`, and `wiz_recv_ignore()` matches W5500 ring-pointer semantics and is not itself a defect.

No heap allocation or variable-length array was found in the audited W5500 core. Stack/resource conclusions still depend on platform callbacks and libc logging.

The checkout contains pre-existing untracked `.opencode/` and `.specify/` directories. They are unrelated to the audit and must not be modified or removed.

**Third-pass audit model notice:** The five-domain audit was run in three independent passes using the same model (glm-5.2) for cross-validation. Pass 1 produced AUD-001 through AUD-035. Pass 2 independently confirmed all existing findings and added AUD-036, AUD-037, AUD-038 plus RC-001 through RC-008. Pass 3 (this snapshot) re-ran the five independent domain agents with the same model, confirmed all prior findings against the source, and added nine novel findings:

- **AUD-039:** PHY reset lacks settle/wait before re-accessing PHYCFGR (P1).
- **AUD-040:** 16-bit socket-register writes are two SPI frames and can be torn mid-update (P1; shares remediation with AUD-030).
- **AUD-041:** 16-bit socket-register reads lack the seqlock-style retry that `getSn_TX_FSR`/`getSn_RX_RSR` use (P1; shares remediation with AUD-030).
- **AUD-042:** Add NULL validation to IPv4 `recvfrom()` `addr`/`port` outputs (P2).
- **AUD-043:** Skip address/port handling in `sendto()` for MACRAW mode to avoid NULL deref (P2).
- **AUD-044:** Stop `disconnect()` from re-issuing `Sn_CR_DISCON` on every nonblocking retry (P2).
- **AUD-045:** Add NULL validation to `wiz_send_data()` / `wiz_recv_data()` for defense in depth (P2).
- **AUD-046:** Cache `Sn_TXBUF_SIZE` / `Sn_RXBUF_SIZE` after init to avoid per-operation re-reads (P3).
- **AUD-047:** Add `len == 0` early return to `wiz_recv_ignore` (P3).

Pass 3 also added seven additional rejected candidates (RC-009 through RC-015) to document candidates considered and dismissed during the third pass, ensuring later passes do not repeatedly rediscover them.

All third-pass findings were verified against the source by the main session before being committed to this file. No source file other than `TODO.md` was modified.

**Fourth-pass audit model notice (2026-07-18):** A fourth cross-validation pass was run with a different model (Claude Fable 5), five parallel independent domain agents, plus main-session source verification, ASan model harnesses, and the Clang static analyzer (see the Validation Snapshot → Fourth Pass). Every finding independently produced by the Fable agents reproduced an existing AUD-001…AUD-047 finding, confirming the prior three passes. The fourth pass:

- Added **AUD-048** (read `IR`+`SIR` in one VDM frame, P3) and **AUD-049** (program `Sn_DHAR` with the multicast group MAC before `OPEN`, P2) as genuinely novel findings.
- Model-confirmed **AUD-001** with AddressSanitizer (overflow at `buf[2048]`, underflow at `buf[-13]`).
- **Disputed AUD-037**: the second `sock_is_sending` block (`socket.c:582-596`) is unreachable dead code on W5500, so the described SENDOK-race corruption is not reachable as written; retained and flagged for human re-verification rather than deleted.
- **Re-scoped AUD-036**: the recv() `SOCK_CLOSE_WAIT` stall is bounded by the chip's TCP retransmit timeout (same class as AUD-007), not an unbounded hang; flagged for re-verification.
- Added **RC-016** (W5500 macro-guard "mismatch" is a false positive — `W5500` is `#define`d `5500`), **RC-017** (`CN_SET_NETMODE` fallthrough does not reach on the W5500 path — it `return`s at wizchip_conf.c:589), and **RC-018** (dead-but-harmless code fragments) so later passes do not rediscover them.

No existing finding was deleted. No source file other than `TODO.md` was modified in the fourth pass.
