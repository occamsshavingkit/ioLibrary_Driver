# Feature Specification: Resolve Production Audit Findings

**Feature Branch**: `005-fix-audit-findings`
**Created**: 2026-07-23
**Status**: Draft
**Input**: User description: "fix all the audit findings."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Dependable Socket Operations Under Concurrency (Priority: P1)

As an embedded-system integrator, I can use every supported socket operation from concurrent execution contexts without deadlocks, leaked locks, corrupted shared state, or indefinite waits, so networking remains available under normal load and hardware faults.

**Why this priority**: Current lock recursion and lock-lifetime defects can stop all forward progress, while shared-state races can corrupt unrelated sockets. These are release-blocking failures in the primary product path.

**Independent Test**: Exercise open, connect, listen, send, receive, disconnect, and close operations concurrently across all socket numbers while injecting malformed states and commands that never complete. Verify that every operation completes or reports a bounded failure, all acquired locks are released exactly once, and unrelated sockets retain correct state.

**Acceptance Scenarios**:

1. **Given** a socket operation encounters a state that requires cleanup, **When** cleanup closes or resets that socket, **Then** the operation completes without reacquiring a lock already held by the same execution context.
2. **Given** an invalid socket number or malformed request, **When** the request is rejected, **Then** no socket lock is acquired and no shared state changes.
3. **Given** a socket lock was acquired and any later success, timeout, hardware-failure, or validation branch returns, **When** control leaves the operation, **Then** that acquisition has exactly one matching release and a later valid operation can use the socket.
4. **Given** two execution contexts update state for different sockets at the same time, **When** both updates complete, **Then** both socket state changes are preserved.
5. **Given** the device never clears a command or never reaches an expected state, **When** a socket operation waits for that condition, **Then** it returns an explicit timeout or hardware-failure result within the configured deadline.

---

### User Story 2 - Safe and Recoverable RP2040 Transport (Priority: P2)

As a firmware developer, I receive an explicit and actionable result when the RP2040 communication transport cannot initialize or complete a transfer, so the application can recover without a crash, hang, poisoned resource state, or silent data loss.

**Why this priority**: Resource exhaustion can currently leave an invalid transport handle that is later dereferenced, and stalled transfers can wait forever or falsely report success. These failures make recovery impossible and can crash the device.

**Independent Test**: Force each instruction-memory, state-machine, transfer-channel, and bus-completion failure in turn, repeat initialization after every failure, and exercise zero-length through maximum-length transfers. Verify explicit failure reporting, bounded completion, complete cleanup, safe retry, and preserved interrupt responsiveness.

**Acceptance Scenarios**:

1. **Given** any required transport resource is unavailable, **When** initialization is attempted, **Then** initialization reports failure, leaves no usable-looking invalid handle, and releases every resource acquired by that attempt.
2. **Given** initialization previously failed partway through, **When** resources become available and initialization is retried, **Then** the retry succeeds without restarting the device.
3. **Given** a transfer stalls or reports a hardware error, **When** its deadline expires or the error is detected, **Then** the caller receives a failure and the transport remains recoverable.
4. **Given** a zero-length transfer, **When** it is requested, **Then** the operation succeeds as a no-op without reading from or writing to the caller's buffer.
5. **Given** the maximum supported transfer is active, **When** unrelated time-critical interrupts occur, **Then** they are serviced within the documented interrupt-latency budget and are not masked for the duration of the transfer.
6. **Given** the transport is closed, put to sleep, woken, or reinitialized in any supported order, **When** the lifecycle transition completes, **Then** resources are neither leaked nor released twice and the reported state matches actual usability.

---

### User Story 3 - Correct Configuration, PHY, and Interrupt Behavior (Priority: P3)

As a product integrator, I can trust configuration changes, physical-link controls, raw-socket options, and GPIO interrupt callbacks to reflect the hardware and documented API contract, so the device behaves predictably without hidden stale state or unrelated callbacks.

