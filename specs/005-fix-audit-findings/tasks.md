# Tasks: Resolve Production Audit Findings

**Input**: Design documents from `specs/005-fix-audit-findings/`

**Prerequisites**: `plan.md`, `spec.md`, `research.md`, `data-model.md`,
`contracts/`, `quickstart.md`

**Tests**: Required by FR-021 through FR-024. Each regression task must be
completed and observed failing for the audited reason before its paired
implementation task starts.

**Organization**: Tasks are grouped by user story. Every task is one coherent,
atomic reviewer gate and must be assigned alone to a future implementation
agent. Root and nested-repository changes are never combined in one task.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel because it changes different files and has no
  dependency on another incomplete task.
- **[Story]**: Maps the task to one user story from `spec.md`.
- Every task names exact paths, verification intent, requirement mapping, and
  any authoritative external specification section it implements.

## Execution Contract

- Run root tests from the repository root with
  `make -C tests BUILD_DIR=/tmp/opencode/w5500-005/<lane> <target>`.
- Run nested tests with `cmake -S WIZnet-PICO-C/tests -B
  /tmp/opencode/w5500-005/wiznet-pico-tests -DIOLIBRARY_ROOT="$PWD"`, followed
  by CMake build and `ctest --no-tests=error`.
- A test task is complete only after its new regression fails for the stated
  production defect. A compile or link failure is an acceptable red state only
  when the task introduces a contract that production does not yet declare or
  define; unrelated harness compilation and initialization failures are not.
- An implementation task is complete only after its paired regression and all
  earlier tests in that phase pass.
- Hardware execution is mandatory. Unavailable hardware produces `BLOCKED`,
  never a host-only substitute or fabricated PASS.
- Root and `WIZnet-PICO-C/` revisions are recorded separately in release
  evidence; generated outputs stay under `/tmp/opencode/w5500-005/`.

---

## Phase 1: Setup (Shared Test Infrastructure)

**Purpose**: Establish truthful production-linked test seams before changing
runtime behavior.

- [x] T001 Add `BUILD_DIR` support, strict C11 compiler flags, and named out-of-tree targets for all planned production-linked root tests in `tests/Makefile` (FR-021, FR-022; CUR-016)
- [x] T002 Create a register-side-effect-only W5500 SPI fake with command, W1C interrupt, PHY, monotonic-time, transaction-log, and lock instrumentation in `tests/support/w5500_spi_model.c` and `tests/support/w5500_spi_model.h`, without reproducing driver algorithms (FR-021, FR-023; CUR-016, CUR-017; W5500 Datasheet v1.1.0e sections 2.3-2.5)
- [x] T003 [P] Repair `tests/test_w5500_correctness.c` by replacing GNU nested callbacks with file-scope callbacks wired to the production-linked SPI fake, preserving assertions so the rebuilt binary no longer requires executable stack (FR-021, FR-022; CUR-016)
- [x] T004 [P] Repair `tests/test_public_api_sanitizer.c` by replacing all-zero callbacks with modeled W5500 transitions that preserve the `SOCK_OK` contract without weakening failure assertions (FR-020-FR-022; CUR-015, CUR-016)
- [x] T005 [P] Create the production-linked Pico SDK fake and CTest foundation in `WIZnet-PICO-C/tests/CMakeLists.txt`, `WIZnet-PICO-C/tests/fakes/fake_pico_sdk.c`, and `WIZnet-PICO-C/tests/fakes/fake_pico_sdk.h` (FR-021; Pico SDK 2.2.0 `hardware/pio.h:1993-2003`, `hardware/dma.h:718-764`, `pico/mutex.h:16-42`)

**Checkpoint**: Existing root tests use the hardware-effect fake, the nested
CTest foundation can register production-linked tests, and no runtime
remediation has been hidden in the harness.

---

## Phase 2: Foundational (Blocking Root Contracts)

**Purpose**: Publish the error, lifecycle, status, time, lock, transaction, and
stable-read contracts consumed by every user story.

**CRITICAL**: No user-story implementation begins until this phase passes.

- [ ]x] T006 Add failing public API regressions for `SOCKERR_IO`, `SOCKERR_NOTREADY`, root lifecycle transitions, FAULTED rejection, clear-error behavior, and verified recover in `tests/test_public_api_sanitizer.c` (FR-006, FR-010, FR-020; CUR-007, CUR-015)
- [ ]x] T007 Define the two error results and implement `wizchip_state_t`, `wizchip_get_state()`, `wizchip_get_last_error()`, `wizchip_clear_last_error()`, and `wizchip_recover()` in `Ethernet/socket.h`, `Ethernet/wizchip_conf.h`, and `Ethernet/wizchip_conf.c` to pass T006 (FR-006, FR-010, FR-020; CUR-007, CUR-015)
- [ ]x] T008 Add a failing callback-layout regression proving SPI busy/error/clear storage cannot overwrite byte or burst callback pointers in `tests/test_public_api_sanitizer.c` (FR-009, FR-021; CUR-010)
- [ ]x] T009 Move `SPI_STATUS` outside `_WIZCHIP.IF` and implement atomic `reg_wizchip_spistatus_cbfunc()` registration in `Ethernet/wizchip_conf.h` and `Ethernet/wizchip_conf.c` to pass T008 (FR-009, FR-010; CUR-010)
- [ ]x] T010 Add failing deadline-policy regressions for time registration, nonzero config validation, absolute expiration, wait-hook calls, poll fallback exhaustion, monotonic wrap safety, and the RTR/RCR floor in `tests/test_w5500_fault_injection.c` (FR-005, FR-020; CUR-004; SC-002; W5500 Datasheet v1.1.0e section 2.5)
- [ ]x] T011 Implement `reg_wizchip_time_cbfunc()`, timeout config accessors, absolute deadline helpers, wait-hook support, and `_WIZCHIP_POLL_MAX_` failsafe in `Ethernet/wizchip_conf.h` and `Ethernet/wizchip_conf.c` to pass T010 (FR-005, FR-020; CUR-004; W5500 Datasheet v1.1.0e section 2.5)
- [ ]x] T012 Add failing lock-registration regressions for complete pair installation, safe defaults, non-recursive ownership, and global-before-socket ordering in `tests/test_w5500_concurrency.c` (FR-003, FR-020; CUR-002; SC-001)
- [ ]x] T013 Publish and implement complete-pair `reg_wizchip_lock_cbfunc()` registration in `Ethernet/wizchip_conf.h` and `Ethernet/wizchip_conf.c` to pass T012 (FR-003, FR-020; CUR-002)
- [ ]x] T014 Add failing checked-transaction regressions for null/nonzero buffers, zero-length no-op, sticky busy/error status, CS balance, and one VDM frame in `tests/test_public_api_sanitizer.c` (FR-009-FR-011, FR-020; CUR-007, CUR-010)
- [ ]x] T015 Implement `wizchip_read8_checked()`, `wizchip_write8_checked()`, `wizchip_read_buf_checked()`, and `wizchip_write_buf_checked()` plus legacy delegation in `Ethernet/wizchip_conf.h`, `Ethernet/wizchip_conf.c`, and `Ethernet/W5500/w5500.c` to pass T014 (FR-009-FR-011; CUR-007, CUR-010; W5500 Datasheet v1.1.0e sections 2.3 and 2.5)
- [ ]x] T016 Add failing unstable-sample regressions that distinguish stable zero from deadline/I/O failure for Sn_TX_FSR and Sn_RX_RSR in `tests/test_w5500_fault_injection.c` (FR-005, FR-010; CUR-004; SC-002; W5500 Datasheet v1.1.0e section 2.5)
- [ ]x] T017 Add status-returning stable Sn_TX_FSR/Sn_RX_RSR helpers and route socket callers through them in `Ethernet/W5500/w5500.h`, `Ethernet/W5500/w5500.c`, and `Ethernet/socket.c` to pass T016 (FR-005, FR-010; CUR-004; W5500 Datasheet v1.1.0e section 2.5)

