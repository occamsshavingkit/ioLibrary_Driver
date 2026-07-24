# Feature Specification: Fix P0 Audit Findings — W5500 ioLibrary_Driver

**Feature Branch**: `001-fix-p0-audit-findings`

**Created**: 2026-07-18

**Status**: Draft

**Input**: User description: "fix the P0 findings in @TODO.md : open up our own fork in github. Put each fix separately to the original repo as a PR branched off its main."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Fix AUD-001 (P1): UDP loopback validates recvfrom return before buffer access

An embedded developer running the W5500 loopback application with a UDP socket should never experience a stack-buffer-overflow when a full-capacity datagram arrives, nor a stack-buffer-underflow when recvfrom() returns a negative error code such as SOCKERR_SOCKCLOSED.

**Why this priority**: This is a deployment blocker. Any negative return or full-capacity datagram from recvfrom() causes an out-of-bounds memory write, which can corrupt adjacent stack data or fault the MCU.

**Independent Test**: With a mocked recvfrom() returning DATA_BUF_SIZE, -13, or 0, the loopback_udpc() function must return without writing to buf beyond its allocation. Run under ASan/UBSan on host.

**Acceptance Scenarios**:

1. **Given** a UDP socket receives a full DATA_BUF_SIZE datagram, **When** loopback_udpc() calls recvfrom(), **Then** no byte is written past the DATA_BUF_SIZE buffer and the function returns an error or continues safely.
2. **Given** recvfrom() returns a negative error (e.g. SOCKERR_SOCKCLOSED), **When** loopback_udpc() processes the return, **Then** no byte is written at a negative index into buf, and the error propagates to the caller.

---

### User Story 2 - Fix AUD-002 (P2): SO_KEEPALIVEAUTO getter uses correct output width

An integrator calling getsockopt(sn, SO_KEEPALIVEAUTO, &value) with a uint8_t destination must not have adjacent memory overwritten, and the call must not fault on strict-alignment MCUs.

**Why this priority**: Deployment blocker. The mismatch between the documented uint8_t result and the uint16_t* write causes memory corruption on the caller's stack.

**Independent Test**: Place canaries around a uint8_t variable, call getsockopt() with SO_KEEPALIVEAUTO, and verify the canaries are intact. Run on a strict-alignment target.

**Acceptance Scenarios**:

1. **Given** a caller passes a uint8_t variable to getsockopt(SO_KEEPALIVEAUTO), **When** the getter executes, **Then** only one byte is written to the destination and no adjacent bytes are modified.

---

### User Story 3 - Fix AUD-003 (P3): SPI callback members initialized for W5500 SPI mode

In a W5500 build with SPI interface, calling WIZCHIP_READ() before any callback registration must not invoke incompatible BUS callback signatures. The SPI union member must be initialized with type-correct, fail-closed stubs.

**Why this priority**: Deployment blocker. Any W5500 access before SPI callback registration invokes incompatible function signatures through the wrong union member, which can HardFault or corrupt memory on common ARM ABIs.

**Independent Test**: In a W5500 build, call WIZCHIP_READ(MR) before registration. It must return a deterministic error without accessing the BUS or faulting.

**Acceptance Scenarios**:

1. **Given** a W5500 SPI build with no callbacks registered, **When** WIZCHIP_READ() is called, **Then** no BUS callback is invoked, no HardFault occurs, and the function returns 0x00 (an all-zero SPI read via the unregistered stub, distinguishable from valid W5500 register values).

---

### User Story 4 - Fix AUD-004 (P4): Nonblocking W5500 TCP recv() returns available data

A nonblocking TCP socket with data in the RX buffer must return that data on recv(). Currently it returns SOCK_BUSY before checking whether data is available, which prevents the RX pointer from advancing and eventually stalls the buffer.

**Why this priority**: Deployment blocker. A nonblocking TCP socket that has available receive data but always returns SOCK_BUSY can never drain, causing a permanent receive stall.

**Independent Test**: Open a W5500 TCP socket with SF_IO_NONBLOCK, inject 1 byte into the receive buffer, and call recv(). It must return 1 byte. An empty socket must still return SOCK_BUSY.

**Acceptance Scenarios**:

1. **Given** a nonblocking TCP socket with RX data available, **When** recv() is called, **Then** the available data is returned and the RX pointer advances.
2. **Given** a nonblocking TCP socket with no RX data, **When** recv() is called, **Then** SOCK_BUSY is returned within a bounded number of SPI frame iterations (fewer than 5 polling loop cycles, matching the single-register read check).

---

### User Story 5 - Fix AUD-005 (P5): ctlwizchip() does not dereference NULL arguments

Calling ctlwizchip(CW_RESET_WIZCHIP, NULL) or ctlwizchip(CW_INIT_WIZCHIP, NULL) must not dereference NULL, matching the documented API contract that these commands accept no argument or explicitly support NULL.