**Why this priority**: Incorrect reset and power sequencing, stale caches, rejected valid flags, split configuration writes, and unfiltered interrupt dispatch produce field failures that are difficult to diagnose and can affect unrelated consumers.

**Independent Test**: Cycle physical-link power and reset state, change every cached network and buffer configuration, use every documented raw-socket flag, and trigger both owned and unrelated GPIO events. Compare callback values and readback state with the device's observable state after every operation.

**Acceptance Scenarios**:

1. **Given** the physical link is powered down and then restored, **When** the power controls complete, **Then** unrelated configuration bits are preserved and the hardware reaches the requested power state.
2. **Given** a physical-link reset is requested in an optimized build, **When** the reset completes, **Then** the reset and stabilization intervals satisfy the documented minimums and the final state is operational.
3. **Given** the physical-link status changes, **When** a registered callback is invoked, **Then** the callback value matches the hardware link state observed for that event.
4. **Given** a caller supplies a documented raw-socket option or a valid combination of options, **When** the socket is opened, **Then** the options are accepted and applied; undocumented bits are rejected without side effects.
5. **Given** network, retry, memory-allocation, or reset configuration changes, **When** callers read the associated cached or hardware-backed values, **Then** they observe the new values consistently across every socket.
6. **Given** an unrelated GPIO event occurs, **When** interrupt dispatch runs, **Then** no network callback is invoked; only valid registered network interrupt sources are dispatched.

---

### User Story 4 - Trustworthy Release Evidence (Priority: P4)

As a maintainer deciding whether to release the driver, I can rely on automated checks and project documentation to represent the production implementation and its actual results, so a release cannot be approved from stale, modeled-only, or false-success evidence.

**Why this priority**: Existing correctness and public-interface checks either crash or fail, the formal model does not exercise production code, and completion documents claim issues are resolved despite contrary evidence.

**Independent Test**: Run the complete host test suite with memory and undefined-behavior checking, production-linked formal or exhaustive checks, static analysis, optimized-build verification, and the defined hardware regression suite. Confirm every audit requirement has passing evidence and that release documents report the observed results without unsupported claims.

**Acceptance Scenarios**:

1. **Given** the host correctness suite is built with supported runtime safety checks, **When** it runs, **Then** it completes without crashes, sanitizer findings, executable-stack requirements, or skipped audit regressions.
2. **Given** public-interface regression tests are run against production sources, **When** all cases execute, **Then** all expected success and failure contracts pass without substituting replicated implementations for production logic.
3. **Given** bounded or formal analysis is used as release evidence, **When** its result is reported, **Then** it analyzes the production source directly or a mechanically extracted unit with no handwritten replacement behavior, and its assumptions do not incorrectly serialize independent sockets.
4. **Given** any required check fails or cannot run, **When** release status is documented, **Then** the failure or unavailable check is stated explicitly and the audit is not marked resolved.
5. **Given** all required checks pass, **When** the audit status is updated, **Then** every finding links to its corrective requirement and passing regression evidence.
6. **Given** historical audit findings are already marked resolved, **When** release evidence is assembled, **Then** every historical finding remains supported by current evidence or is explicitly superseded by a current corrective requirement.

### Edge Cases