**Checkpoint**: Root contracts compile for W5500 and expose the interfaces
required by the nested transport without yet changing individual socket paths.

---

## Phase 3: User Story 1 - Dependable Socket Operations Under Concurrency (Priority: P1) MVP

**Goal**: Every supported socket operation validates before locking, owns one
non-recursive lock exactly once, preserves independent socket state, and returns
bounded explicit failures.

**Independent Test**: Run `test-concurrency` and `test-fault`; 10,000 mixed
operations across all eight sockets and two contexts complete under a
120-second watchdog with zero deadlocks, lock imbalance, state loss, hangs, or
unexpected results.

The 120-second watchdog is an outer host-test safety bound, not a production
operation deadline.

### Red-Green Slices for User Story 1

- [ ]x] T018 [US1] Add a failing TSan race regression for simultaneous I/O-mode and send-state updates on different sockets in `tests/test_w5500_concurrency.c` (FR-004, FR-021; CUR-003; SC-001)
- [ ]x] T019 [US1] Replace `sock_io_mode` and `sock_is_sending` shared bitfields with per-socket arrays and reset hooks in `Ethernet/socket.c` to pass T018 (FR-004; CUR-003; SC-001)
- [ ]x] T020 [US1] Add failing non-recursive close/fault regressions for one lock acquisition, lock-owned cleanup, already-CLOSED success, bounded failed close, FAULTED retry, and chip-reset recovery in `tests/test_w5500_concurrency.c` and `tests/test_w5500_fault_injection.c` (FR-001, FR-003, FR-006; CUR-001, CUR-002, CUR-004; W5500 Datasheet v1.1.0e section 2.5)
- [ ]x] T021 [US1] Implement an internal lock-owned close helper, per-socket HEALTHY/FAULTED state, bounded close outcomes, and verified state clearing in `Ethernet/socket.c` and `Ethernet/socket.h` to pass T020 (FR-001, FR-003, FR-006, FR-017; CUR-001, CUR-002, CUR-004; W5500 Datasheet v1.1.0e section 2.5)
- [ ]x] T022 [US1] Add failing `socket()`/`listen()` regressions for pre-lock immutable validation, post-lock mutable validation, one unlock, and no public-close recursion in `tests/test_w5500_concurrency.c` (FR-001-FR-003, FR-020; CUR-001, CUR-002)
- [ ]x] T023 [US1] Refactor `socket()` and `listen()` to validate immutable inputs before locking, revalidate mutable state after locking, call only lock-owned cleanup, and use one unlock exit in `Ethernet/socket.c` to pass T022 (FR-001-FR-003, FR-020; CUR-001, CUR-002)
- [ ]x] T024 [US1] Add failing OPEN/LISTEN command-acceptance and state-transition deadline regressions in `tests/test_w5500_fault_injection.c` (FR-005, FR-006; CUR-004; SC-002; W5500 Datasheet v1.1.0e section 2.5)
- [ ]x] T025 [US1] Replace OPEN/LISTEN polling with deadline-aware command-acceptance and state-transition waits in `Ethernet/socket.c` to pass T024 (FR-005, FR-006; CUR-004; W5500 Datasheet v1.1.0e section 2.5)
- [ ]x] T026 [US1] Add failing `connect_IO_6()`/`disconnect()` regressions for address, port, mode, state, and exact unlock behavior in `tests/test_w5500_concurrency.c` (FR-001-FR-003, FR-020; CUR-001, CUR-002)
- [ ]x] T027 [US1] Refactor `connect_W5x00()`, `connect_W6x00()`, `connect_IO_6()`, and `disconnect()` to enforce validation order, lock-owned cleanup, and one unlock exit in `Ethernet/socket.c` to pass T026 (FR-001-FR-003, FR-020; CUR-001, CUR-002)
- [ ]x] T028 [US1] Add failing CONNECT/DISCON acceptance, establishment, TIMEOUT, CLOSED, and retry-state deadline regressions in `tests/test_w5500_fault_injection.c` (FR-005, FR-006; CUR-004; SC-002; W5500 Datasheet v1.1.0e section 2.5)
- [ ]x] T029 [US1] Replace CONNECT/DISCON polling with deadline-aware acceptance/completion and explicit TIMEOUT/CLOSED/fault outcomes in `Ethernet/socket.c` to pass T028 (FR-005, FR-006; CUR-004; W5500 Datasheet v1.1.0e section 2.5)
- [ ]x] T030 [US1] Add failing `send()`/`recv()` regressions for invalid socket, null nonzero buffer, zero-length pre-access no-op, CLOSE_WAIT cleanup, and one unlock per exit in `tests/test_w5500_concurrency.c` (FR-001-FR-003, FR-011, FR-020; CUR-001, CUR-002)
- [ ]x] T031 [US1] Refactor `send()` and `recv()` for pre-lock zero-length/immutable validation, one unlock exit, and lock-owned CLOSE_WAIT cleanup in `Ethernet/socket.c` and `Ethernet/socket.h` to pass T030 (FR-001-FR-003, FR-011, FR-020; CUR-001, CUR-002)
- [ ]x] T032 [US1] Add failing stream-I/O regressions for pending SENDOK/TIMEOUT, TX-space, RX-size, socket-state, SEND acceptance, and RECV acceptance deadlines in `tests/test_w5500_fault_injection.c` (FR-005, FR-006, FR-010; CUR-004; SC-002; W5500 Datasheet v1.1.0e section 2.5)
- [ ]x] T033 [US1] Bound every stream SEND/RECV free-space, availability, interrupt, command, and state wait and preserve fault state after ambiguous mutation in `Ethernet/socket.c` to pass T032 (FR-005, FR-006, FR-010; CUR-004; W5500 Datasheet v1.1.0e section 2.5)
- [ ]x] T034 [US1] Add failing `sendto_IO_6()`/`recvfrom_IO_6()` regressions for wrapper validation, zero length, exact unlock, and malformed MACRAW cleanup without recursive close in `tests/test_w5500_concurrency.c` (FR-001-FR-003, FR-011, FR-020; CUR-001, CUR-002)
- [ ]x] T035 [US1] Refactor `sendto_W5x00()`, `sendto_W6x00()`, `sendto_IO_6()`, `recvfrom_W5x00()`, `recvfrom_W6x00()`, and `recvfrom_IO_6()` for pre-lock validation, zero-length no-op, one unlock exit, and lock-owned MACRAW cleanup in `Ethernet/socket.c` and `Ethernet/socket.h` to pass T034 (FR-001-FR-003, FR-011, FR-020; CUR-001, CUR-002)
- [ ]x] T036 [US1] Add failing datagram/raw-I/O regressions for TX/RX availability, SENDOK/TIMEOUT, SEND/RECV acceptance, and nonblocking deadline semantics in `tests/test_w5500_fault_injection.c` (FR-005, FR-006, FR-010; CUR-004; SC-002; W5500 Datasheet v1.1.0e section 2.5)
- [ ]x] T037 [US1] Bound every datagram/raw TX/RX availability, command, SENDOK, TIMEOUT, and nonblocking wait in `Ethernet/socket.c` to pass T036 (FR-005, FR-006, FR-010; CUR-004; W5500 Datasheet v1.1.0e section 2.5)
- [ ]x] T038 [US1] Add failing `ctlsocket()`, `setsockopt()`, and `getsockopt()` regressions for invalid socket/control/pointer/mode, faulted `SO_STATUS`, no unintended access, and one unlock per exit in `tests/test_w5500_concurrency.c` (FR-002, FR-003, FR-006, FR-020; CUR-002, CUR-015)
- [ ]x] T039 [US1] Harden `ctlsocket()`, `setsockopt()`, and `getsockopt()` validation, fault-state query behavior, and cleanup exits in `Ethernet/socket.c` to pass T038 (FR-002, FR-003, FR-006, FR-020; CUR-002, CUR-015)
- [ ]x] T040 [US1] Add a failing bounded SO_KEEPALIVESEND command-acceptance regression in `tests/test_w5500_fault_injection.c` (FR-005, FR-006; CUR-004; SC-002; W5500 Datasheet v1.1.0e section 2.5)
- [ ]x] T041 [US1] Replace the SO_KEEPALIVESEND spin loop with a deadline-aware command wait and concrete error propagation in `Ethernet/socket.c` to pass T040 (FR-005, FR-006; CUR-004; W5500 Datasheet v1.1.0e section 2.5)
- [ ]x] T042 [US1] Add a failing concurrent ephemeral-port regression for uniqueness, wrap behavior, and global-before-socket lock ordering in `tests/test_w5500_concurrency.c` (FR-003, FR-004; CUR-003; SC-001)
- [ ]x] T043 [US1] Move ephemeral-port allocation under the global lock before socket-lock acquisition and preserve wrap/uniqueness semantics in `Ethernet/socket.c` to pass T042 (FR-003, FR-004; CUR-003; SC-001)
- [ ]x] T044 [US1] Execute the final 10,000-operation two-context/eight-socket stress and all US1 fault regressions from `tests/test_w5500_concurrency.c` and `tests/test_w5500_fault_injection.c`, recording zero TSan races, deadlocks, lock leaks, state loss, or deadline overruns (FR-001-FR-006, FR-021; CUR-001-CUR-004; SC-001, SC-002)

