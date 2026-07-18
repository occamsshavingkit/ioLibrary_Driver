# Research: Fix P1 Audit Findings — W5500 ioLibrary_Driver

All technical decisions are pre-determined by the audit findings in TODO.md with adjustments for scope and feasibility.

## AUD-006: Reject unsupported protocols

- **Decision**: Add guard checks at the top of the non-IPv6 `socket()` switch before any SPI access. For W5500, reject `Sn_MR_UDP6`, `Sn_MR_UDPD`, and similar IPv6/dual constants. Reject `Sn_MR_MACRAW` on `sn != 0`.
- **Rationale**: The existing code reaches `sn != 0` validation but not protocol validation. Adding checks before the `while (getSn_SR(sn) != SOCK_CLOSED)` loop at socket.c:336-352 prevents the unbounded wait.
- **Alternatives considered**: Moving protocol validation into `socket_IO_6()`. Rejected — validation should be in the chip-independent layer.

## AUD-007/AUD-036: Bounded polling deadlines

- **Decision**: Add a `_WIZCHIP_POLL_MAX_` constant (default ~10000 iterations) and wrap each infinite polling loop with an iteration counter that returns a new `SOCKERR_TIMEOUT` or equivalent on exhaustion. For AUD-036 specifically, add an explicit return from the `SOCK_CLOSE_WAIT`/zero-data branch.
- **Rationale**: The W5500 has no host-side deadline mechanism — `RTR`/`RCR` bound silicon behavior but not host SPI faults. A simple iteration counter is the minimal change that prevents tight loops without requiring RTOS timer integration.
- **Alternatives considered**: RTOS timer callbacks, hardware watchdog integration. Rejected as platform-specific — iteration counters are portable.

## AUD-008: Interrupt-masked transfers

- **Decision**: Document the single-task assumption in `wizchip_conf.h` near the `WIZCHIP_CRITICAL` macros. Add a comment that if the SPI callback uses DMA, the caller must not mask interrupts globally.
- **Rationale**: Full mutex-based SPI locking (per TODO.md action) requires RTOS integration. The minimum viable fix is documentation warning users not to combine global interrupt masking with DMA-driven callbacks.
- **Alternatives considered**: Splitting transfers into smaller VDM transactions. Rejected — would change the W5500 SPI protocol contract (CS must remain asserted across a VDM frame).

## AUD-009: SPI callback status

- **Decision**: Add documentation in `wizchip_conf.h` stating that SPI callbacks must be synchronous and complete before returning. No API signature change (would break all existing implementations).
- **Rationale**: Status returns would change all callback signatures, breaking every existing integration. Documentation is the minimum viable fix.
- **Alternatives considered**: Adding parallel `_with_status` callback variants. Rejected as over-engineered for a P1 finding.

## AUD-010: Probe W5500 identity

- **Decision**: In `wizchip_sw_reset()`, after the reset sequence, read `VERSIONR` and verify `== 0x04`. In `wizchip_init()`, after calling reset, assert VERSIONR passes. Add a bounded retry on VERSIONR read (allow N attempts with small delay between attempts).
- **Rationale**: `VERSIONR == 0x04` is the documented W5500 identifier at w5500.h:402-407. A bounded retry accounts for SPI line settling.
- **Alternatives considered**: Hardware RSTn pin control. Rejected — requires board-specific GPIO pin configuration.

## AUD-013: Buffer layout validation

- **Decision**: In `wizchip_init()`, before any SPI write or reset, validate each entry in the TX and RX arrays using a wider unsigned accumulator (uint16_t instead of int8_t). Check each value is in {0,1,2,4,8,16} and each total ≤ 16 KiB.
- **Rationale**: The current `int8_t` accumulator can overflow/narrow, bypassing total checks. Legal sizes are documented at w5500.h:570-590.
- **Alternatives considered**: Reading back after programming to verify. Rejected — validation before write prevents bad states, readback is a supplementary check.

## AUD-014: Partial IPRAW receives

- **Decision**: Move `pack_len` calculation and `wiz_recv_data()` outside the `if (sock_remained_size[sn] == 0)` block. First-chunk header parsing stays inside; payload sizing and copy happen on every invocation.
- **Rationale**: The current structure skips payload extraction on continuation calls, leaving `pack_len == 0` and returning zero. Moving payload handling outside the condition fixes the stall.
- **Alternatives considered**: Restructuring the entire IPRAW path. Rejected — over-scoped.

## AUD-015: Nonblocking sendto() after SEND