- All hardware socket slots are occupied when another socket is requested.
- A socket number is outside the hardware-supported range, including the first value immediately above the range.
- A null pointer, missing callback, zero-length buffer, or unsupported flag combination reaches a public operation.
- A command register, transfer completion signal, or physical-link state never reaches the expected value.
- Instruction memory, state machines, or transfer channels are exhausted individually and in combination.
- Initialization fails after acquiring only some resources, followed by immediate retry and repeated initialize/close cycles.
- Close, sleep, wake, reset, and transfer operations overlap or arrive in an unexpected but supported lifecycle order.
- Two contexts update different bits in a shared socket-state field at the same time.
- Two contexts request operations on the same socket at the same time and must serialize without recursive locking, corruption, or lock leakage.
- A physical link is disconnected during reset, power transition, or status notification.
- Network buffer sizes or common network settings change while another context reads them.
- A maximum-length transfer occurs while USB, timer, or other latency-sensitive interrupts are active.
- Network and unrelated GPIO sources assert simultaneously.
- The same test is compiled without optimization, with production optimization, and with runtime safety instrumentation.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Socket operations MUST complete without recursive acquisition of a non-recursive socket lock, including every cleanup and nested-operation path.
- **FR-002**: Public socket operations MUST validate the socket identifier and other preconditions before acquiring a socket-specific lock.
- **FR-003**: Every successful lock acquisition MUST have exactly one release on every success, validation-failure, timeout, hardware-failure, and cleanup path.
- **FR-004**: Concurrent updates to shared socket state MUST preserve all non-conflicting updates, including updates for different sockets.
- **FR-005**: Every wait for a hardware command, state transition, or transfer completion MUST be bounded by a documented deadline and MUST return an explicit failure when the condition is not met.
- **FR-006**: A timed-out or failed operation MUST leave the affected socket or transport in a documented state that permits cleanup, reset, or retry without restarting the device.
- **FR-007**: Transport initialization MUST expose success or failure to its caller and MUST prevent use of an invalid or incomplete transport instance.
- **FR-008**: A failed initialization attempt MUST release all resources acquired by that attempt and MUST leave allocation state reusable by a later attempt.
- **FR-009**: Registration and readiness status for byte transfers, burst transfers, and critical-section callbacks MUST remain independent so changing one cannot falsely validate, invalidate, or overwrite another.
- **FR-010**: Transport and device-operation failures MUST propagate to the initiating caller rather than being converted to success or observable only through diagnostic output.
- **FR-011**: Zero-length reads and writes MUST be safe no-ops and MUST NOT access a caller buffer.
- **FR-012**: Initialize, close, sleep, wake, reset, and reinitialize transitions MUST be idempotent where documented and MUST prevent resource leaks, double release, and use-after-close.
- **FR-013**: Network transfers MUST preserve the documented interrupt-latency budget; time-critical interrupts MUST NOT be blocked for the full duration of a payload transfer.
- **FR-014**: Physical-link power and reset controls MUST preserve unrelated register fields, request the intended hardware state, satisfy documented timing constraints, and report inability to reach that state.
- **FR-015**: Physical-link status notifications MUST report the hardware state associated with the event rather than a constant, stale, or inferred value.
- **FR-016**: Every documented raw-socket option and valid option combination MUST be accepted and applied, while unsupported bits MUST be rejected without partial configuration.
- **FR-017**: Cached network, retry, socket-memory, and reset-related values MUST remain coherent with successful configuration changes and device reset outcomes.
- **FR-018**: A logical multi-byte or multi-register configuration update MUST appear as one consistent change to concurrent users and MUST NOT expose a partially updated value.
- **FR-019**: GPIO interrupt registration and dispatch MUST reject invalid socket identifiers, ignore unrelated GPIO sources, and invoke only the callback associated with a valid asserted source.
- **FR-020**: Public operations MUST reject invalid pointers, unsupported values, unavailable callbacks, and invalid lifecycle states without a crash, undefined behavior, lock leak, resource leak, or unintended hardware access.
- **FR-021**: Regression tests MUST exercise production implementation paths for all audit findings and MUST include success, boundary, injected-failure, timeout, cleanup, retry, and concurrency cases.
- **FR-022**: Host tests MUST run under supported memory and undefined-behavior checking without sanitizer findings, harness crashes, or an executable-stack requirement.
- **FR-023**: Formal, bounded, or model-based evidence MUST analyze the same source functions used in production, either linked directly or mechanically extracted without handwritten replacement behavior; the evidence MUST identify the source revision, analyzed functions, extraction procedure, and assumptions, and MUST model independent socket activity without assuming global serialization that production does not provide.
- **FR-024**: Hardware regression evidence MUST retain the established version-register, address-assignment, datagram send/receive, and receive-pointer behavior while adding real locking, fault-path, physical-link, lifecycle, and resource-exhaustion coverage.
- **FR-025**: Audit and security documentation MUST state actual check outcomes, distinguish host, model, and hardware evidence, and MUST NOT claim completion while any required finding lacks passing evidence.
- **FR-026**: Every confirmed audit finding, regardless of severity, MUST map to at least one corrective requirement and one passing regression or verification result before the feature is considered complete.
- **FR-027**: Historical findings `AUD-001` through `AUD-073` MUST be reconciled against the remediated production code as still passing, superseded by a current corrective requirement, or excluded by a documented and verified scope decision; a historical resolved claim MUST NOT remain when its underlying behavior fails current verification.