**Checkpoint**: User Story 1 is independently usable and testable on the host.

---

## Phase 4: User Story 2 - Safe and Recoverable RP2040 Transport (Priority: P2)

**Goal**: The single RP2040 transport owns validated configuration and resources,
reports every failure, preserves interrupt responsiveness, and supports bounded
recovery across all lifecycle states.

**Independent Test**: Run nested CTest with 100 injected failures per resource,
zero through 16 KiB transfers, DMA/PIO stalls, lifecycle overlap, and sticky
status checks; all operations return by deadline with no invalid handle, leak,
double release, unsafe unclaim, or payload-wide interrupt mask.

### Red-Green Slices for User Story 2

- [x] T045 [US2] Create failing transport configuration tests for nulls, pins, instance/divider validation, timeout defaults, and caller-lifetime independence in `WIZnet-PICO-C/tests/test_wizchip_qspi_pio.c` (FR-007, FR-008, FR-020; CUR-005, CUR-006; Pico SDK 2.2.0 `hardware/gpio.h:244-245`, `hardware/pio.h:535-576`)
- [x] T046 [US2] Append timeout fields, copy/normalize `wiznet_spi_config_t`, and validate all transport inputs before side effects in `WIZnet-PICO-C/port/ioLibrary_Driver/inc/wizchip_qspi_pio.h`, `WIZnet-PICO-C/port/ioLibrary_Driver/src/wizchip_qspi_pio.c`, and `WIZnet-PICO-C/port/ioLibrary_Driver/src/wizchip_spi.c` to pass T045 (FR-007, FR-008, FR-020; CUR-005, CUR-006; RP2040 Datasheet sections 2.19.1 and 3.5.5)
- [x] T047 [US2] Add failing transactional-open tests for singleton contention, PIO/SM exhaustion, DMA-out failure, DMA-in partial failure, reverse unwind, null publication, and 100 retry/close cycles in `WIZnet-PICO-C/tests/test_wizchip_qspi_pio.c` (FR-007, FR-008, FR-012; CUR-005, CUR-006, CUR-008; SC-003; Pico SDK 2.2.0 `hardware/pio.h:1993-2003`)
- [x] T048 [US2] Implement OPENING reservation, `pio_claim_free_sm_and_add_program()`, nonpanic DMA claims, ownership ledger, reverse unwind, READY-last publication, and legacy null wrapper in `WIZnet-PICO-C/port/ioLibrary_Driver/inc/wizchip_qspi_pio.h` and `WIZnet-PICO-C/port/ioLibrary_Driver/src/wizchip_qspi_pio.c` to pass T047 (FR-007, FR-008, FR-012; CUR-005, CUR-006, CUR-008; Pico SDK 2.2.0 `hardware/pio.h:1993-2038`, `hardware/pio/pio.c:404-469`)
- [x] T049 [US2] Add failing bus/global/eight-socket mutex tests for one-time initialization, one CS-frame ownership, no recursive acquisition, lifecycle exclusion, and IRQ-enabled 16 KiB transfer in `WIZnet-PICO-C/tests/test_wizchip_qspi_pio.c` and `WIZnet-PICO-C/tests/test_wizchip_spi.c` (FR-012, FR-013; CUR-008, CUR-009; Pico SDK 2.2.0 `pico/mutex.h:16-42`, `pico/critical_section.h:17-28`)
- [x] T050 [US2] Replace payload-wide `critical_section_t` ownership with one non-recursive bus mutex, one global mutex, eight socket mutexes, and short state-only IRQ protection in `WIZnet-PICO-C/port/ioLibrary_Driver/src/wizchip_spi.c`, `WIZnet-PICO-C/port/ioLibrary_Driver/src/wizchip_qspi_pio.c`, `WIZnet-PICO-C/port/ioLibrary_Driver/inc/wizchip_qspi_pio.h`, and `WIZnet-PICO-C/port/CMakeLists.txt` to pass T049 (FR-012, FR-013; CUR-008, CUR-009; Pico SDK 2.2.0 `pico/mutex.h:16-42`, `pico/critical_section.h:17-28`)
- [x] T051 [US2] Add failing bounded-abort tests for immediate/near-deadline/nonretiring DMA, both channels, RP2040-E13 IRQ state, quarantine, close/recover retry, and no global reset/unsafe unclaim in `WIZnet-PICO-C/tests/test_wizchip_qspi_pio.c` (FR-005, FR-006, FR-008, FR-012; CUR-006-CUR-008; RP2040 Datasheet section 2.5.5.3; Pico SDK 2.2.0 `hardware/dma.h:718-764`)
- [x] T052 [US2] Implement bounded `dma_hw->abort` polling, IRQ-mask preservation, E13 acknowledgement, per-channel quarantine, and BUSY-safe ownership in `WIZnet-PICO-C/port/ioLibrary_Driver/src/wizchip_qspi_pio.c` to pass T051 (FR-005, FR-006, FR-008, FR-012; CUR-006-CUR-008; RP2040 Datasheet section 2.5.5.3; Pico SDK 2.2.0 `hardware/dma.h:718-764,891-943`)
- [x] T053 [US2] Add failing transfer tests for zero-length no-op, one byte, 16 KiB, overflow, DMA/PIO stalls, AHB errors, deadline boundaries, partial-data discard, CS cleanup, sticky error, and wait-hook servicing in `WIZnet-PICO-C/tests/test_wizchip_qspi_pio.c` (FR-005, FR-006, FR-010, FR-011, FR-013; CUR-007, CUR-009; RP2040 Datasheet sections 2.5.1, 2.5.2, 3.2.4, 3.5.2)
- [x] T054 [US2] Implement zero-length pre-access return, bounded DMA/PIO completion, error-bit checks, partial-data discard, sticky status, FAULTED transition, and 16 KiB enforcement in `WIZnet-PICO-C/port/ioLibrary_Driver/src/wizchip_qspi_pio.c` and `WIZnet-PICO-C/port/ioLibrary_Driver/inc/wizchip_qspi_pio.h` to pass T053 (FR-005, FR-006, FR-010, FR-011, FR-013; CUR-007, CUR-009; RP2040 Datasheet sections 2.5.1, 2.5.2, 3.2.4, 3.5.2)
- [x] T055 [US2] Add failing lifecycle tests for repeated/concurrent close, sleep/wake no-ops, recover, reopen, invalid-state transfer, quarantine refusal, and 100 lifecycle cycles in `WIZnet-PICO-C/tests/test_wizchip_qspi_pio.c` (FR-006, FR-012, FR-020; CUR-008, CUR-015; Pico SDK 2.2.0 `hardware/pio.h:1029-1102,1816-1828`)
- [x] T056 [US2] Implement checked close/sleep/wake/recover entry points and legacy wrappers with serialized lifecycle transitions and quarantine-aware retirement in `WIZnet-PICO-C/port/ioLibrary_Driver/inc/wizchip_qspi_pio.h` and `WIZnet-PICO-C/port/ioLibrary_Driver/src/wizchip_qspi_pio.c` to pass T055 (FR-006, FR-012, FR-020; CUR-008, CUR-015; Pico SDK 2.2.0 `hardware/pio.h:1029-1102,1816-1828`)
- [x] T057 [US2] Create failing high-level port tests for checked initialization order, null-handle rejection, idempotence, time/lock/data/status/CS registration, bounded link/version checks, and lifecycle result propagation in `WIZnet-PICO-C/tests/test_wizchip_spi.c` (FR-007, FR-009, FR-010, FR-020; CUR-005, CUR-007, CUR-010, CUR-015; Pico SDK 2.2.0 `pico/error.h:12-46`, `hardware/timer.h:219-242`)
- [x] T058 [US2] Upgrade high-level port APIs and adapters to return Pico status, register root time/lock/data/status/CS callbacks after READY, and bound link/version checks in `WIZnet-PICO-C/port/ioLibrary_Driver/inc/wizchip_spi.h`, `WIZnet-PICO-C/port/ioLibrary_Driver/src/wizchip_spi.c`, and `WIZnet-PICO-C/port/CMakeLists.txt` to pass T057 (FR-007, FR-009, FR-010, FR-020; CUR-005, CUR-007, CUR-010, CUR-015; Pico SDK 2.2.0 `pico/error.h:12-46`, `hardware/timer.h:219-242`)
- [x] T059 [US2] Create a failing source-contract test in `WIZnet-PICO-C/tests/test_example_status_contract.py` that discovers every source under `WIZnet-PICO-C/examples/` calling a WIZnet startup API, rejects unchecked upgraded startup calls, and verifies `WIZnet-PICO-C/examples/can/can_loopback/wizchip_can_loopback.c` and `WIZnet-PICO-C/examples/can/can_utils/wizchip_can_utils.c` remain excluded because they call none (FR-007, FR-010, FR-020; CUR-005, CUR-007, CUR-015)
- [x] T060 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/upnp/wizchip_upnp.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T061 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/loopback/wizchip_loopback.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T062 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/mqtt/publish_subscribe/wizchip_mqtt_publish_subscribe.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T063 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/mqtt/publish/wizchip_mqtt_publish.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T064 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/mqtt/subscribe/wizchip_mqtt_subscribe.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T065 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/sntp/wizchip_sntp.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T066 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/tftp/wizchip_tftp_client.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T067 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/dhcp_dns/wizchip_dhcp_dns.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T068 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/can/can_to_ethernet/wizchip_can_to_eth_tcpc.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T069 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/can/can_to_ethernet/wizchip_can_to_eth_tcps.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T070 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/can/can_web_config/wizchip_can_web_config.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T071 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/pppoe/wizchip_pppoe.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T072 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/tcp_client_over_ssl/wizchip_tcp_client_over_ssl.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T073 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/tcp_server_over_ssl/wizchip_tcp_server_over_ssl.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T074 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/udp_multicast/udp_multicast_receiver/wizchip_udp_multicast_receiver.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T075 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/netbios/wizchip_netbios.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T076 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/http/server/wizchip_http_server.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T077 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/tcp_server_multi_socket/wizchip_tcp_server_multi_socket.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T078 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/udp/udp_server/wizchip_udp_server.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T079 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/udp/udp_client/wizchip_udp_client.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)
- [x] T080 [P] [US2] Check every upgraded startup result and abort initialization on failure in `WIZnet-PICO-C/examples/network_install/wizchip_network_install.c` to satisfy T059 (FR-007, FR-010, FR-020; Pico SDK 2.2.0 `pico/error.h:12-46`)

