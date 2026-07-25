# Implementation Plan: Resolve Production Audit Findings

**Branch**: `005-fix-audit-findings` | **Date**: 2026-07-23 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/005-fix-audit-findings/spec.md`

## Summary

Remediate every verified W5500 core-driver and RP2040/W55RP20 transport finding as one release-readiness change. The design replaces recursive and leaking socket-lock paths with explicit lock ownership, bounds every hardware wait through a platform-neutral deadline policy, makes the PIO/DMA transport transactional and recoverable, separates SPI status from aliased data callbacks, preserves interrupt responsiveness with a bus mutex, defers GPIO dispatch out of ISR context, restores PHY/configuration coherence, and replaces unsupported assurance claims with production-linked host, formal, and hardware evidence.

## Technical Context

**Language/Version**: C11 for root driver, transport, diagnostics, and host tests; C++17 and PIO assembly only where required by Pico SDK/CMake; Python 3.11 or newer for diagnostic control and evidence validation
**Primary Dependencies**: WIZnet ioLibrary W5500 core; WIZnet-PICO-C 2.2.0; Raspberry Pi Pico SDK 2.2.0 (`pico_stdlib`, `pico_sync`, `pico_time`, `hardware_pio`, `hardware_dma`, `hardware_gpio`, `hardware_clocks`, TinyUSB, watchdog); POSIX pthreads for host concurrency tests
**Storage**: No runtime persistence or heap allocation; static driver/transport state and memory-mapped registers; Markdown release evidence generated under the feature directory
**Testing**: GNU Make host suite; GCC/Clang strict C11; ASan+UBSan; separate TSan lane; production-linked SPI/register fake; CBMC harness over production source; cppcheck/clang-tidy/Clang analyzer; `arm-none-eabi-gcc`; CMake/CTest diagnostic host tests; RP2040 hardware smoke and full diagnostic
**Target Platform**: W5500 on W55RP20/RP2040 Cortex-M0+ for acceptance; Linux host for deterministic tests and analysis; W6300 shared-source compile compatibility
**Project Type**: Embedded C library with a nested platform-port repository and host/hardware verification harnesses
**Performance Goals**: Preserve independent operation of eight sockets; complete 10,000 mixed concurrent socket operations without deadlock or lost state; service the documented interrupt source within its derived deadline during a maximum 16 KiB transfer; preserve three address cycles and 100 datagram exchanges
**Constraints**: Non-recursive socket and bus locks; no payload-wide interrupt masking; no unbounded hardware/DMA/PIO waits; no unsafe unclaim of active resources; zero-length I/O is a no-op; one active PIO transport instance; maximum W5500 operation equals configured socket capacity up to 16 KiB; no W6300-only behavior change; hardware-dependent failures remain release blockers
**Scale/Scope**: Eight hardware sockets, two PIO blocks, four state machines per PIO, 32 instruction words per PIO, 12 DMA channels, 27 functional requirements, 11 success criteria, 73 historical audit IDs plus 19 current audit categories

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

The file `.specify/memory/constitution.md` is an unratified template containing only placeholder principle and governance fields. It defines no enforceable project principles, version, ratification date, or quality gates.

**Pre-design gate result**: PASS. There is no constitutional requirement to violate or justify. The feature specification and repository instructions remain binding: production-linked evidence, bounded failure behavior, no silent deferral, no unsupported completion claims, and hardware verification where required.

## Project Structure

### Documentation (this feature)

```text
specs/005-fix-audit-findings/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── evidence.md                 # Created and populated during implementation
├── contracts/
│   ├── core-driver-api.md
│   ├── rp2040-transport-api.md
│   └── verification-evidence.md
└── checklists/
    └── requirements.md
```

### Source Code (repository root)

```text
Ethernet/
├── socket.c                    # Socket validation, lock ownership, waits, flags, recovery
├── socket.h                    # Error and zero-length/public socket contracts
├── wizchip_conf.c              # Lifecycle, callbacks, deadlines, PHY, caches, transactions
├── wizchip_conf.h              # Public callback/time/lock/status contracts
└── W5500/
    ├── w5500.c                 # Checked SPI transactions and coherent access helpers
    └── w5500.h                 # Register accessors and 16-bit setter routing

WIZnet-PICO-C/                  # Nested transport repository
├── CMakeLists.txt
├── port/
│   ├── CMakeLists.txt
│   └── ioLibrary_Driver/
│       ├── inc/
│       │   ├── wizchip_spi.h
│       │   ├── wizchip_qspi_pio.h
│       │   └── wizchip_gpio_irq.h
│       └── src/
│           ├── wizchip_spi.c
│           ├── wizchip_qspi_pio.c
│           ├── wizchip_qspi_pio.pio
│           └── wizchip_gpio_irq.c
└── tests/
    ├── CMakeLists.txt
    ├── fakes/
    ├── test_wizchip_qspi_pio.c
    └── test_wizchip_gpio_irq.c

tests/
├── Makefile                     # Out-of-tree host/analysis targets and strict tool lanes
├── check_audit_evidence.py
├── support/
│   ├── w5500_spi_model.c
│   └── w5500_spi_model.h
├── cbmc/
│   ├── Makefile
│   └── w5500_production_harness.c
├── test_w5500_atomic_pointer_write.c
├── test_w5500_correctness.c
├── test_public_api_sanitizer.c
├── test_w5500_concurrency.c
├── test_w5500_fault_injection.c
├── test_w5500_raw_flags.c
├── test_w5500_phy.c
└── hardware/
    ├── rp2040_w5500_probe/
    │   └── README.md             # Smoke setup and current three-cycle/100-exchange procedure
    └── rp2040_w5500_diag_full/
        └── README.md             # Correct _full paths, provenance, flash, and release commands