### Key Entities

- **Socket operation state**: The lifecycle state, ownership status, pending command, per-socket mode, and shared bookkeeping associated with one hardware socket. `HEALTHY` permits normal operations; `FAULTED` rejects normal operations until bounded close or verified chip reset restores a known state.
- **Transport instance**: The communication path's initialization result, acquired hardware resources, transfer status, deadline state, and lifecycle state.
- **Callback registration state**: Independent registrations and readiness status for byte, burst, critical-section, physical-link, and GPIO notifications.
- **Device configuration state**: Hardware-backed and cached network, retry, socket-memory, raw-mode, reset, power, and physical-link values that must remain coherent.
- **Audit evidence record**: A finding identifier, applicable requirement, source revision, verification method, observed result, evidence location, and release-blocking status.

### Audit Coverage

| Current ID | Confirmed finding category | Covered by |
|------------|----------------------------|------------|
| `CUR-001` | Recursive socket lock acquisition and deadlock | FR-001, FR-003, FR-021 |
| `CUR-002` | Invalid socket locking and lock leaks | FR-002, FR-003, FR-020, FR-021 |
| `CUR-003` | Shared socket-bitfield lost updates | FR-004, FR-021, FR-023 |
| `CUR-004` | Unbounded polling and false-success completion | FR-005, FR-006, FR-010, FR-021 |
| `CUR-005` | Null transport handle after initialization failure | FR-007, FR-008, FR-020, FR-021 |
| `CUR-006` | Partial allocation leaks and poisoned allocation state | FR-008, FR-012, FR-021 |
| `CUR-007` | Transfer stalls, silent failures, and unsafe zero-length writes | FR-005, FR-006, FR-010, FR-011, FR-021 |
| `CUR-008` | Transport lifecycle leaks, double release, and invalid reuse | FR-006, FR-012, FR-021 |
| `CUR-009` | Full-transfer interrupt blackout | FR-013, FR-024 |
| `CUR-010` | Aliased callback-registration status | FR-009, FR-021 |
| `CUR-011` | Physical-link power, reset, delay, and callback defects | FR-014, FR-015, FR-021, FR-024 |
| `CUR-012` | Rejected documented raw-socket flags | FR-016, FR-021 |
| `CUR-013` | Stale configuration caches and split setters | FR-017, FR-018, FR-021 |
| `CUR-014` | Invalid or unrelated GPIO interrupt dispatch | FR-019, FR-020, FR-021 |
| `CUR-015` | Public-interface validation and hardening gaps | FR-020, FR-021 |
| `CUR-016` | Crashing sanitizer harness and failing public-interface suite | FR-021, FR-022 |
| `CUR-017` | Non-production or over-serialized bounded model | FR-004, FR-023 |
| `CUR-018` | Inaccurate audit completion and verification claims | FR-025, FR-026 |
| `CUR-019` | Historical `AUD-001` through `AUD-073` resolution claims | FR-025, FR-027 |

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A stress run of at least 10,000 mixed socket lifecycle and data operations across all hardware sockets, with at least two concurrent execution contexts, completes with zero deadlocks, lock leaks, lost non-conflicting state updates, sanitizer findings, or unexpected failures.
- **SC-002**: Every injected stuck-command, stalled-transfer, unavailable-callback, invalid-input, and hardware-failure case returns the documented result within its configured deadline plus a 10% measurement tolerance; none hangs or crashes.
- **SC-003**: For each transport resource type, 100 consecutive forced partial-initialization failures followed by successful initialization and close complete with zero leaked resources, double releases, or device restarts.
- **SC-004**: During maximum-length network transfers, every scheduled latency-sensitive interrupt is serviced within the deadline derived from that interrupt source's documented service requirement; the release evidence identifies the source document, revision, section, derived deadline, event count, worst-case latency, and comparison with the pre-remediation full-transfer blackout.
- **SC-005**: Across 100 physical-link disconnect, reconnect, power-down, power-up, and reset cycles, requested states and callback values match hardware observations every time, and measured reset timing meets the documented minimum in both debug and production-optimized builds.
- **SC-006**: Tests cover 100% of documented raw-socket flags and valid combinations selected by the public contract, with all valid cases accepted and all cases containing unsupported bits rejected without side effects.
- **SC-007**: Reconfiguration and reset tests across all hardware sockets report zero cache-versus-hardware mismatches and zero observations of partial logical configuration values.
- **SC-008**: Every confirmed audit finding has a traceable regression or verification case that passes against production code; the complete host suite reports zero failures under the supported memory and undefined-behavior checkers and requires no executable stack.
- **SC-009**: The established hardware happy path passes for three consecutive address-assignment cycles and 100 consecutive datagram send/receive exchanges, including correct receive-pointer advancement, with no regression from the pre-remediation baseline.
- **SC-010**: Release documentation lists the result and evidence source for every required check, contains zero unsupported completion claims, and leaves the audit blocked whenever any mandatory result is failing or unavailable.
- **SC-011**: The reconciliation record accounts for all 73 historical audit identifiers with zero unexplained omissions and zero findings marked resolved when their current production regression evidence fails.