**Checkpoint**: User Story 2 passes nested host tests independently; all sample
applications consume checked startup results.

---

## Phase 5: User Story 3 - Correct Configuration, PHY, and Interrupt Behavior (Priority: P3)

**Goal**: PHY state, raw flags, caches, multi-register values, and GPIO callbacks
match observed hardware and public contracts without partial or unrelated effects.

**Independent Test**: Run `test-phy`, `test-raw-flags`, `test-configuration`,
atomic-write tests, and GPIO CTest; every modeled readback/callback matches the
requested state, all documented flags are covered, and unrelated sources remain
untouched.

### Red-Green Slices for User Story 3

- [x] T081 [P] [US3] Create failing status/reset tests for PHY API signatures, nulls, unsupported values, `ctlwizchip()` propagation, observable 200 microsecond engineering hold, RST low/high sequence, masked readback, and deadline error in `tests/test_w5500_phy.c` (FR-005, FR-014, FR-020; CUR-011; SC-005; W5500 Datasheet v1.1.0e section 2.4)
- [x] T082 [US3] Upgrade PHY APIs to status returns and implement bounded real-time RST low/high/reset readback behavior in `Ethernet/wizchip_conf.h` and `Ethernet/wizchip_conf.c` to pass T081 (FR-005, FR-014, FR-020; CUR-011; W5500 Datasheet v1.1.0e section 2.4)
- [x] T083 [US3] Add failing PHY power/mode/config tests for preserved unrelated bits, valid mode, RST high, exact readback, and getter null handling in `tests/test_w5500_phy.c` (FR-014, FR-020; CUR-011; SC-005; W5500 Datasheet v1.1.0e section 2.4)
- [x] T084 [US3] Implement global-lock PHY power/mode/config read-modify-write transactions with exact masked readback and null/value validation in `Ethernet/wizchip_conf.h` and `Ethernet/wizchip_conf.c` to pass T083 (FR-014, FR-020; CUR-011; W5500 Datasheet v1.1.0e section 2.4)
- [x] T085 [US3] Add failing link-notification tests for sampled ON/OFF values, one callback per transition, and coherent registration/reset history in `tests/test_w5500_phy.c` (FR-015; CUR-011; SC-005; W5500 Datasheet v1.1.0e section 2.4)
- [x] T086 [US3] Pass the sampled hardware link value to `wizchip_phy_link_callback()` and initialize/reset previous-link history coherently in `Ethernet/wizchip_conf.h` and `Ethernet/wizchip_conf.c` to pass T085 (FR-015; CUR-011; W5500 Datasheet v1.1.0e section 2.4)
- [x] T087 [P] [US3] Create an exhaustive failing matrix for TCP, UDP, MACRAW, IPRAW, `SF_IO_NONBLOCK`, flag dependencies, MACRAW socket zero, unknown bits, and rejection without side effects in `tests/test_w5500_raw_flags.c` (FR-002, FR-016, FR-021; CUR-012; SC-006; W5500 Datasheet v1.1.0e section 2.5)
- [x] T088 [US3] Replace ad hoc socket flag checks with mode-exact masks/dependencies, strip software nonblocking before Sn_MR, and reject unknown bits before side effects in `Ethernet/socket.c` and `Ethernet/socket.h` to pass T087 (FR-002, FR-016; CUR-012; W5500 Datasheet v1.1.0e section 2.5)
- [x] T089 [P] [US3] Extend failing atomic-write coverage to `setINTLEVEL`, `setRTR`, `setPSID`, `setPMRU`, `setSn_PORT`, `setSn_DPORT`, `setSn_MSSR`, and `setSn_FRAG` in `tests/test_w5500_atomic_pointer_write.c` (FR-018; CUR-013; SC-007; W5500 Datasheet v1.1.0e sections 2.3 and 2.5)
- [x] T090 [US3] Route all eight remaining 16-bit setters through one `wizchip_write16_5500()` VDM transaction in `Ethernet/W5500/w5500.h` to pass T089 (FR-018; CUR-013; W5500 Datasheet v1.1.0e sections 2.3 and 2.5)
- [x] T091 [P] [US3] Create failing authoritative TX/RX buffer setter tests for validation, checked write/readback, success-only cache update, and injected failure in `tests/test_w5500_configuration.c` (FR-017, FR-018; CUR-013; SC-007; W5500 Datasheet v1.1.0e sections 2.3 and 2.5)
- [x] T092 [US3] Replace direct TX/RX size macros with checked authoritative setters and success-only byte-cache updates in `Ethernet/W5500/w5500.h`, `Ethernet/W5500/w5500.c`, `Ethernet/wizchip_conf.h`, and `Ethernet/wizchip_conf.c` to pass T091 (FR-017, FR-018; CUR-013; W5500 Datasheet v1.1.0e sections 2.3 and 2.5)
- [x] T093 [US3] Add a failing memory-layout/reset transaction regression for global-plus-eight-socket ordering, mid-layout failure, all-socket software clearing, and cache repopulation in `tests/test_w5500_configuration.c` (FR-003, FR-006, FR-017, FR-018; CUR-013; SC-007; W5500 Datasheet v1.1.0e sections 2.3 and 2.5)
- [x] T094 [US3] Make `wizchip_init()` and `wizchip_sw_reset()` global-plus-ascending-socket transactions with failure-safe publication, full socket-state clearing, and cache refresh in `Ethernet/wizchip_conf.c`, `Ethernet/wizchip_conf.h`, `Ethernet/socket.c`, and `Ethernet/socket.h` to pass T093 (FR-003, FR-006, FR-017, FR-018; CUR-013; W5500 Datasheet v1.1.0e sections 2.3 and 2.5)
- [x] T095 [US3] Add failing concurrent netinfo/DNS/DHCP and RTR/RCR set/get tests that allow only complete old/new values and enforce the retransmission deadline floor in `tests/test_w5500_configuration.c` (FR-005, FR-017, FR-018; CUR-013; SC-007; W5500 Datasheet v1.1.0e sections 2.3 and 2.5)
- [x] T096 [US3] Protect netinfo/DNS/DHCP and RTR/RCR getters/setters as global logical transactions and update the deadline floor coherently in `Ethernet/wizchip_conf.c` and `Ethernet/wizchip_conf.h` to pass T095 (FR-005, FR-017, FR-018; CUR-013; W5500 Datasheet v1.1.0e sections 2.3 and 2.5)
- [x] T097 [US3] Add failing interrupt-mask set/get/clear tests for one coherent logical mask and no unrelated ownership loss in `tests/test_w5500_configuration.c` (FR-018, FR-020; CUR-013; SC-007; W5500 Datasheet v1.1.0e section 2.5)
- [x] T098 [US3] Protect interrupt-mask accessors and `ctlwizchip()` dispatch as pointer-validated global logical transactions in `Ethernet/wizchip_conf.c` and `Ethernet/wizchip_conf.h` to pass T097 (FR-018, FR-020; CUR-013; W5500 Datasheet v1.1.0e section 2.5)
- [x] T099 [US3] Create failing GPIO tests for invalid registration, merged masks, one raw `PIN_INT` handler, exact falling-edge ack, ISR-only pending capture, task dispatch, W1C clearing, simultaneous unrelated sources, drain/rearm, unregister, and legacy adaptation in `WIZnet-PICO-C/tests/test_wizchip_gpio_irq.c` (FR-019-FR-021; CUR-014, CUR-015; Pico SDK 2.2.0 `hardware/gpio.h:455-471,559-611,648-683,785-830`)
- [x] T100 [US3] Replace the core-wide callback with per-socket registration, raw `PIN_INT` pending ISR, task-context SIR/Sn_IR dispatch, mask ownership, drain/rearm, unregister, and status-returning legacy adaptation in `WIZnet-PICO-C/port/ioLibrary_Driver/inc/wizchip_gpio_irq.h` and `WIZnet-PICO-C/port/ioLibrary_Driver/src/wizchip_gpio_irq.c` to pass T099 (FR-019, FR-020; CUR-014, CUR-015; RP2040 Datasheet section 2.19.3; Pico SDK 2.2.0 `hardware/gpio.h:559-611,648-683,785-830`)