- **Decision**: Add per-socket pending-send tracking for datagram sockets. Nonblocking mode returns after issuing SEND; blocking mode uses the new bounded deadline from AUD-007.
- **Rationale**: Currently `sendto()` always spins for SENDOK/TIMEOUT regardless of nonblocking mode, defeating its purpose.
- **Alternatives considered**: Timer-based completion. Rejected as platform-specific.

## AUD-016: API return semantics

- **Decision**: Add documentation in `socket.h` distinguishing "command accepted" from "command completed." Add return value comments for each blocking API.
- **Rationale**: The W5500 clears Sn_CR on command acceptance while processing continues. The distinction between acceptance and completion must be explicit in the API contract.
- **Alternatives considered**: Rewriting all blocking APIs to wait for completion. Rejected — AUD-007 already adds deadlines; this augments with documentation.

## AUD-017: Application send loops

- **Decision**: In `loopback.c` and `multicast.c` send loops, check for `ret == SOCK_BUSY` (value 0) and either track partial progress with a static offset or return to the event loop.
- **Rationale**: `SOCK_BUSY == 0` causes the loop to `sentsize += 0` and spin. Explicit handling prevents infinite loops.
- **Alternatives considered**: Changing `SOCK_BUSY` to non-zero value. Rejected — would break all existing callers checking `ret < 0`.

## AUD-018: UDP peer metadata persistence

- **Decision**: In `loopback.c` loopback_udpc, change `destip`/`destport` from automatic (stack) variables to static variables scoped to the function. Reinitialize only on `PACK_COMPLETED`.
- **Rationale**: The socket contract at socket.h:415-433 says addr/port are valid only on first chunk. Application variables must persist them across continuation reads.
- **Alternatives considered**: Processing complete datagram in one invocation. Rejected — would miss execution budget constraints.

## AUD-039: PHY reset settle

- **Decision**: After `getPHYCFGR()` reads back the cleared RST bit, insert a delay loop (approximately 200 µs at common MCU speeds) before re-setting `PHYCFGR_RST`. After the rising edge, poll `PHYCFGR` until `LNK` stabilizes or a bounded deadline expires.
- **Rationale**: Vendor sample code implements this delay. The W5500 datasheet does not specify exact settle time, but ~165 µs is the documented register-file init time.
- **Alternatives considered**: Hardware timer delay. Rejected — iteration-based delay is portable.

## AUD-040/AUD-041: Atomic 16-bit register access

- **Decision**: For writes (AUD-040): wrap each 16-bit setter macro body in a single `WIZCHIP_CRITICAL_ENTER/EXIT`. For reads (AUD-041): apply the read-twice-and-compare pattern to `getSn_TX_RD`, `getSn_TX_WR`, `getSn_RX_RD`, `getSn_RX_WR` and wrap `getSn_PORT`, `getSn_DPORT`, `getSn_MSSR` in a single CRITICAL section.
- **Rationale**: These fixes share remediation with AUD-030 (efficiency). Single CRITICAL section is simplest; seqlock retry already demonstrated by `getSn_TX_FSR`/`getSn_RX_RSR`.
- **Alternatives considered**: Single-VDM burst with `WIZCHIP_READ_BUF`. Rejected as larger refactor — CRITICAL wrapping is minimal.

## AUD-037: Disputed SENDOK race

- **Decision**: The fourth-pass analysis confirms the second `sock_is_sending` block at socket.c:582-596 is unreachable dead code on the W5500 path. Remove it and add a comment documenting why it's unreachable (the bit is always cleared before reaching line 582).
- **Rationale**: Code is unreachable per analysis; keeping it confuses readers and the audit itself.
- **Alternatives considered**: Implementing the race fix anyway. Rejected — fixing an unreachable code path wastes effort.

## AUD-008/009/011/012: Architectural findings

- **Decision**: Minimum viable: documentation + lightweight guards. AUD-008 (IRQ masking): add documentation. AUD-009 (SPI status): add documentation. AUD-011 (concurrency): add `sock_io_mode` atomic-access comment and small critical-section guards where easy. AUD-012 (interrupt ownership): snapshot `Sn_IR` into a local variable before processing to reduce the window for ISR races.
- **Rationale**: Full fixes require RTOS integration (mutexes, task notifications) that belongs in a porting layer, not the core library. Documenting the single-task assumption is the minimum viable.
- **Alternatives considered**: Ignoring these findings entirely. Rejected — P1 findings require at least documentation of the risk.

## Research Conclusions

18 P1 findings addressed. Simple fixes (13) are ready for implementation. Architectural findings (5) addressed with documentation + targeted guards. All fixes are independent and can be applied as separate branches from fork/master.