## Assumptions

- The remediation scope includes the W5500 core driver in the root repository and the RP2040 communication and GPIO integration in the nested transport repository because both participate in the audited product path.
- The existing successful public behavior remains supported. Behavior that deadlocks, crashes, silently succeeds after failure, leaks resources, or accepts invalid state is not a compatibility contract.
- Socket locks remain non-recursive as documented; callers and internal operations must work correctly under that contract.
- A supported RP2040 hardware target, W5500 device, network peer, and means of measuring interrupt latency and physical-link state are available for release verification.
- The prior audit transcript establishes that the target hardware was accessible. If it becomes unavailable, hardware-dependent criteria remain release blockers rather than being replaced by host-only evidence.
- Hardware-independent fault injection may be used to reproduce resource exhaustion, stuck commands, stalled transfers, and callback failures, but passing evidence must exercise the same production paths used on hardware.
- The latest verified audit is the corrective baseline for this feature. The 73 earlier `AUD-001` through `AUD-073` records remain regression obligations and are reconciled because the latest audit invalidated some prior completion claims.
- Numeric repetition counts in the success criteria are minimum release thresholds intended to expose intermittent state and resource failures; planning may raise them when supported by risk evidence but must not silently reduce them.
- Findings discovered while correcting or testing a confirmed finding are included when they are necessary to satisfy that finding's requirement; unrelated feature development is excluded.

## Out of Scope

- New network protocols, socket modes, or chip capabilities unrelated to a confirmed audit finding.
- Broad architectural rewrites or performance optimization beyond what is required for correctness, bounded completion, and interrupt responsiveness.
- W6300-only behavior, except that shared interfaces changed by this feature must continue to compile and retain their established successful behavior.
- Publishing, upstreaming, or merging the remediation; those actions require separate explicit approval after implementation and verification.