**Why this priority**: Deployment blocker. Documented no-argument calls can dereference NULL if the compiler does not optimize away the unconditional dereference at the top of ctlwizchip().

**Independent Test**: Call ctlwizchip(CW_RESET_WIZCHIP, NULL) and ctlwizchip(CW_INIT_WIZCHIP, NULL) at -O0 and -O2. Neither may fault. Argument-requiring commands called with NULL must return a clean error.

**Acceptance Scenarios**:

1. **Given** ctlwizchip(CW_RESET_WIZCHIP, NULL) is called, **When** the function dispatches, **Then** the reset proceeds without dereferencing the NULL argument.
2. **Given** ctlwizchip(CW_INIT_WIZCHIP, NULL) is called, **When** the function dispatches, **Then** default socket buffer sizes are used without faulting.
3. **Given** a command that requires an argument is called with NULL, **When** the function dispatches, **Then** a defined error is returned.

---

### Edge Cases

- What happens when recvfrom() returns exactly DATA_BUF_SIZE? Must not write beyond buffer.
- What happens when a uint8_t aligned at the end of a memory region is passed to the SO_KEEPALIVEAUTO getter? Must not cross into unmapped memory.
- What happens when WIZCHIP_READ is called before any SPI callback registration on a W5500 MCU? Must not HardFault.
- What happens when a nonblocking TCP socket has exactly 0 bytes available? Must return SOCK_BUSY, not hang or return SOCK_OK.
- What happens when ctlwizchip() is called with NULL at -O0 vs -O2? Must not depend on optimizer behavior for safety.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The loopback_udpc() function MUST validate the recvfrom() return value before using it as an array index or writing a null terminator into buf.
- **FR-002**: The getsockopt() SO_KEEPALIVEAUTO handler MUST write through a uint8_t* pointer, matching the documented uint8_t result type at socket.h:583 and the uint8_t read at socket.c:1334.
- **FR-003**: The WIZCHIP global initializer MUST conditionally initialize the SPI union member (.IF.SPI) when _WIZCHIP_IO_MODE_ is a SPI mode, with type-correct SPI callback stubs matching uint8_t (*)(void) and void (*)(uint8_t) signatures.
- **FR-004**: The W5500 non-IPv6 TCP recv() polling loop MUST check recvsize != 0 before checking nonblocking mode, matching the ordering used by the IPv6 branch.
- **FR-005**: The ctlwizchip() function MUST dereference arg only within cases that require it, and MUST reject NULL for commands that require a valid argument.
- **FR-006**: Each fix MUST be submitted as a separate GitHub Pull Request from a fork of the Wiznet/ioLibrary_Driver repository, branched off the repository's master branch.
- **FR-007**: Each PR MUST target the original Wiznet/ioLibrary_Driver repository.

### Key Entities

- **Audit Finding (AUD)**: A confirmed defect from the TODO.md audit snapshot, identified by its AUD number, severity (P0), affected source file and line range, failure description, and prescribed action.
- **GitHub Pull Request**: A proposed code change submitted from a forked repository branch to the upstream Wiznet/ioLibrary_Driver master branch, containing exactly one AUD fix with a descriptive commit message.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All 5 P0 findings from the TODO.md audit (AUD-001 through AUD-005) have corresponding PRs created against Wiznet/ioLibrary_Driver.
- **SC-002**: Each PR contains exactly one atomic fix with no unrelated changes.
- **SC-003**: Each fix addresses the root cause described in its corresponding AUD finding, not just the symptom.
- **SC-004**: Each fix passes the verification criteria listed in its AUD finding (e.g., no OOB write under ASan, canary integrity, no NULL dereference).

## Assumptions

- The fork of Wiznet/ioLibrary_Driver already exists in the occamsshavingkit GitHub account.
- The upstream repository's master branch is the correct target for all PRs.
- The fixes are applied to the commit at which the audit was performed (39fae86465dbaa728107c3b2a90692c0a1639735).
- No additional changes outside the scope of each AUD finding are needed for the fixes to be correct.
- The W5500 build configuration (_WIZCHIP_ == 5500, SPI mode) is the target for all fixes.
- HardFault verification (AUD-003) requires board-level testing on ARM hardware; host ASan/UBSan catches the underlying memory access patterns that would cause HardFault but cannot definitively prove HardFault absence without hardware.
- Concurrency and interrupt-context scenarios for shared global state (WIZCHIP global in AUD-003, ctlwizchip in AUD-005) are covered by P1 findings (AUD-011: per-socket concurrency model, AUD-012: socket interrupt ownership) and are explicitly out of scope for these P0 fixes. The P0 fixes address initialization-time correctness, not runtime concurrency.
