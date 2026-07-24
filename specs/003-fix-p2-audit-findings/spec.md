# Feature Specification: Fix P2 Audit Findings — W5500 ioLibrary_Driver

**Feature Branch**: `003-fix-p2-audit-findings`

**Created**: 2026-07-18

**Status**: Draft

**Input**: User description: "fix the P2 findings in @TODO.md : make sure each fix for an audit finding can be cleanly applied to current main with the intent to open a PR individually for each fix when wiznet accepts the current outstanding PRs."

## User Scenarios & Testing *(mandatory)*

P2 findings cover functional correctness, API consistency, and integration robustness. Each finding is a self-contained fix applicable to a single branch from fork/master.

### AUD-019: Multicast on correct socket and port

Multicast setup helpers must configure the caller-selected socket and the supplied group port, not hardcoded socket 0 and port 3000.

**Independent Test**: Exercise sockets 0-7 and several group ports; Sn_DIPR, Sn_DPORT, Sn_PORT must match request.

### AUD-020: Consistent socket-option queries

SO_FLAG must include software nonblocking state; SO_REMAINSIZE must correctly classify IPRAW; SO_PACKINFO must compare masked mode byte.

**Independent Test**: Query every option across TCP, UDP, IPRAW, MACRAW, both I/O modes.

### AUD-021: Zero-length UDP datagram marker

PACK_FIRST must be preserved for a zero-length UDP datagram per socket.h:596-599 contract.

**Independent Test**: Receive UDP-header-only packet; return must be 0 with PACK_FIRST marker.

### AUD-022: Network mode disable

wizchip_setnetmode() must clear the controlled mode bits before OR-ing in the requested value.

**Independent Test**: Enable and disable every supported mode independently; verify exact MR state.

### AUD-023: PHY power mode exact equality

wizphy_setphypmode() must compare masked OPMDC field for exact equality, not bitwise truth.

**Independent Test**: Simulate accepted and rejected writes with alternate OPMDC values.

### AUD-024: Protocol flag validation

Each W5500 protocol must reject flags outside its explicitly allowed mask.

**Independent Test**: Pair every protocol with every flag; only documented combinations accepted.

### AUD-025: Collision-safe port allocator

Application source-port allocation must handle wrap, avoid zero/privileged ports, and prevent duplicates.

**Independent Test**: Open clients across all sockets; no zero, privileged, or duplicate ports.

### AUD-026: Explicit build selection

Compilation without _WIZCHIP_ must fail with a clear diagnostic instead of silently defaulting to W6300.

**Independent Test**: Build without _WIZCHIP_ fails; W5500 build proves correct chip selection.

### AUD-027: Immediate callback registration failure

Incompatible registration APIs must return an error instead of spinning on compile-time-false checks.

**Independent Test**: Call every registration API; immediate success or error returned without hang.

### AUD-028: Strict-C and legacy API compliance

Legacy aliases must compile; strict warning builds must pass; format mismatches must be fixed.

**Independent Test**: Compile with GCC and Clang under C99/C11 with -Wall -Wextra -Wpedantic -Wundef -Wformat=2 -Werror.

### AUD-038: Unambiguous send() timeout notification

send() timeout after socket close must return a distinct error from general SOCKERR_TIMEOUT.

**Independent Test**: Trigger TCP send timeout; verify returned error unambiguously indicates destruction.

### AUD-042: NULL validation for recvfrom() addr/port

IPv4 recvfrom() paths must check addr/port for NULL like the IPv6 path does.

**Independent Test**: recvfrom(sn, buf, 100, NULL, NULL) returns SOCKERR_ARG without fault.

### AUD-043: MACRAW sendto() NULL deref prevention

sendto() in MACRAW mode must skip addr/port validation when addr is NULL.

**Independent Test**: socket(0, Sn_MR_MACRAW, 0, 0); sendto(0, frame, 60, NULL, 0); succeeds.

### AUD-044: Single-issue disconnect() on nonblocking retry

disconnect() must track in-progress state and not re-issue Sn_CR_DISCON on retries.

**Independent Test**: 100 disconnect() calls in tight loop; SPI capture shows exactly 1 DISCON.

### AUD-045: NULL guard for wiz_send_data/wiz_recv_data

Exported driver functions must guard against NULL data pointer with non-zero length.

**Independent Test**: wiz_send_data(0, NULL, 10) returns without fault under ASan.

### AUD-049: Sn_DHAR programming before multicast OPEN

Multicast socket helpers must compute and write the group MAC to Sn_DHAR before OPEN.

**Independent Test**: Hardware-in-loop: join group with and without DHAR; recept must require DHAR.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001 (AUD-019)**: Multicast helpers must use caller-selected `sn` and group port for all Sn_DIPR, Sn_DPORT, Sn_PORT writes.
- **FR-002 (AUD-020)**: SO_FLAG, SO_REMAINSIZE, and SO_PACKINFO must report accurate state for all W5500 protocol modes.
- **FR-003 (AUD-021)**: Zero-length UDP datagrams must preserve PACK_FIRST in the completion path.
- **FR-004 (AUD-022)**: wizchip_setnetmode() must clear the controlled mode mask before OR-ing the requested value.
- **FR-005 (AUD-023)**: wizphy_setphypmode() must compare masked OPMDC field for exact equality, not bitwise truth.
- **FR-006 (AUD-024)**: Each W5500 protocol must validate flags against an explicit allowed mask, rejecting extra bits.
- **FR-007 (AUD-025)**: Application source-port allocation must be collision-safe with proper wrap handling.
- **FR-008 (AUD-026)**: Building without _WIZCHIP_ must produce a clear compile error instead of silently defaulting.
- **FR-009 (AUD-027)**: Callback registration for the wrong interface mode must return an error immediately instead of spinning.
- **FR-010 (AUD-028)**: Strict-C compilation must pass; legacy API aliases must function; format/indentation warnings must be fixed.
- **FR-011 (AUD-038)**: send() timeout after socket close must return a distinct error code from SOCKERR_TIMEOUT.
- **FR-012 (AUD-042)**: recvfrom() IPv4 paths must validate addr and port for NULL before dereferencing.
- **FR-013 (AUD-043)**: sendto() must skip addr/port handling in MACRAW mode to avoid NULL dereference.
- **FR-014 (AUD-044)**: disconnect() must track in-progress state and not re-issue Sn_CR_DISCON on retries.
- **FR-015 (AUD-045)**: wiz_send_data() and wiz_recv_data() must guard against NULL wizdata with non-zero len.
- **FR-016 (AUD-049)**: Multicast helpers must program Sn_DHAR with the group MAC before OPEN.
- **FR-017**: Each fix must be on a separate branch from fork/master, compile under all _WIZCHIP_ values, and be independently PR-able.

## Success Criteria *(mandatory)*

- **SC-001**: All 16 P2 findings have fix branches, each with single commit, each compiling for W5500.
- **SC-002**: Each fix passes the verification criteria in its TODO.md AUD entry.
- **SC-003**: Each branch applies cleanly to fork/master and can be submitted as an independent PR.
- **SC-004**: AUD-028 build passes with -Wall -Wextra -Wpedantic -Wundef -Wformat=2 -Werror under C99 and C11.

## Assumptions

- The fork occamsshavingkit/ioLibrary_Driver exists with the audit commit as fork/master.
- P0 and P1 fix branches do not block P2 work.
- AUD-049 requires hardware-in-loop for full verification; unit-level correctness of DHAR computation is sufficient for this feature.
- AUD-028 fixes are cumulative across multiple individual issues (legacy alias, macro spelling, format specifiers, etc.) in one branch per the finding.
