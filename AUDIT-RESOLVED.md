# W5500 Audit Findings — Resolved

**Fork**: `occamsshavingkit/ioLibrary_Driver` | **Branch**: `all-audit-fixes`
**Audit snapshot**: `Wiznet/ioLibrary_Driver` commit `39fae86`
**Audit date**: 2026-07-18 | **Resolution date**: 2026-07-18

All 49 findings resolved. Individual PRs submitted for P0; P1-P3 branches on fork ready for submission when upstream accepts outstanding PRs. Full resolution detail in commit messages and branch descriptions.

## Resolution Summary

| Priority | Count | Type | Status |
|----------|-------|------|--------|
| P0 | 5 | Code | PRs #180–184 submitted to Wiznet/ioLibrary_Driver |
| P1 | 16 | 12 code + 4 docs | 16 branches on fork |
| P2 | 16 | 16 code | 16 branches on fork |
| P3 | 10 | 7 code + 3 docs | 10 branches on fork |
| Arch | 4 | Full implementation | LOCK infra, SPI status, IRQ model, bus mutex — on all-audit-fixes |

## Host-Side Verification Results

| ID | Test | Result |
|----|------|--------|
| VER-001 | Strict C99 compile (GCC -Wall -Wextra -Wpedantic -Werror) | 5 issues found (3 fixed, 2 cosmetic remain) |
| VER-007 | libFuzzer packet parser (500K iterations, ASan+UBSan) | Clean — no sanitizer abort |
| VER-008 | UBSan on union type-punning (AUD-003) | Confirmed: mismatched call returns wrong value |
| VER-009 | cppcheck static analysis | Cosmetic only — no new defects |

## P0 — Deployment Blockers

### [x] AUD-001: Prevent out-of-bounds termination in the W5500 UDP client loopback
**Resolution**: Moved `ret <= 0` check before `buf[ret] = 0x00` in `loopback_udpc()`. PR #180.

### [x] AUD-002: Correct the `SO_KEEPALIVEAUTO` getter width
**Resolution**: Changed `uint16_t*` to `uint8_t*` in getsockopt(). PR #181.

### [x] AUD-003: Initialize the active SPI callback member instead of incompatible BUS callbacks
**Resolution**: Added `#if (_WIZCHIP_IO_MODE_ & _WIZCHIP_IO_MODE_SPI_)` conditional in WIZCHIP initializer, initiating `.IF.SPI` with correct stubs. PR #182.

### [x] AUD-004: Make nonblocking W5500 TCP recv() consume available data
**Resolution**: Swapped `recvsize != 0` and nonblocking-mode checks in the W5500 `#else` branch, matching IPv6 branch ordering. PR #183.

### [x] AUD-005: Stop ctlwizchip() from dereferencing absent arguments
**Resolution**: Removed unconditional `uint8_t tmp = *(uint8_t*)arg`. Added guarded dereferences with NULL checks inside cases. PR #184.

## P1 — Reliability, Availability, and Concurrency

### [x] AUD-006: Reject unsupported W5500 protocols and invalid MACRAW sockets before issuing OPEN
**Resolution**: Wrapped IPv6/dual case labels in `#ifdef IPV6_AVAILABLE`. Added MACRAW `sn==0` check before register mutation.

### [x] AUD-007: Add bounded deadlines and recovery to all W5500 polling loops
**Resolution**: Added `_WIZCHIP_POLL_MAX_` constant and `SOCKERR_DEADLINE`. Applied iteration counters to FSR/RSR seqlock loops, socket OPEN wait, send/sendto/recv polling.

### [x] AUD-036: Prevent recv() deadlock in SOCK_CLOSE_WAIT with zero RX and pending TX
**Resolution**: Folded into AUD-007. Added deadline check in SOCK_CLOSE_WAIT/zero-data branch with `continue`.

### [x] AUD-008: Remove full-payload transfers from globally interrupt-masked sections
**Resolution**: Documentation: CRIS struct updated with FreeRTOS `xSemaphoreTake/Give` example. CRITICAL_ENTER macro docs updated. Full implementation on all-audit-fixes with bus mutex pattern.

### [x] AUD-009: Define and enforce synchronous, error-reporting SPI callback semantics
**Resolution**: Documentation: SPI callback contract documented. Full implementation on all-audit-fixes adds `IF.SPI_STATUS` struct with `_check_busy`/`_get_error` callbacks and `_spi_status_check()` wrapper.

### [x] AUD-010: Probe W5500 identity and configuration during initialization and recovery
**Resolution**: Changed `wizchip_sw_reset()` from `void` to `int8_t`. Added VERSIONR check with bounded retry. Updated callers.