**Checkpoint**: User Story 3 host and nested regressions pass independently.

---

## Phase 6: User Story 4 - Trustworthy Release Evidence (Priority: P4)

**Goal**: Every production correction is supported by reproducible host,
formal, cross-build, and physical evidence, and no status document can claim a
stronger result than the canonical matrix.

**Independent Test**: Execute `quickstart.md` from clean matching root/nested
revisions. The validator accepts exactly 19 current and 73 historical rows, all
mandatory gates pass, and no hardware result is substituted.

### Evidence and Gate Infrastructure

- [x] T101 [P] [US4] Add failing evidence-validator tests for ID completeness/uniqueness, FR lists, full clean SHAs, empty fields, missing artifacts, model-only production claims, host-only hardware claims, invalid supersession/scope, blocker contradictions, stronger status claims, and `--traceability` coverage of every FR/SC/CUR/AUD identifier in `tests/test_check_audit_evidence.py` (FR-023, FR-025-FR-027; CUR-017-CUR-019; SC-008, SC-010, SC-011)
- [x] T102 [US4] Implement the exact canonical Markdown parser, release rules, and `--traceability SPEC TASKS` mode in `tests/check_audit_evidence.py` to pass T101 (FR-023, FR-025-FR-027; CUR-017-CUR-019)
- [x] T103 [US4] Add a fresh ASan+UBSan lane that proves instrumentation, runs every production-linked root regression, rejects skips, and requires non-executable stack in `tests/Makefile` (FR-021, FR-022; CUR-016; SC-008)
- [x] T104 [US4] Add a separate pthread TSan lane for `tests/test_w5500_concurrency.c` without ASan mixing in `tests/Makefile` (FR-004, FR-021, FR-022; CUR-001-CUR-003, CUR-016; SC-001, SC-008)
- [x] T105 [US4] Create `tests/cbmc/Makefile` and `tests/cbmc/w5500_production_harness.c` with production-linked nondeterministic SPI/time/lock properties and documented assumptions, and label `tests/test_cbmc_model.c` supplemental only (FR-004, FR-023; CUR-003, CUR-017; SC-008; CBMC User Guide, "Properties" and "Assumptions" sections)
- [x] T106 [US4] Add reproducible cppcheck, clang-tidy, and Clang analyzer targets with recorded versions and actionable-finding policy in `tests/Makefile` and `tests/.clang-tidy` (FR-021, FR-025; CUR-016, CUR-018; SC-008, SC-010)
- [x] T107 [US4] Add warnings-as-errors W5500 Cortex-M0+/M4, shared W6300, and both RP2040 firmware cross-build targets using Pico SDK 2.2.0 and `arm-none-eabi-gcc` in `tests/Makefile` (FR-021, FR-025; CUR-016, CUR-018; SC-008, SC-010)
- [x] T108 [US4] Add binary verification for GNU_STACK, executable data, applicable PIE/RELRO, sanitizer runtime linkage, and optimized observable PHY delays in `tests/Makefile` (FR-014, FR-022, FR-025; CUR-011, CUR-016, CUR-018; SC-005, SC-008, SC-010; GNU Binutils `ld` manual "Options" entries `-z execstack`, `-z noexecstack`, `-z relro`, and `-pie`)