docs/security/
└── SECURITY-REVIEW-2026-07-21.md

AUDIT-RESOLVED.md
TODO.md
```

**Structure Decision**: Preserve the established root ioLibrary layout and nested WIZnet-PICO-C port. Add focused test-support files rather than embedding a handwritten model in each test. The root and nested repositories receive coordinated source commits; hardware harnesses always compile those production sources directly. Deadline, lock, and status callbacks remain lightweight contracts inside the existing driver modules; no standalone runtime layer or general-purpose framework is introduced.

## Architecture

### Core Driver

- Validate immutable arguments before locking; validate mutable socket state after acquiring the socket lock.
- Replace shared socket bitfields with per-socket state arrays.
- Use internal lock-aware cleanup helpers and one post-lock exit per public operation.
- Keep a socket faulted after ambiguous deadline/I/O failure until bounded close or explicit chip reset establishes a known state.
- Add monotonic-time and wait-hook callbacks with `_WIZCHIP_POLL_MAX_` as a no-clock fallback.
- Move SPI status outside the interface union and retain failures as sticky lifecycle state until explicit recovery.
- Protect chip-wide logical transactions and cache refreshes with the global lock.
- Route every remaining 16-bit setter through one VDM transaction.

### RP2040 Transport

- Replace the payload-wide Pico critical section with a non-recursive `mutex_t` bus lock; reserve `critical_section_t` for short ISR-visible state changes only.
- Copy transport configuration, claim PIO program/state machine atomically with the SDK helper, claim DMA channels, then publish READY state.
- Poll DMA completion, errors, and PIO completion against absolute deadlines.
- On timeout, disable PIO, issue a bounded DMA abort request, discard partial data, and quarantine any channel that cannot retire; never reset global DMA or unclaim active hardware.
- Serialize transfer, close, sleep, wake, reset, and reinitialize through an explicit lifecycle state machine.
- Keep legacy data callback signatures and expose transport failure through independent busy/error/clear callbacks.

### GPIO and Events

- Install a raw handler only for `PIN_INT`; acknowledge and record pending state in ISR context.
- Read/clear W5500 interrupt sources and invoke per-socket callbacks only from task-context dispatch.
- Merge registration masks, support unregister, and remove/disable the raw handler when no registrations remain.

### Verification

- Repair harness defects before changing production behavior, and verify each new regression fails for the audited reason before its fix.
- Link all host tests and CBMC harnesses to production source.
- Keep generated binaries, reports, and captured logs in a caller-selected out-of-tree build directory so evidence collection does not dirty either candidate worktree.
- Run ASan+UBSan and TSan separately.
- Make the full diagnostic authoritative for resource, lifecycle, PHY, GPIO, concurrency, and latency evidence; retain the lean probe as a smoke prerequisite.
- Generate a canonical evidence matrix before correcting historical status documents.

## Phase 0: Research Outcome

`research.md` resolves platform scope, lock ordering, deadline policy, transport allocation and lifecycle, DMA abort behavior, error propagation, GPIO context, flag masks, PHY timing, cache coherence, transfer limits, verification architecture, hardware-harness ownership, and audit reconciliation. Vendor-undefined timing values are identified as configurable engineering defaults rather than normative guarantees. No unresolved clarification remains.

## Phase 1: Design Outputs

- `data-model.md` defines socket state, transport state/resource ownership, callback registration, configuration cache, deadline policy, and audit evidence transitions.
- `contracts/core-driver-api.md` defines error codes, lock/time/status registration, zero-length behavior, flags, and lifecycle/fault semantics.
- `contracts/rp2040-transport-api.md` defines transactional open, state-returning lifecycle operations, sticky transfer status, and deferred GPIO registration/dispatch.
- `contracts/verification-evidence.md` defines required evidence fields, accepted methods, result states, and release gating.
- `quickstart.md` provides the host, analysis, cross-compile, hardware smoke, full diagnostic, and evidence-validation commands with expected outcomes.

## Delivery Strategy

1. Establish truthful, production-linked host harnesses and failing regressions for each confirmed root cause.
2. Correct core lock ownership, per-socket state, deadlines, fault propagation, flags, PHY, caches, and coherent writes in independently reviewable behavior slices.
3. Correct transport allocation/lifecycle, bus locking, bounded completion, error latch, and GPIO deferred dispatch in the nested repository.
4. Integrate root and transport contracts, then run complete host, static, formal, cross-compile, and optimized-build gates.
5. Run smoke and full hardware acceptance, generate `evidence.md`, reconcile all audit IDs, and update security/status documentation from observed results.

The following `/speckit.tasks` phase must decompose these slices into coherent atomic tasks; no task may combine independently reviewable findings merely because they share a source file.

## Post-Design Constitution Check

The constitution remains an unratified placeholder and contributes no additional design gate. Treating it as non-normative is an explicit decision for this feature, not a ratification or reinterpretation; constitution adoption is a separate governance workflow. The design introduces no unexplained framework, persistence layer, heap allocator, or W6300 feature expansion. Complexity is limited to synchronization domains, explicit state, deadline/error contracts, and verification infrastructure required directly by FR-001 through FR-027.

**Post-design gate result**: PASS.

## Complexity Tracking

No constitution violations require justification. The coordinated root/nested-repository change is required by the audited runtime boundary and is tracked as one feature because neither side can independently provide end-to-end error propagation or release evidence.