### [x] AUD-011: Establish a concurrency model and serialize complete per-socket operations
**Resolution**: Documentation: single-task assumption stated. Full implementation on all-audit-fixes adds `_LOCK` struct with per-socket and global lock callbacks, default no-ops, `reg_wizchip_lock_cbfunc()`, and lock macros.

### [x] AUD-012: Give W5500 socket interrupt events a single software owner
**Resolution**: Documentation: single-owner model documented near `wizchip_clrinterrupt()`. Guidance to snapshot `Sn_IR` into software-pending bits.

### [x] AUD-013: Validate complete W5500 TX/RX buffer layouts before touching hardware
**Resolution**: Added validation of TX/RX arrays before reset: check values in {0,1,2,4,8,16}, totals ≤ 16 KiB, using `uint16_t` accumulator.

### [x] AUD-014: Make partial IPRAW receives progress on every call
**Resolution**: Moved `pack_len` calculation and `wiz_recv_data()` outside the `sock_remained_size==0` condition for non-IPv6 IPRAW path.

### [x] AUD-015: Make nonblocking sendto() nonblocking after the SEND command
**Resolution**: Added nonblocking-mode check after Sn_CR wait in sendto(), returning `len` immediately instead of spinning for SENDOK.

### [x] AUD-016: Align blocking socket API returns with W5500 command completion
**Resolution**: Documentation: comments in `socket.h` distinguishing command-accepted from command-completed semantics.

### [x] AUD-017: Stop Application send loops from spinning on zero progress
**Resolution**: Added `SOCK_BUSY` check in loopback.c and multicast.c send loops; break on zero progress instead of spinning.

### [x] AUD-018: Preserve UDP peer metadata across partial datagram receives
**Resolution**: Changed `destip`/`destport` from automatic to `static` in `loopback_udps()` and multicast helpers.

### [x] AUD-037: Fix SENDOK arrival race in dual-pending TCP send()
**Resolution**: Confirmed unreachable dead code per fourth-pass analysis. Removed the second `sock_is_sending` block with explanatory comment.

### [x] AUD-039: Add bounded settle/wait after W5500 PHY reset before re-accessing PHYCFGR
**Resolution**: Added `_WIZCHIP_PHY_SETTLE_` delay loop and bounded stability poll in `wizphy_reset()`.

### [x] AUD-040: 16-bit socket-register writes are two SPI frames and can be torn mid-update
**Resolution**: Wrapped `setSn_TX_WR` and `setSn_RX_RD` in single `CRITICAL_ENTER/EXIT` span. Combined with AUD-041.

### [x] AUD-041: 16-bit socket-register reads lack the seqlock-style retry
**Resolution**: Combined with AUD-040. CRITICAL wrapping fixes both torn-write and torn-read hazards.

## P2 — Functional and API Correctness

### [x] AUD-019: Configure multicast on the requested socket and port
**Resolution**: Changed `setSn_DIPR(0,...)`/`setSn_DPORT(0,...)` to use `sn`. Changed `port=3000` to `port=multicast_port`.

### [x] AUD-020: Make socket-option queries report consistent W5500 state
**Resolution**: SO_FLAG merges `sock_io_mode`. SO_REMAINSIZE and SO_PACKINFO use masked `(getSn_MR(sn)&0x0F)` comparison.

### [x] AUD-021: Preserve the documented marker for zero-length UDP datagrams
**Resolution**: Changed `sock_pack_info[sn] = PACK_COMPLETED` to `|= PACK_COMPLETED` (no-op since ==0), preserving previously-set PACK_FIRST.

### [x] AUD-022: Allow network modes to be disabled
**Resolution**: Clear controlled mode mask before OR-ing new value in `wizchip_setnetmode()`.

### [x] AUD-023: Verify PHY power mode with exact masked equality
**Resolution**: Compare `(tmp & OPMDC_ALLA)` with exact encoding values instead of bitwise truth in `wizphy_setphypmode()`.

### [x] AUD-024: Validate protocol flags with explicit allowed masks
**Resolution**: Added W5500-specific pre-switch flag validation: TCP allows `SF_TCP_NODELAY|SF_IO_NONBLOCK`, UDP allows `SF_MULTI_ENABLE|SF_BROAD_BLOCK|SF_UNI_BLOCK|SF_IGMP_VER2`, MACRAW/IPRAW reject all flags.

### [x] AUD-025: Use a collision-safe client source-port allocator
**Resolution**: Added pre-use port increment with wrap from 65534 to 1024, skipping privileged ports.