### Hardware Smoke Harness

- [x] T109 [US4] Add failing source/build contract checks for current clock-divider macros, strict warnings, production high-level transport linkage, and required Pico libraries in `tests/test_hardware_harness_contracts.py` (FR-021, FR-024, FR-025; CUR-009, CUR-016, CUR-018)
- [x] T110 [US4] Replace stale clock macros and partial transport wiring, remove warning suppressions, and compile checked production root/transport/DHCP sources in `tests/hardware/rp2040_w5500_probe/CMakeLists.txt` and `tests/hardware/rp2040_w5500_probe/src/main.c` to pass T109 (FR-021, FR-024; CUR-009, CUR-016; Pico SDK 2.2.0 build interfaces)
- [x] T111 [US4] Add failing smoke transcript/count tests for VERSIONR 0x04, three independent address assignments, 100 UDP echoes, exact Sn_RX_RD advancement, zero sticky error, and no watchdog reset in `tests/test_hardware_harness_contracts.py` (FR-024, FR-025; SC-009; W5500 Datasheet v1.1.0e sections 2.3 and 2.5)
- [x] T112 [US4] Implement the bounded smoke sequence and document setup/results in `tests/hardware/rp2040_w5500_probe/src/main.c` and `tests/hardware/rp2040_w5500_probe/README.md` to pass T111, without presenting smoke as full diagnostic evidence (FR-024, FR-025; SC-009; W5500 Datasheet v1.1.0e sections 2.3 and 2.5)

### Authoritative Diagnostic Harness

- [x] T113 [US4] Add failing C/Python contract tests for `_full` paths/target name, strict warnings, production `wizchip_spi.c`/PIO/GPIO linkage, and required Pico libraries in `tests/hardware/rp2040_w5500_diag_full/tests/test_diag_build_contract.py` (FR-021, FR-024, FR-025; CUR-009, CUR-014, CUR-016, CUR-018)
- [x] T114 [US4] Revive the production-linked `_full` target, correct README paths/stale macros, remove warning suppressions, and link checked root/transport/GPIO sources in `tests/hardware/rp2040_w5500_diag_full/CMakeLists.txt`, `tests/hardware/rp2040_w5500_diag_full/README.md`, and `tests/hardware/rp2040_w5500_diag_full/src/w5500_diag_board.c` to pass T113 (FR-021, FR-024, FR-025; CUR-009, CUR-014, CUR-016, CUR-018)
- [ ] T115 [US4] Add failing provenance tests for root/transport SHA, dirty state, binary diff, build UTC, Pico SDK/compiler/build type, thresholds, and post-build UF2 digest in `tests/hardware/rp2040_w5500_diag_full/tests/test_diag_build_info.c` and `tests/hardware/rp2040_w5500_diag_full/host/test_diag_host.py` (FR-023-FR-026; CUR-017-CUR-019; SC-008, SC-010)
- [ ] T116 [US4] Implement dual-repository/toolchain/threshold provenance and UF2 manifest output in `tests/hardware/rp2040_w5500_diag_full/CMakeLists.txt`, `tests/hardware/rp2040_w5500_diag_full/src/diag_build_info.h.in`, `tests/hardware/rp2040_w5500_diag_full/src/diag_build_info.c`, and `tests/hardware/rp2040_w5500_diag_full/host/diag_host.py` to pass T115 (FR-023-FR-026; CUR-017-CUR-019)
- [ ] T117 [US4] Add failing runner/controller tests for separate PIO instruction, SM, DMA-out, and DMA-in exhaustion stages with 100 unwind/retry/close cycles and ownership counters in `tests/hardware/rp2040_w5500_diag_full/tests/test_diag_runner.c`, `tests/hardware/rp2040_w5500_diag_full/tests/test_diag_stage_phases.c`, and `tests/hardware/rp2040_w5500_diag_full/host/test_diag_host.py` (FR-007, FR-008, FR-012, FR-024; CUR-005, CUR-006, CUR-008; SC-003; RP2040 Datasheet sections 2.5 and 3)
- [ ] T118 [US4] Implement the four resource-exhaustion stages and reports in `tests/hardware/rp2040_w5500_diag_full/src/diag_runner.c`, `tests/hardware/rp2040_w5500_diag_full/src/diag_w5500_stages.c`, and `tests/hardware/rp2040_w5500_diag_full/host/diag_host.py` to pass T117 (FR-007, FR-008, FR-012, FR-024; CUR-005, CUR-006, CUR-008; SC-003; RP2040 Datasheet sections 2.5 and 3)
- [ ] T119 [US4] Add failing zero-length, stalled DMA/PIO, AHB error, deadline, sticky-status, partial-data, and quarantine diagnostic tests in `tests/hardware/rp2040_w5500_diag_full/tests/test_diag_stage_phases.c` and `tests/hardware/rp2040_w5500_diag_full/host/test_diag_host.py` (FR-005, FR-006, FR-010, FR-011, FR-024; CUR-004, CUR-007, CUR-010; SC-002; RP2040 Datasheet section 2.5.5.3)
- [ ] T120 [US4] Implement timeout/recovery/status diagnostic stages and structured outcomes in `tests/hardware/rp2040_w5500_diag_full/src/diag_w5500_stages.c`, `tests/hardware/rp2040_w5500_diag_full/src/diag_runner.c`, and `tests/hardware/rp2040_w5500_diag_full/host/diag_host.py` to pass T119 (FR-005, FR-006, FR-010, FR-011, FR-024; CUR-004, CUR-007, CUR-010; SC-002; RP2040 Datasheet section 2.5.5.3)
- [ ] T121 [US4] Add failing repeated/concurrent close, sleep, wake, recover, reopen, reset/transfer exclusion, and actual-claim lifecycle tests in `tests/hardware/rp2040_w5500_diag_full/tests/test_diag_stage_phases.c` and `tests/hardware/rp2040_w5500_diag_full/host/test_diag_host.py` (FR-006, FR-012, FR-024; CUR-008; SC-002, SC-003)
- [ ] T122 [US4] Implement lifecycle diagnostic stages and state/resource reporting in `tests/hardware/rp2040_w5500_diag_full/src/diag_w5500_stages.c`, `tests/hardware/rp2040_w5500_diag_full/src/diag_runner.c`, and `tests/hardware/rp2040_w5500_diag_full/host/diag_host.py` to pass T121 (FR-006, FR-012, FR-024; CUR-008; SC-002, SC-003)
- [ ] T123 [US4] Add failing two-core/eight-socket diagnostic tests for same-socket serialization, independent progress, 10,000 operations, lock imbalance, watchdog deadlock, and state loss in `tests/hardware/rp2040_w5500_diag_full/tests/test_diag_stage_phases.c` and `tests/hardware/rp2040_w5500_diag_full/host/test_diag_host.py` (FR-001-FR-004, FR-024; CUR-001-CUR-003; SC-001; Pico SDK 2.2.0 `pico/mutex.h:16-42`)
- [ ] T124 [US4] Implement the multicore socket-lock diagnostic and metrics in `tests/hardware/rp2040_w5500_diag_full/src/diag_w5500_stages.c`, `tests/hardware/rp2040_w5500_diag_full/src/diag_runner.c`, and `tests/hardware/rp2040_w5500_diag_full/host/diag_host.py` to pass T123 (FR-001-FR-004, FR-024; CUR-001-CUR-003; SC-001; Pico SDK 2.2.0 `pico/mutex.h:16-42`)
- [ ] T125 [US4] Add failing 100-cycle PHY diagnostic tests for disconnect/reconnect/power/reset, callback value, PHYCFGR readback, reset-pin timing, configured hold, and debug/optimized identity in `tests/hardware/rp2040_w5500_diag_full/tests/test_diag_stage_phases.c` and `tests/hardware/rp2040_w5500_diag_full/host/test_diag_host.py` (FR-014, FR-015, FR-024; CUR-011; SC-005; W5500 Datasheet v1.1.0e sections 2.4 and 2.5)
- [ ] T126 [US4] Implement the PHY cycle/timing diagnostic with the 200 microsecond value labeled engineering policy in `tests/hardware/rp2040_w5500_diag_full/src/diag_w5500_stages.c`, `tests/hardware/rp2040_w5500_diag_full/src/diag_runner.c`, and `tests/hardware/rp2040_w5500_diag_full/host/diag_host.py` to pass T125 (FR-014, FR-015, FR-024; CUR-011; SC-005; W5500 Datasheet v1.1.0e sections 2.4 and 2.5)
- [ ] T127 [US4] Add failing GPIO diagnostic tests for unrelated filtering, raw ack, ISR-only pending work, task dispatch, mask merge, W1C clearing, drain/rearm, and final unregister in `tests/hardware/rp2040_w5500_diag_full/tests/test_diag_stage_phases.c` and `tests/hardware/rp2040_w5500_diag_full/host/test_diag_host.py` (FR-019, FR-020, FR-024; CUR-014; Pico SDK 2.2.0 `hardware/gpio.h:559-611,648-683,785-830`)
- [ ] T128 [US4] Implement the GPIO ownership/filtering diagnostic and structured event evidence in `tests/hardware/rp2040_w5500_diag_full/src/diag_w5500_stages.c`, `tests/hardware/rp2040_w5500_diag_full/src/diag_runner.c`, and `tests/hardware/rp2040_w5500_diag_full/host/diag_host.py` to pass T127 (FR-019, FR-020, FR-024; CUR-014; RP2040 Datasheet section 2.19.3)
- [ ] T129 [US4] Add failing maximum-16-KiB USB-SOF latency tests for source citation, 1 ms derived deadline, measured SPI clock, event count, worst latency, missed events, and pre-fix blackout in `tests/hardware/rp2040_w5500_diag_full/tests/test_diag_stage_phases.c` and `tests/hardware/rp2040_w5500_diag_full/host/test_diag_host.py` (FR-013, FR-024, FR-025; CUR-009, CUR-018; SC-004; USB 2.0 Specification section 11.18.2)
- [ ] T130 [US4] Implement the maximum-transfer USB-SOF latency stage without claiming a universal RP2040 bound in `tests/hardware/rp2040_w5500_diag_full/src/diag_w5500_stages.c`, `tests/hardware/rp2040_w5500_diag_full/src/diag_runner.c`, and `tests/hardware/rp2040_w5500_diag_full/host/diag_host.py` to pass T129 (FR-013, FR-024, FR-025; CUR-009, CUR-018; SC-004; USB 2.0 Specification section 11.18.2)

