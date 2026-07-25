# Contract: Verification and Audit Evidence

## Canonical Artifact

Implementation creates `specs/005-fix-audit-findings/evidence.md`. It is the canonical release record for this feature. `TODO.md`, `AUDIT-RESOLVED.md`, and security-review prose may summarize it but may not claim a stronger result.

## Required Evidence Record

Every current category `CUR-001` through `CUR-019` and every historical `AUD-001` through `AUD-073` has exactly one row with:

| Field | Requirement |
|-------|-------------|
| Finding | Stable current identifier or historical AUD identifier. |
| Requirements | One or more FR identifiers. |
| Root revision | Full root Git SHA and dirty status. |
| Transport revision | Full nested Git SHA and dirty status when applicable. |
| Method | Accepted method identifier. |
| Command | Exact reproducible command. |
| Evidence location | Test/log/report path and relevant assertion/stage. |
| Result | `PASS`, `FAIL`, `BLOCKED`, `NOT_APPLICABLE`, or `SUPERSEDED`. |
| Observed at | UTC timestamp. |
| Notes | Measured thresholds, source citation, superseding ID, or verified scope reason. |
| Release blocker | `YES` or `NO`. |

No row may use `NOT_APPLICABLE` without a verified chip/path scope reason. No row may use `SUPERSEDED` without a replacement finding and evidence covering the original behavior.

### Canonical Markdown Shape

`evidence.md` uses this exact heading and column order so
`tests/check_audit_evidence.py` can parse it deterministically:

```markdown
# Feature 005 Release Evidence

| Finding | Requirements | Root revision | Transport revision | Method | Command | Evidence location | Result | Observed at | Notes | Release blocker |
|---------|--------------|---------------|--------------------|--------|---------|-------------------|--------|-------------|-------|-----------------|
| CUR-001 | FR-001, FR-003, FR-021 | 0123456789abcdef0123456789abcdef01234567 (clean) | 89abcdef0123456789abcdef0123456789abcdef (clean) | host-production, tsan | `make -C tests test`<br>`make -C tests tsan` | `artifacts/host.log#CUR-001`<br>`artifacts/tsan.log#CUR-001` | PASS | 2026-07-23T12:00:00Z | Non-recursive lock regression and stress evidence. | NO |
```

The example row defines syntax only; implementation replaces it with observed
candidate evidence. Multiple methods, commands, and evidence locations use
comma-separated method IDs and same-order `<br>` entries. Literal `|`
characters inside a field are encoded as `&#124;`. Revisions are full 40-digit
SHAs followed by `(clean)` or `(dirty)`; mandatory PASS rows require `(clean)`.
Root-only rows use `NOT_APPLICABLE: root-only` in Transport revision. Times use
UTC `YYYY-MM-DDTHH:MM:SSZ` format.

## Accepted Methods

| Method | Minimum validity |
|--------|------------------|
| `host-production` | Links current production sources and drives public/integration APIs through the SPI/register fake. |
| `asan-ubsan` | Fresh binary contains sanitizer instrumentation and exits with zero findings/failures. |
| `tsan` | Separate fresh pthread binary links production sources and exits with zero races/failures. |
| `cbmc-production` | Harness analyzes current production source directly with recorded assumptions; no replicated driver state machine. |
| `static-analysis` | Tool/version/configuration and zero actionable findings recorded. |
| `cross-compile` | W5500 Cortex-M0+/M4 and shared W6300 compile targets recorded with warnings as errors. |
| `optimized-check` | Production-optimized build/disassembly verifies observable delay calls and non-executable stack. |
| `hardware-smoke` | Lean probe runs exact production sources and records required cycles/exchanges. |
| `hardware-diagnostic` | Full diagnostic records provenance, stage inputs, thresholds, measured values, and outcomes. |

Handwritten replica models may appear as supplemental evidence labeled `model-non-production`; they cannot satisfy FR-023 or close a release blocker.

## Required Verification Matrix

| Gate | Mandatory outcome |
|------|-------------------|
| Strict host build | GCC and Clang compile all root test targets with no warnings/errors. |
| ASan+UBSan | All production-linked host tests pass with zero sanitizer findings and no executable-stack requirement. |
| TSan concurrency | At least 10,000 mixed operations with two or more contexts pass with zero races, deadlocks, lock leaks, or state loss. |
| Fault injection | Every stuck-command, stalled-transfer, invalid input, unavailable callback, and hardware failure returns within documented deadline plus tolerance. |
| Transport resource cycles | Each resource failure point completes 100 failure/retry/close cycles with no unsafe release or restart. |
| Flag matrix | Every documented valid flag/combination passes; every unsupported bit/dependency fails without side effects. |
| Configuration coherence | All sockets show zero cache/readback mismatches and zero partial logical updates. |
| Formal/bounded | Production-linked assumptions and results are recorded; independent sockets are not globally serialized by the model. |
| Static/cross-build | Required analyzers and target builds have zero actionable findings. |
| Hardware smoke | Three address-assignment cycles and 100 datagram exchanges pass with correct RX pointer advancement. |
| Hardware diagnostic | PHY cycles, GPIO filtering, lifecycle, resource exhaustion, timeout recovery, real locks, and interrupt latency all pass. |
| Historical reconciliation | Exactly 73 historical IDs are present with no unexplained omission or false resolved status. |

## Hardware Latency Evidence

The diagnostic record includes:

- Interrupt source and authoritative service requirement.
- Source document, revision, and section.
- Derived deadline in microseconds.
- Transfer length and measured SPI clock.
- Event count, worst-case latency, missed events, and pre-remediation blackout comparison.

The mandatory release measurement schedules USB SOF service. Cite USB 2.0 section 11.18.2 and its 1 ms full-speed frame interval, and require every event scheduled by that measurement to meet the derived deadline. Additional scheduled sources require their own authoritative source and derivation. The evidence must not describe the USB interval as a universal RP2040 maximum.

## Result and Release Rules

- Every `CUR-001` through `CUR-019` row is mandatory and must be PASS.
- A historical AUD row may be PASS, or may be `SUPERSEDED`/`NOT_APPLICABLE` only when its required replacement/scope evidence passes validation.
- Any FAIL, BLOCKED, invalid supersession, or invalid scope result blocks completion.
- Hardware unavailability leaves hardware-dependent rows BLOCKED.
- Hardware is unavailable when any required target, W5500 device, USB access,
  network peer, physical-link control, or latency-measurement facility is absent
  or inoperable when a mandatory stage is executed. A partial stage, prior
  transcript, or host substitute cannot clear that BLOCKED result.
- A fresh PASS may replace FAIL/BLOCKED only with a recorded command and evidence.
- A test binary name does not prove instrumentation; evidence records the build command and linked runtime/tool report.
- Model-only success cannot override production-linked failure.
- Documentation is updated only after the matrix reflects observed results.
- Release readiness requires every mandatory current requirement to pass and all 73 historical IDs to be reconciled.

## Evidence Validation

The implementation must provide a deterministic checker that fails when:

- an AUD identifier is missing or duplicated;
- a CUR identifier is missing or duplicated;
- a required field is empty;
- a current CUR row is not PASS;
- a historical AUD row is FAIL/BLOCKED or has an invalid supersession/scope claim;
- a PASS row references a missing evidence location;
- a hardware-dependent row is passed using host-only evidence;
- a production-linkage requirement cites `model-non-production` only;
- status documents claim completion while the canonical gate is blocked.