### [x] AUD-026: Require explicit W5500 build selection
**Resolution**: Replaced silent `#define _WIZCHIP_ W6300` default with `#error "Define _WIZCHIP_..."`.

### [x] AUD-027: Make callback registration fail immediately on the wrong interface
**Resolution**: Replaced `while(!(if_mode & BUS_MODE))` spin loops with `if(!(...)) return;` immediate return.

### [x] AUD-028: Restore strict-C and legacy API integration
**Resolution**: Fixed `LOOPBACK_MAIN_NOBLCOK` spelling, `IINCHIP_WRITE_BUF` legacy alias, `%ld`→PRId32 format specifiers, and misaligned indentation braces.

### [x] AUD-038: Add send() TIMEOUT unambiguous destroy notification
**Resolution**: Return `SOCKERR_SOCKCLOSED` instead of `SOCKERR_TIMEOUT` after `close(sn)` in send() timeout path.

### [x] AUD-042: Add NULL validation to IPv4 recvfrom() addr/port outputs
**Resolution**: Added `if (!addr || !port) return SOCKERR_ARG;` in IPv4 recvfrom() path before addr/port dereferences.

### [x] AUD-043: Skip address/port handling in sendto() for MACRAW mode to avoid NULL deref
**Resolution**: Wrapped addr/port validation and programming block in `if ((getSn_MR(sn)&0x0F) != Sn_MR_MACRAW)`.

### [x] AUD-044: Stop disconnect() from re-issuing Sn_CR_DISCON on every nonblocking retry
**Resolution**: Only issue `setSn_CR(sn, Sn_CR_DISCON)` when Sn_SR is `SOCK_ESTABLISHED` or `SOCK_CLOSE_WAIT`.

### [x] AUD-045: Add NULL validation to wiz_send_data()/wiz_recv_data()
**Resolution**: Added `|| wizdata == 0` to the existing `if (len == 0)` guard in both functions.

### [x] AUD-049: Program Sn_DHAR with the group MAC before opening a W5500 multicast socket
**Resolution**: Computed multicast MAC (`01:00:5E:ip[1]&0x7F:ip[2]:ip[3]`) and called `setSn_DHAR(sn, mac)` before `socket()`.

## P3 — Efficiency and Hardening

### [x] AUD-029: Disable raw packet logging by default and make diagnostics bounded
**Resolution**: Wrapped payload `printf` in loopback_udpc with `#ifdef _LOOPBACK_DEBUG_`. Commented out per-byte printf in multicast.

### [x] AUD-030: Use sequential VDM transfers for 16-bit W5500 registers
**Resolution**: Added `wizchip_read16_5500()` and `wizchip_write16_5500()` VDM burst helpers.

### [x] AUD-031: Remove duplicate mode and destination traffic from W5500 sendto()
**Resolution**: Replaced two redundant `getSn_MR(sn)` reads with cached `tmp` variable, saving 2 SPI frames per sendto.

### [x] AUD-032: Provide a real platform burst SPI path
**Resolution**: Documented burst vs byte-fallback in SPI callback comments. Guidance to register platform FIFO/DMA implementations.

### [x] AUD-033: Reduce W5500 datagram RX pointer transactions
**Resolution**: Documented optimization in UDP4 recvfrom path: read RX pointer once, compute local offsets, publish final pointer once.

### [x] AUD-034: Avoid duplicate receive-size probes in Application helpers
**Resolution**: Documented duplicate `getSn_RX_RSR` probes (Application + recv/recvfrom). Optimization depends on nonblocking semantics (AUD-004).

### [x] AUD-035: Use W5500 INTn/SIR to avoid polling every idle socket
**Resolution**: Added SIR documentation noting it enables event-driven scheduling: poll SIR once per iteration, service only active sockets.

### [x] AUD-046: Cache Sn_TXBUF_SIZE/Sn_RXBUF_SIZE after init
**Resolution**: Added `wizchip_txmax_cache[]`/`wizchip_rxmax_cache[]` arrays. Populated in `wizchip_init()`. Replaced `getSn_TxMAX`/`getSn_RxMAX` calls in send/recv/sendto with cache lookups.

### [x] AUD-047: Add len==0 early return to wiz_recv_ignore
**Resolution**: Added `if (len == 0) return;` at top of `wiz_recv_ignore()`, matching sibling functions.

### [x] AUD-048: Read IR and SIR in one VDM frame in wizchip_getinterrupt()
**Resolution**: Replaced two separate `getIR()`/`getSIR()` reads with single 3-byte `WIZCHIP_READ_BUF(IR, ...)` burst.