### Mandatory Gate Execution

- [ ] T131 [US4] Execute strict GCC and Clang root host lanes from `tests/Makefile` and retain complete logs under `/tmp/opencode/w5500-005/evidence/` (FR-021, FR-025; CUR-016, CUR-018; SC-008, SC-010)
- [ ] T132 [US4] Execute the fresh ASan+UBSan lane from `tests/Makefile`, verify runtime linkage/non-executable stack, and retain zero-finding logs under `/tmp/opencode/w5500-005/evidence/` (FR-021, FR-022, FR-025; CUR-016, CUR-018; SC-008, SC-010)
- [ ] T133 [US4] Execute the separate TSan 10,000-operation lane from `tests/Makefile` and retain zero-race/deadlock logs under `/tmp/opencode/w5500-005/evidence/` (FR-004, FR-021, FR-022; CUR-001-CUR-003, CUR-016; SC-001, SC-008)
- [ ] T134 [US4] Execute nested transport CTest plus diagnostic CTest/Python host suites from `WIZnet-PICO-C/tests/CMakeLists.txt` and `tests/hardware/rp2040_w5500_diag_full/`, rejecting zero discovered tests and retaining logs (FR-021, FR-024; CUR-005-CUR-016; SC-002, SC-003, SC-008)
- [ ] T135 [US4] Execute the production-linked properties in `tests/cbmc/Makefile`, record functions/assumptions/unwind bounds, and retain zero-failed-property output (FR-023; CUR-003, CUR-017; SC-008)
- [ ] T136 [US4] Execute cppcheck, clang-tidy, and Clang analyzer targets from `tests/Makefile`, resolving every actionable finding before retaining reports (FR-021, FR-025; CUR-016, CUR-018; SC-008, SC-010)
- [x] T137 [US4] Execute W5500 Cortex-M0+/M4, shared W6300, probe, and full-diagnostic cross-builds from `tests/Makefile` with warnings as errors and retain toolchain/version logs (FR-021, FR-025; CUR-016, CUR-018; SC-008, SC-010)
- [x] T138 [US4] Execute binary hardening and optimized PHY-delay checks from `tests/Makefile` and retain GNU_STACK, section, runtime-linkage, and disassembly reports (FR-014, FR-022, FR-025; CUR-011, CUR-016, CUR-018; SC-005, SC-008, SC-010; GNU Binutils `ld` manual "Options" entries `-z execstack`, `-z noexecstack`, `-z relro`, and `-pie`)
- [x] T139 [US4] Build, flash, and physically execute the smoke procedure in `tests/hardware/rp2040_w5500_probe/README.md`, recording both clean revisions, three address assignments, 100 UDP exchanges, RX-pointer advancement, and the complete transcript; mark BLOCKED if hardware is unavailable (FR-024-FR-026; CUR-018; SC-008-SC-010; W5500 Datasheet v1.1.0e sections 2.3 and 2.5)
- [x] T140 [US4] Build, flash, and physically execute every authoritative stage in `tests/hardware/rp2040_w5500_diag_full/README.md`, recording clean dual provenance, resource/PHY repetitions, multicore sockets, GPIO, lifecycle, recovery, and USB latency; mark BLOCKED if hardware is unavailable (FR-024-FR-026; CUR-001-CUR-019; SC-001-SC-010; USB 2.0 Specification section 11.18.2)

### Canonical Evidence and Documentation

