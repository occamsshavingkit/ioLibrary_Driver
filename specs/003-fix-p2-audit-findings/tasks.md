# Tasks: Fix P2 Audit Findings — W5500 ioLibrary_Driver

**Input**: Design documents from `specs/003-fix-p2-audit-findings/`

**Prerequisites**: plan.md, spec.md, research.md, quickstart.md

## Format: `[ID] [P?] [Story] Description`

## Phase 1: Setup

- [X] T001 Verify fork exists, fork/master at 39fae86, working directory clean
- [X] T002 Confirm P0 PRs (#180-184) and P1 branches still present

## Phase 2: AUD-019 — Multicast on correct socket and port

- [X] T00[345] [US1] Create branch `fix/aud-019-multicast-correct-socket` from fork/master
- [X] T00[345] [US1] Fix `Application/multicast/multicast.c:67-70` to use `sn` instead of hardcoded 0 for Sn_DIPR/DPORT writes; change local `port = 3000` (lines 11, 86) to use the supplied multicast port parameter
- [X] T00[345] [US1] Push branch

## Phase 3: AUD-020 — Consistent socket-option queries

- [X] T00[678] [US2] Create branch `fix/aud-020-consistent-sockopt` from fork/master
- [X] T00[678] [US2] Fix `Ethernet/socket.c`: SO_FLAG (line 1347) merge `sock_io_mode` into result; SO_REMAINSIZE (line 1424) compare `(getSn_MR(sn) & 0x0F)` with exact protocol values; SO_PACKINFO (line 1431) compare masked mode byte
- [X] T00[678] [US2] Push branch

## Phase 4: AUD-021 — Zero-length UDP datagram marker

- [X] - [ ] T009 [US3] Create branch `fix/aud-021-zero-udp-marker` from fork/master
- [X] - [ ] T010 [US3] In `Ethernet/socket.c:1198-1213`, preserve PACK_FIRST for zero-length UDP datagrams instead of overwriting with PACK_COMPLETED==0
- [X] - [ ] T011 [US3] Push branch

## Phase 5: AUD-022 — Network mode disable

- [ ] T012 [US4] Create branch `fix/aud-022-network-mode-disable` from fork/master
- [ ] T013 [US4] In `Ethernet/wizchip_conf.c:1404-1418` wizchip_setnetmode(), replace mode bits using clear-then-OR: preserve unrelated MR bits, clear controlled mask, then OR requested value
- [ ] T014 [US4] Push branch

## Phase 6: AUD-023 — PHY power mode exact equality

- [ ] T015 [US5] Create branch `fix/aud-023-phy-powermode-equality` from fork/master
- [ ] T016 [US5] In `Ethernet/wizchip_conf.c:1141-1165` wizphy_setphypmode(), compare `(tmp & PHYCFGR_OPMDC_ALLA)` to requested encoding with exact equality instead of bitwise truth
- [ ] T017 [US5] Push branch

## Phase 7: AUD-024 — Protocol flag validation

- [ ] T018 [US6] Create branch `fix/aud-024-protocol-flag-validation` from fork/master
- [ ] T019 [US6] In `Ethernet/socket.c:251-317`, define explicit allowed mask per W5500 protocol (e.g., TCP: SF_TCP_NODELAY; UDP: SF_MULTI_ENABLE|SF_BROAD_BLOCK|SF_UNI_BLOCK; MACRAW: SF_MAC_RAW_ONLY; etc.) and reject `flag & ~allowed_mask` before setSn_MR()
- [ ] T020 [US6] Push branch

## Phase 8: AUD-025 — Collision-safe port allocator

- [ ] T021 [US7] Create branch `fix/aud-025-port-allocator` from fork/master
- [ ] T022 [US7] In `Application/loopback/loopback.c`, fix any_port allocator: increment before use, wrap from 65535 to 1024 (skip privileged ports), check for duplicates. Fix UDP static 50000 to use same allocator
- [ ] T023 [US7] Push branch

## Phase 9: AUD-026 — Explicit build selection

- [ ] T024 [US8] Create branch `fix/aud-026-explicit-build` from fork/master
- [ ] T025 [US8] In `Ethernet/wizchip_conf.h:77-80`, replace silent default of `_WIZCHIP_` to W6300 with `#error "Define _WIZCHIP_ to your WIZnet chip (e.g., W5500) before building"`
- [ ] T026 [US8] Push branch

## Phase 10: AUD-027 — Immediate callback registration failure

- [ ] T027 [US9] Create branch `fix/aud-027-callback-registration-failure` from fork/master
- [ ] T028 [US9] In `Ethernet/wizchip_conf.c:309-338`, replace spin-on-false with immediate `return -1` for wrong-interface registration APIs (reg_wizchip_bus_cbfunc, reg_wizchip_busbuf_cbfunc in SPI builds)
- [ ] T029 [US9] Push branch

## Phase 11: AUD-028 — Strict-C and legacy API compliance

- [ ] T030 [US10] Create branch `fix/aud-028-strict-c-compliance` from fork/master
- [ ] T031 [US10] Fix `Ethernet/W5500/w5500.h:78-81`: map IINCHIP_WRITE_BUF to WIZCHIP_WRITE_BUF. Fix `Application/loopback/loopback.c:7`: correct LOOPBACK_MAIN_NOBLCOK to LOOPBACK_MAIN_NOBLOCK. Add braces around misleading-indentation body at `Ethernet/socket.c:1036-1054`. Fix `%ld` with int32_t → use PRId32 from inttypes.h. Keep private prototypes in socket.c
- [ ] T032 [US10] Push branch

## Phase 12: AUD-038 — Unambiguous send() timeout notification

- [ ] T033 [US11] Create branch `fix/aud-038-send-timeout-destroy-notify` from fork/master
- [ ] T034 [US11] In `Ethernet/socket.c:545-547`, after `close(sn)` on send timeout, return `SOCKERR_SOCKCLOSED` instead of `SOCKERR_TIMEOUT` to unambiguously indicate socket destruction
- [ ] T035 [US11] Push branch

## Phase 13: AUD-042 — NULL validation for recvfrom() addr/port

- [ ] T036 [US12] Create branch `fix/aud-042-recvfrom-null-validation` from fork/master
- [ ] T037 [US12] In `Ethernet/socket.c:1054` (UDP4) and line 1126 (IPRAW4), add `if (!addr || !port) return SOCKERR_ARG;` before dereferences, mirroring IPv6 guard at line 1019
- [ ] T038 [US12] Push branch

## Phase 14: AUD-043 — MACRAW sendto() NULL deref prevention

- [ ] T039 [US13] Create branch `fix/aud-043-macraw-sendto-null-deref` from fork/master
- [ ] T040 [US13] In `Ethernet/socket.c:821-850`, when `getSn_MR(sn) == Sn_MR_MACRAW` (cached as `tmp` at line 794), skip the entire address-validation/programming block in the W5500 `#ifndef IPV6_AVAILABLE` section
- [ ] T041 [US13] Push branch

## Phase 15: AUD-044 — Single-issue disconnect() on nonblocking retry

- [ ] T042 [US14] Create branch `fix/aud-044-disconnect-single-issue` from fork/master
- [ ] T043 [US14] In `Ethernet/socket.c:491-510`, add per-socket in-progress disconnect bit (analogous to sock_is_sending). Issue Sn_CR_DISCON only when no disconnect in flight; re-entry polls Sn_IR_TIMEOUT and returns status
- [ ] T044 [US14] Push branch

## Phase 16: AUD-045 — NULL guard for wiz_send_data/wiz_recv_data

- [ ] T045 [US15] Create branch `fix/aud-045-wizdata-null-guard` from fork/master
- [ ] T046 [US15] In `Ethernet/W5500/w5500.c:203` (wiz_send_data) and line 221 (wiz_recv_data), add `if (!wizdata && len > 0) return;` before dereferencing wizdata
- [ ] T047 [US15] Push branch

## Phase 17: AUD-049 — Sn_DHAR programming before multicast OPEN

- [ ] T048 [US16] Create branch `fix/aud-049-multicast-dhar` from fork/master
- [ ] T049 [US16] In `Application/multicast/multicast.c:63-71` and `126-134`, before `socket(sn, Sn_MR_UDP, port, Sn_MR_MULTI)`, compute group MAC from multicast_ip (`{0x01,0x00,0x5E, ip[1]&0x7F, ip[2], ip[3]}`) and call `setSn_DHAR(sn, mac)`
- [ ] T050 [US16] Push branch

## Phase 18: Verification

- [ ] T051 Multi-chip compile check: verify all 16 branches compile for W5500
- [ ] T052 Verify all 16 branches: single commit each, clean apply to fork/master

## Dependencies

All 16 stories independent. No cross-fix dependencies. All branch from fork/master.

## Parallel Strategy

All 16 fix tasks touch different files/non-overlapping regions. All branches can be created and worked on in parallel.

## Notes

- Each fix: 1 branch, 1 commit, fork/master base
- AUD-028 is multi-issue cumulative fix in one branch
- PR creation deferred per user instruction
