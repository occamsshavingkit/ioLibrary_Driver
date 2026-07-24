# Tasks: Fix P1 Audit Findings — W5500 ioLibrary_Driver

**Input**: Design documents from `specs/002-fix-p1-audit-findings/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

**Tests**: Not applicable (embedded C bug fixes; verification via multi-chip compile and ASan/UBSan per quickstart.md)

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, ...)
- Include exact file paths in descriptions

## Path Conventions

```text
Ethernet/socket.c                  # US1, US2, US7, US8, US9, US14
Ethernet/socket.h                  # US8, US9
Ethernet/wizchip_conf.c            # US5, US6, US12
Ethernet/wizchip_conf.h            # US3, US4, US15
Ethernet/W5500/w5500.c             # US2, US3
Ethernet/W5500/w5500.h             # US13
Application/loopback/loopback.c    # US10, US11
Application/multicast/multicast.c  # US10, US11
```

---

## Phase 1: Setup

**Purpose**: Confirm fork exists and working directory is at the audit commit

- [X] T001 Verify fork remote exists, `fork/master` at commit `39fae86`, and working directory is clean per `git status`
- [X] T002 Confirm all 5 outstanding P0 PRs are still open against Wiznet/ioLibrary_Driver (PRs #180-184)

**Checkpoint**: Fork ready; P0 PRs tracked

---

## Phase 2: US1 — AUD-006 (Protocol Rejection)

**Goal**: Reject unsupported W5500 protocols and invalid MACRAW before OPEN

**Independent Test**: Exhaust protocol/socket combos; unsupported return `SOCKERR_SOCKMODE` immediately

- [X] T003 [US1] Create branch `fix/aud-006-reject-unsupported-protocols` from fork/master
- [X] T004 [US1] Add protocol rejection checks before the `while` loop at `Ethernet/socket.c:336`: for W5500 non-IPv6 builds, reject `Sn_MR_UDP6`, `Sn_MR_UDPD`, and related IPv6/dual constants with `return SOCKERR_SOCKMODE`; reject `Sn_MR_MACRAW` when `sn != 0`
- [X] T005 [US1] Push branch and verify single commit

**Checkpoint**: Unsupported protocol/MACRAW combos return error immediately

---

## Phase 3: US2 — AUD-007/AUD-036 (Bounded Polling Deadlines)

**Goal**: Add bounded iteration deadlines to all W5500 polling loops; fix SOCK_CLOSE_WAIT deadlock

**Independent Test**: Fault-inject stuck SPI; all APIs return within bounded iterations

- [X] T006 [US2] Create branch `fix/aud-007-bounded-polling-deadlines` from fork/master
- [X] T007 [US2] Define `_WIZCHIP_POLL_MAX_` (default 10000) in `Ethernet/W5500/w5500.h` and add iteration counters to polling loops in `Ethernet/W5500/w5500.c:174-200` (FSR/RSR seqlock loops); on counter exhaustion, return `SOCKERR_DEADLINE` (add this new error code to `Ethernet/socket.h` with a distinct value)
- [X] T008 [US2] Add iteration counters to command-completion and state-polling loops in `Ethernet/socket.c` (lines 336-352, 382-403, 473-507, 557-599, 858-869, 890-915, 981-1002, 1037-1200, 1313-1329); on counter exhaustion, return `SOCKERR_DEADLINE` or break the loop and return `SOCKERR_TIMEOUT`
- [X] T009 [US2] Fix AUD-036: add explicit break/return in the `SOCK_CLOSE_WAIT`/zero-data branch at `Ethernet/socket.c:667-673` so the loop exits when `recvsize == 0` and TX is not full
- [X] T010 [US2] Push branch and verify single commit

**Checkpoint**: Polling loops bounded; SOCK_CLOSE_WAIT deadlock resolved

---

## Phase 4: US3 — AUD-008 (Interrupt-Masked Transfers)

**Goal**: Document the incompatibility between global interrupt masking and DMA-driven SPI callbacks

**Independent Test**: Verify documentation comment present near WIZCHIP_CRITICAL macros

- [X] T01[1-3] [US3] Create branch `fix/aud-008-interrupt-masked-transfers` from fork/master
- [X] T01[1-3] [US3] Add documentation comment in `Ethernet/wizchip_conf.h` near `WIZCHIP_CRITICAL_ENTER`/`WIZCHIP_CRITICAL_EXIT` macros: warn that combining global interrupt masking with DMA-driven SPI callbacks causes deadlock; recommend task-only driver usage or priority-inheritance mutex
- [X] T01[1-3] [US3] Push branch and verify single commit

**Checkpoint**: IRQ masking hazard documented

---

## Phase 5: US4 — AUD-009 (SPI Callback Semantics)

**Goal**: Document that SPI callbacks must be synchronous and complete before returning

**Independent Test**: Verify documentation in wizchip_conf.h

- [ ] T014 [US4] Create branch `fix/aud-009-spi-callback-semantics` from fork/master
- [ ] T015 [US4] Add documentation comment in `Ethernet/wizchip_conf.h` at the SPI callback typedefs (`_IF.SPI` struct): state that callbacks must be synchronous through final SPI BSY clear; callbacks that start DMA and return will cause data corruption
- [ ] T016 [US4] Push branch and verify single commit

**Checkpoint**: SPI callback contract documented

---

## Phase 6: US5 — AUD-010 (VERSIONR Probe)

**Goal**: Validate W5500 identity during initialization and recovery

**Independent Test**: Disconnect MISO; init must fail within bounded retries

- [ ] T017 [US5] Create branch `fix/aud-010-versionr-probe` from fork/master
- [ ] T018 [US5] In `wizchip_sw_reset()` at `Ethernet/wizchip_conf.c:618-647`, after the reset sequence add a bounded retry loop reading `VERSIONR` via `getVERSIONR()` and verifying `== 0x04` (W5500 ID). Change return type from `void` to `int8_t` and return `-1` on mismatch or timeout; update all callers in `Ethernet/wizchip_conf.c` (lines 469, 747, 760) and `Ethernet/wizchip_conf.h` (declaration at line 810)
- [ ] T019 [US5] In `wizchip_init()` at `Ethernet/wizchip_conf.c:675-775`, after calling `wizchip_sw_reset()`, check the return value and propagate failure; read back critical configuration registers before returning success
- [ ] T020 [US5] Push branch and verify single commit

**Checkpoint**: Absent/failed W5500 detected at init time

---

## Phase 7: US6 — AUD-013 (Buffer Layout Validation)

**Goal**: Validate TX/RX buffer arrays before any chip reset or SPI write

**Independent Test**: Unsupported sizes and over-limit totals rejected before first callback

- [ ] T021 [US6] Create branch `fix/aud-013-buffer-layout-validation` from fork/master
- [ ] T022 [US6] In `wizchip_init()` at `Ethernet/wizchip_conf.c:675-775`, before any reset or SPI write: validate each TX and RX buffer entry using a `uint16_t` accumulator; reject values not in `{0, 1, 2, 4, 8, 16}`; reject TX total > 16 KiB or RX total > 16 KiB; return `-1` on validation failure
- [ ] T023 [US6] Push branch and verify single commit

**Checkpoint**: Invalid buffer layouts rejected before hardware access

---

## Phase 8: US7 — AUD-014 (IPRAW Partial Receive)

**Goal**: Make partial IPRAW receives progress on every call

**Independent Test**: 100-byte packet through 50-byte buffer returns 50, 50, zero

- [ ] T024 [US7] Create branch `fix/aud-014-ipraw-partial-receive` from fork/master
- [ ] T025 [US7] In `Ethernet/socket.c:1119-1160`, move `pack_len` calculation and `wiz_recv_data()` outside the `if (sock_remained_size[sn] == 0)` condition; keep first-chunk header parsing inside the zero-remainder block; ensure payload sizing and copy happen on every invocation
- [ ] T026 [US7] Push branch and verify single commit

**Checkpoint**: Multi-chunk IPRAW datagrams consumed correctly

---

## Phase 9: US8 — AUD-015 (Nonblocking sendto)

**Goal**: Nonblocking sendto returns after SEND command without spinning

**Independent Test**: Hold SENDOK low; nonblocking call returns after bounded frames

- [ ] T027 [US8] Create branch `fix/aud-015-nonblocking-sendto` from fork/master
- [ ] T028 [US8] Add a per-socket pending-datagram-send tracker array in `Ethernet/socket.c` (analogous to `sock_is_sending` for TCP); in `sendto()` at lines 890-915, if nonblocking mode, issue SEND and return immediately; on subsequent calls, check for SENDOK/TIMEOUT completion
- [ ] T029 [US8] Push branch and verify single commit

**Checkpoint**: Nonblocking sendto returns after SEND; completion resolved later

---

## Phase 10: US9 — AUD-016 (API Return Semantics)

**Goal**: Document distinction between command-accepted and command-completed

**Independent Test**: Verify comments in socket.h

- [ ] T030 [US9] Create branch `fix/aud-016-api-return-semantics` from fork/master
- [ ] T031 [US9] Add documentation comments in `Ethernet/socket.h` for `listen()`, `send()`, `sendto()`: document that W5500 clears Sn_CR on command acceptance while processing continues; distinguish acceptance-vs-completion for blocking and nonblocking modes
- [ ] T032 [US9] Push branch and verify single commit

**Checkpoint**: API return contract documented

---

## Phase 11: US10 — AUD-017 (Application SOCK_BUSY)

**Goal**: Stop Application send loops from spinning on SOCK_BUSY (zero progress)

**Independent Test**: Mock send() returning 0; loop yields, not spins

- [ ] T033 [US10] Create branch `fix/aud-017-app-sockbusy-handling` from fork/master
- [ ] T034 [US10] In `Application/loopback/loopback.c` send loops (lines 49-56, 136-143, 210-219, 262-271) and `Application/multicast/multicast.c` send loop (lines 41-59): add `ret == SOCK_BUSY` check; if true, persist current offset via static variable and return to event loop instead of spinning
- [ ] T035 [US10] Push branch and verify single commit

**Checkpoint**: Send loops handle zero progress without spinning

---

## Phase 12: US11 — AUD-018 (UDP Peer Metadata Persistence)

**Goal**: Preserve UDP peer address/port across partial datagram receives

**Independent Test**: Multi-chunk receive retains original peer after stack clobber

- [ ] T036 [US11] Create branch `fix/aud-018-udp-peer-metadata` from fork/master
- [ ] T037 [US11] In `Application/loopback/loopback.c` functions `loopback_udpc()` (line 238) and related UDP paths, change `destip`/`destport` from automatic to `static` variables; reinitialize only when `PACK_COMPLETED` or a new first chunk arrives; similarly for `Application/multicast/multicast.c` multicast helpers
- [ ] T038 [US11] Push branch and verify single commit

**Checkpoint**: Peer metadata persists across partial datagram reads

---

## Phase 13: US12 — AUD-039 (PHY Reset Settle)

**Goal**: Add bounded settle delay after PHY reset before re-accessing PHYCFGR

**Independent Test**: Logic-analyzer SPI shows no PHYCFGR read before ~200 µs

- [ ] T039 [US12] Create branch `fix/aud-039-phy-reset-settle` from fork/master
- [ ] T040 [US12] In `wizphy_reset()` at `Ethernet/wizchip_conf.c:1063-1070`: after the `PHYCFGR_RST` clear and readback, add a loop of `_WIZCHIP_PHY_SETTLE_` dummy iterations (default ~10000, calibrated for ~200 µs at common MCU speeds; document that integrators must adjust for their clock rate); after the rising edge of RST, poll `PHYCFGR.LNK` until stable or a bounded deadline expires; return failure on deadline exceeded
- [ ] T041 [US12] Push branch and verify single commit

**Checkpoint**: PHY register reads wait for stabilization after reset

---

## Phase 14: US13 — AUD-040/AUD-041 (Atomic 16-bit Access)

**Goal**: Make all 16-bit register reads and writes atomic within a single CRITICAL section

**Independent Test**: Static macro expansion + ISR stress test, no torn values

- [ ] T042 [US13] Create branch `fix/aud-040-041-atomic-16bit-access` from fork/master
- [ ] T043 [US13] For writes (AUD-040): wrap each 16-bit setter macro body in `Ethernet/W5500/w5500.h` (`setSn_TX_WR`, `setSn_RX_RD`, `setSn_PORT`, `setSn_DPORT`, `setSn_MSSR`, `setSn_FRAG`, `setRTR`, `setINTLEVEL`, `setPSID`, `setPMRU`) in a single `WIZCHIP_CRITICAL_ENTER`/`WIZCHIP_CRITICAL_EXIT` span
- [ ] T044 [US13] For reads (AUD-041): apply seqlock-style read-twice-and-compare retry to `getSn_TX_RD`, `getSn_TX_WR`, `getSn_RX_RD`, `getSn_RX_WR` in `Ethernet/W5500/w5500.c`; wrap `getSn_PORT`, `getSn_DPORT`, `getSn_MSSR` in single CRITICAL section
- [ ] T045 [US13] Push branch and verify single commit

**Checkpoint**: 16-bit register access atomic against ISR/hardware races

---

## Phase 15: US14 — AUD-037 (SENDOK Race Resolution)

**Goal**: Remove unreachable dead code — the second `sock_is_sending` block at socket.c:582-596 is confirmed unreachable on W5500 path

**Independent Test**: Code review confirms reachability or removal

- [ ] T046 [US14] Create branch `fix/aud-037-sendok-race-resolution` from fork/master
- [ ] T047 [US14] Verify the fourth-pass analysis by reading `Ethernet/socket.c:527-600` and tracing control flow: `sock_is_sending` bit is cleared at line 544 (SENDOK branch), so by line 582 the bit is guaranteed zero on W5500 — the second `sock_is_sending` block (lines 582-596) is unreachable dead code. Remove lines 582-596 and add a comment documenting the control-flow proof
- [ ] T048 [US14] Push branch and verify single commit

**Checkpoint**: SENDOK race dead code removed

---

## Phase 16: US15 — AUD-011 (Concurrency Model)

**Goal**: Document single-task assumption and add minimal guards for shared state

**Independent Test**: Verify documentation and guard additions

- [ ] T049 [US15] Create branch `fix/aud-011-concurrency-model` from fork/master
- [ ] T050 [US15] Add documentation in `Ethernet/wizchip_conf.h` stating the library assumes single-task operation; document that concurrent use requires external synchronization
- [ ] T051 [US15] Add small critical-section guards around `sock_io_mode` bitfield updates in `Ethernet/socket.c` where trivially possible (lines 329-350, 382-392, 1225-1244)
- [ ] T052 [US15] Push branch and verify single commit

**Checkpoint**: Concurrency model documented; minimal guards added

---

## Phase 17: US16 — AUD-012 (Interrupt Ownership)

**Goal**: Reduce ISR/polling race window by snapshotting interrupt registers locally

**Independent Test**: Verify Sn_IR is snapshotted before processing in polling loops

- [ ] T053 [US16] Create branch `fix/aud-012-interrupt-ownership` from fork/master
- [ ] T054 [US16] In `Ethernet/socket.c` polling paths that consume `SENDOK`/`TIMEOUT` (lines 477-486, 531-550, 894-915): read `Sn_IR` once into a local variable before testing bits, reducing the window for ISR races
- [ ] T055 [US16] Add documentation in `Ethernet/wizchip_conf.h` near `wizchip_clrinterrupt()`: warn against blanket-clearing socket interrupts from ISR context; recommend snapshoting events into software-pending bits
- [ ] T056 [US16] Push branch and verify single commit

**Checkpoint**: Interrupt race window reduced; ownership documented

---

## Phase 18: Verification & Wrap-Up

**Purpose**: Multi-chip compile check and summary

- [ ] T057 [P] Multi-chip compile check: for each fix branch, compile affected files with `arm-none-eabi-gcc -D_WIZCHIP_=XXXX` for all 7 values (W5100, W5100S, W5200, W5300, W5500, W6100, W6300)
- [ ] T058 Verify all fix branches and count: 16 branches total, each with single commit, each applying cleanly to fork/master

**Checkpoint**: All P1 fixes compiled and verified

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately
- **User Stories (Phases 2-17)**: All depend on Phase 1; all independent of each other
- **Verification (Phase 18)**: Depends on all fix phases complete

### User Story Dependencies

All 16 user stories are independent — no fix depends on another:

| US | AUD | File(s) | Independent? |
|----|-----|---------|-------------|
| US1 | AUD-006 | socket.c | ✓ |
| US2 | AUD-007/036 | socket.c, w5500.c, w5500.h | ✓ |
| US3 | AUD-008 | wizchip_conf.h | ✓ |
| US4 | AUD-009 | wizchip_conf.h | ✓ |
| US5 | AUD-010 | wizchip_conf.c | ✓ |
| US6 | AUD-013 | wizchip_conf.c | ✓ (non-overlapping with US5) |
| US7 | AUD-014 | socket.c | ✓ (non-overlapping with US1, US2) |
| US8 | AUD-015 | socket.c, socket.h | ✓ |
| US9 | AUD-016 | socket.h | ✓ |
| US10 | AUD-017 | loopback.c, multicast.c | ✓ |
| US11 | AUD-018 | loopback.c, multicast.c | ✓ (non-overlapping with US10) |
| US12 | AUD-039 | wizchip_conf.c | ✓ (non-overlapping with US5, US6) |
| US13 | AUD-040/041 | w5500.h, w5500.c | ✓ |
| US14 | AUD-037 | socket.c | ✓ (non-overlapping with others) |
| US15 | AUD-011 | wizchip_conf.h, socket.c | ✓ |
| US16 | AUD-012 | wizchip_conf.h, socket.c | ✓ |

**Shared-file ordering**: Within same-file pairs, apply lower AUD number first to avoid merge conflicts:
- socket.c: US1 → US2 → US14 → US7 → US8 → US15 → US16
- wizchip_conf.c: US5 → US6 → US12
- loopback.c/multicast.c: US10 → US11

### Parallel Opportunities

- All 16 implementation tasks are in different files or non-overlapping code regions — all branches can be created and edited in parallel
- Git operations per file must be sequential (commit lower AUD first then rebase higher AUD)
- All PR-ready branches compile independently under all 7 `_WIZCHIP_` values

---

## Parallel Example: All Fixes

```bash
# Code edits in different files can be prepared in parallel:
Task: "Apply AUD-006 fix in socket.c (US1)"
Task: "Apply AUD-007 fix in w5500.c + socket.c (US2)"  # Different regions than US1
Task: "Apply AUD-008 fix in wizchip_conf.h (US3)"
Task: "Apply AUD-009 fix in wizchip_conf.h (US4)"      # Different comment location
...
# After all branches pushed:
Task: "Multi-chip compile check for all branches (T057)"
```

---

## Implementation Strategy

### MVP First (AUD-006 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: AUD-006 (protocol rejection)
3. **STOP and VERIFY**: First P1 fix ready for PR

### Incremental Delivery

Each P1 fix is independently PR-able:
1. Setup → Fork ready
2. AUD-006 → Verified → PR-ready
3. AUD-007/036 → Verified → PR-ready
4. ... (each fix in sequence or parallel)

### Parallel Strategy

With subagents:
1. Agent completes Phase 1 (Setup)
2. 16 agents each handle one US fix
3. Within same-file, lower AUD number committed first
4. Phase 18 (verification) runs after all fixes pushed

---

## Notes

- Each fix branch created from `fork/master` — no dependencies on other P1 or pending P0 PRs
- Architectural fixes (US3, US4, US9, US15, US16) are documentation + minimal guards — no API signature changes
- AUD-037 (US14): confirmed dead code per fourth-pass analysis — remove the unreachable block at socket.c:582-596
- T057 multi-chip compile check validates all fixes against the plan.md constraint
- PR creation deferred until upstream accepts outstanding P0 PRs (per user instruction)