- [x] T141 [US4] Populate exactly `CUR-001`-`CUR-019` and `AUD-001`-`AUD-073` rows from observed artifacts in `specs/005-fix-audit-findings/evidence.md`, using PASS/SUPERSEDED/NOT_APPLICABLE only as allowed by the evidence contract (FR-025-FR-027; CUR-018, CUR-019; SC-008, SC-010, SC-011)
- [x] T142 [US4] Run `tests/check_audit_evidence.py specs/005-fix-audit-findings/evidence.md`, fix every rejected row/artifact/status contradiction, and retain the actual release verdict without converting BLOCKED/FAIL to PASS (FR-025-FR-027; CUR-018, CUR-019; SC-010, SC-011)
- [x] T143 [US4] Reconcile contradictory counts, pending gates, and completion claims in `TODO.md` strictly from the validated evidence verdict (FR-025, FR-027; CUR-018, CUR-019; SC-010, SC-011)
- [x] T144 [US4] Replace the stale 49-finding/model-only summary with all 73 reconciled outcomes and current evidence links in `AUDIT-RESOLVED.md` (FR-025-FR-027; CUR-018, CUR-019; SC-010, SC-011)
- [ ] T145 [US4] Correct sanitizer, CBMC, hardware, and risk claims and distinguish host/model/smoke/full evidence in `docs/security/SECURITY-REVIEW-2026-07-21.md` (FR-025-FR-027; CUR-018, CUR-019; SC-010, SC-011)

**Checkpoint**: User Story 4 has a machine-validated, provenance-complete release
record and no mandatory unavailable or failing result is described as resolved.

---

## Phase 7: Polish and Cross-Cutting Release Check

**Purpose**: Confirm traceability and repository cleanliness after every story.

Release publication, merge, and human authorization remain out of scope under
`spec.md`; T149 supplies the final machine verdict for that separate decision.

- [ ] T146 Run the complete `specs/005-fix-audit-findings/quickstart.md` from clean matching root/nested revisions and retain its observed release result (FR-021-FR-027; SC-001-SC-011)
- [ ] T147 Run `git diff --check` in the root and `WIZnet-PICO-C/` repositories, retain output in `/tmp/opencode/w5500-005/evidence/diff-check.log`, and fail the gate on any whitespace error (FR-021, FR-025; SC-008, SC-010)
- [ ] T148 Run `python3 tests/check_audit_evidence.py specs/005-fix-audit-findings/evidence.md --traceability specs/005-fix-audit-findings/spec.md specs/005-fix-audit-findings/tasks.md`, retain output in `/tmp/opencode/w5500-005/evidence/traceability.log`, and fail the gate unless every FR, SC, CUR, and AUD identifier maps to a validated artifact (FR-025-FR-027; SC-008, SC-010, SC-011)
- [ ] T149 Run `tests/check_audit_evidence.py` against `specs/005-fix-audit-findings/evidence.md` and retain its unmodified final release verdict in `/tmp/opencode/w5500-005/evidence/final-verdict.log` without converting FAIL or BLOCKED to PASS (FR-025-FR-027; SC-010, SC-011)

---

## Dependencies and Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: T001 and T005 can begin in parallel; T002 follows T001;
  T003 and T004 follow T002 and can run in parallel.
- **Foundational (Phase 2)**: Depends on Phase 1 and blocks every story.
- **US1 (Phase 3)**: Depends on Phase 2.
- **US2 (Phase 4)**: Depends on Phase 2; it may proceed alongside US1 after
  root callback headers stabilize.
- **US3 (Phase 5)**: PHY work depends only on Phase 2; flag/reset work depends
  on US1 socket state; GPIO work depends on T050 and T098.
- **US4 (Phase 6)**: Infrastructure can begin after its production inputs are
  stable; mandatory executions and evidence depend on US1-US3 completion.
- **Polish (Phase 7)**: Depends on T142-T145 and all mandatory gate executions.

### User Story Dependencies

```text
Setup -> Foundation -> US1 --------------------------+
                    -> US2 --------------------------+-> US4 -> Polish
                    -> US3 (PHY) --------------------+
US1 socket state ----> US3 flags/reset               |
US2 mutex + US3 masks -> US3 GPIO -------------------+
```

- **US1** is the suggested MVP and is independently host-testable.
- **US2** is independently nested-host-testable after foundational root headers.
- **US3** has an independent test suite, but its flag/reset/GPIO slices consume
  explicit US1/US2 contracts described above.
- **US4** is the release decision layer and intentionally depends on all product
  behavior stories.

### Within Each Behavior Slice

- The adjacent test-implementation pairs shown above preserve red-green order.
- A test task must fail for the targeted defect before its paired implementation
  task starts.
- Implementation tasks run their paired test and all earlier phase tests.
- Hardware execution starts only after host, formal, and cross-build gates are
  available; hardware unavailability remains release-blocking.

---

## Parallel Opportunities

### User Story 1

After T021 establishes close/fault ownership, operation-family test tasks can be
prepared by separate workers only when they reserve different test files;
implementation tasks in `Ethernet/socket.c` remain sequential to avoid overlap.

### User Story 2

After T058 and T059, T060-T080 are fully parallel because each updates a
different example source file against the same checked API contract.

### User Story 3

T081, T087, T089, and T091 can start in parallel after their listed
dependencies because they use separate test files. PHY, flags, atomic setters,
and buffer-cache implementations touch distinct primary source regions, but
T094-T100 retain their explicit dependencies.

### User Story 4

T101 can proceed independently from build-gate work. Once diagnostics are
production-linked by T114 and provenance lands in T116, each red diagnostic
test may be prepared independently, but stage implementations are serialized
because they share the runner/catalog files.

---

## Parallel Execution Examples

### User Story 1

```text
Task: T022 [US1] socket/listen lock regression in tests/test_w5500_concurrency.c
Task: T024 [US1] OPEN/LISTEN deadline regression in tests/test_w5500_fault_injection.c
```

### User Story 2

```text
Task: T060 [US2] update examples/upnp/wizchip_upnp.c
Task: T061 [US2] update examples/loopback/wizchip_loopback.c
Task: T062 [US2] update examples/mqtt/publish_subscribe/wizchip_mqtt_publish_subscribe.c
```

### User Story 3

```text
Task: T081 [US3] PHY reset tests in tests/test_w5500_phy.c
Task: T087 [US3] raw flag matrix in tests/test_w5500_raw_flags.c
Task: T089 [US3] atomic setter tests in tests/test_w5500_atomic_pointer_write.c
Task: T091 [US3] buffer cache tests in tests/test_w5500_configuration.c
```

### User Story 4

```text
Task: T101 [US4] evidence validator tests in tests/test_check_audit_evidence.py
Task: T109 [US4] probe build contract in tests/test_hardware_harness_contracts.py
Task: T115 [US4] provenance tests in diagnostic C/Python tests
```

---

## Implementation Strategy

### MVP First: User Story 1

1. Complete T001-T017.
2. Complete T018-T044 in red-green order.
3. Stop and run the US1 independent test criteria.
4. Do not claim release readiness; US2-US4 remain mandatory.

### Incremental Delivery

1. **Foundation**: Truthful production-linked test seams and root contracts.
2. **US1**: Bounded, deadlock-free socket operations.
3. **US2**: Transactional and recoverable RP2040 transport.
4. **US3**: Correct PHY, flags, caches, coherent updates, and GPIO dispatch.
5. **US4**: Complete host/formal/cross/hardware evidence and reconcile status.
6. **Polish**: Repeat the complete clean-revision release workflow.

### Multi-Agent Rule

Each future agent assignment contains exactly one task ID from this file. Even
tasks marked `[P]` are dispatched as separate assignments; `[P]` permits
concurrent execution, not batching.

---

## Notes

- Every production behavior has a preceding failing regression task.
- `[P]` marks different files and no incomplete dependency.
- Root and nested repository commits/evidence remain separate.
- Replica models may remain supplemental but never satisfy production evidence.
- Hardware smoke cannot replace the authoritative diagnostic.
- No task may be deferred or marked complete without its stated verification.
