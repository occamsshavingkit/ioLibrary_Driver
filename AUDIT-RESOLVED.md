# W5500 Audit Finding Reconciliation

**Feature branch**: `005-fix-audit-findings`

**Reconciled at**: 2026-07-24T00:44:09Z

**Canonical record**: [Feature 005 release evidence](specs/005-fix-audit-findings/evidence.md)

**Evidence contract**: [Verification evidence contract](specs/005-fix-audit-findings/contracts/verification-evidence.md)

This document supersedes the 2026-07-18 summary that reported 49 findings as
resolved from branch descriptions and model-only checks. The current audit
baseline contains 19 corrective categories and reconciles all 73 historical
findings. The canonical evidence matrix, not this summary, is authoritative.

No finding is currently release-ready. All `CUR-001` through `CUR-019` rows are
`BLOCKED`, so no historical `SUPERSEDED` row is claimed as resolved. Historical
rows remain release blockers until their replacement `CUR` evidence passes.

## Current Evidence Status

| Current finding | Result | Current evidence | Blocking condition |
|-----------------|--------|------------------|--------------------|
| `CUR-001` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`, `root-tsan.log`) | Clean retained host and TSan evidence is pending. |
| `CUR-002` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`, `root-tsan.log`) | Clean retained lock and validation evidence is pending. |
| `CUR-003` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-tsan.log`, `cbmc.log`) | Clean TSan and production-linked CBMC evidence is pending. |
| `CUR-004` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`) | Clean retained deadline and fault-injection evidence is pending. |
| `CUR-005` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`transport-ctest.log`) | Clean retained transport evidence is pending. |
| `CUR-006` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`transport-ctest.log`) | Clean 100-cycle resource evidence is pending. |
| `CUR-007` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`, `transport-ctest.log`) | Clean root and transport error-propagation evidence is pending. |
| `CUR-008` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`transport-ctest.log`) | Clean retained lifecycle evidence is pending. |
| `CUR-009` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`transport-ctest.log`, `hardware-diagnostic.log`) | **Hardware unavailable:** no RP2040/W5500 diagnostic USB target or latency-measurement facility was detected. |
| `CUR-010` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`, `transport-ctest.log`) | Clean callback-layout evidence is pending. |
| `CUR-011` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`, `verify-binaries.log`, `hardware-diagnostic.log`) | **Hardware unavailable:** no RP2040/W5500 target or physical-link control facility was detected. |
| `CUR-012` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`) | Clean exhaustive flag-matrix evidence is pending. |
| `CUR-013` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`) | Clean configuration-coherence evidence is pending. |
| `CUR-014` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`transport-ctest.log`, `hardware-diagnostic.log`) | **Hardware unavailable:** no RP2040/W5500 diagnostic USB target was detected. |
| `CUR-015` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`, `transport-ctest.log`) | Clean public-interface evidence is pending. |
| `CUR-016` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`, `root-sanitize.log`, `cross-compile.log`, `hardware-smoke.log`, `hardware-diagnostic.log`) | **Hardware unavailable:** no RP2040/W5500 USB target was detected. Clean retained host and build logs are also pending. |
| `CUR-017` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`cbmc.log`) | A retained clean-candidate production-linked CBMC report is pending. |
| `CUR-018` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`static-analysis.log`, `cross-compile.log`, `verify-binaries.log`, `hardware-smoke.log`, `hardware-diagnostic.log`) | **Hardware unavailable:** no RP2040/W5500 USB target was detected. Audit-claim reconciliation is also pending. |
| `CUR-019` | **BLOCKED** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`evidence-validator.log`) | Replacement `CUR` rows have not passed, so historical supersessions are not complete. |

### Hardware-Unavailable Blockers

Physical execution remains unavailable for `CUR-009`, `CUR-011`, `CUR-014`,
`CUR-016`, and `CUR-018`. These rows require RP2040/W5500 smoke or full
diagnostic evidence and remain `BLOCKED`; host or model results are not
substitutes. The other current rows are also `BLOCKED` for the pending clean
host, sanitizer, TSan, CBMC, static-analysis, cross-build, binary, transport, or
evidence-validation artifacts stated above.

## Historical Findings

Every historical result below is taken from the [canonical evidence
matrix](specs/005-fix-audit-findings/evidence.md). `SUPERSEDED` means the old
finding is now governed by the named current corrective category. It does not
mean resolved: every named replacement is presently `BLOCKED`.

| Historical finding | Reconciled outcome | Replacement status |
|--------------------|--------------------|--------------------|
| `AUD-001` | **SUPERSEDED** by `CUR-015` | `CUR-015` BLOCKED |
| `AUD-002` | **SUPERSEDED** by `CUR-015` | `CUR-015` BLOCKED |
| `AUD-003` | **SUPERSEDED** by `CUR-010` | `CUR-010` BLOCKED |
| `AUD-004` | **SUPERSEDED** by `CUR-007` | `CUR-007` BLOCKED |
| `AUD-005` | **SUPERSEDED** by `CUR-015` | `CUR-015` BLOCKED |
| `AUD-006` | **SUPERSEDED** by `CUR-012` | `CUR-012` BLOCKED |
| `AUD-007` | **SUPERSEDED** by `CUR-004` | `CUR-004` BLOCKED |
| `AUD-008` | **SUPERSEDED** by `CUR-009` | `CUR-009` BLOCKED (hardware unavailable) |
| `AUD-009` | **SUPERSEDED** by `CUR-010` | `CUR-010` BLOCKED |
| `AUD-010` | **SUPERSEDED** by `CUR-005` | `CUR-005` BLOCKED |
| `AUD-011` | **SUPERSEDED** by `CUR-002` | `CUR-002` BLOCKED |
| `AUD-012` | **SUPERSEDED** by `CUR-014` | `CUR-014` BLOCKED (hardware unavailable) |
| `AUD-013` | **SUPERSEDED** by `CUR-013` | `CUR-013` BLOCKED |
| `AUD-014` | **SUPERSEDED** by `CUR-007` | `CUR-007` BLOCKED |
| `AUD-015` | **SUPERSEDED** by `CUR-004` | `CUR-004` BLOCKED |
| `AUD-016` | **SUPERSEDED** by `CUR-004` | `CUR-004` BLOCKED |
| `AUD-017` | **SUPERSEDED** by `CUR-004` | `CUR-004` BLOCKED |
| `AUD-018` | **SUPERSEDED** by `CUR-015` | `CUR-015` BLOCKED |
| `AUD-019` | **SUPERSEDED** by `CUR-015` | `CUR-015` BLOCKED |
| `AUD-020` | **SUPERSEDED** by `CUR-015` | `CUR-015` BLOCKED |
| `AUD-021` | **SUPERSEDED** by `CUR-015` | `CUR-015` BLOCKED |
| `AUD-022` | **SUPERSEDED** by `CUR-013` | `CUR-013` BLOCKED |
| `AUD-023` | **SUPERSEDED** by `CUR-011` | `CUR-011` BLOCKED (hardware unavailable) |
| `AUD-024` | **SUPERSEDED** by `CUR-012` | `CUR-012` BLOCKED |
| `AUD-025` | **SUPERSEDED** by `CUR-003` | `CUR-003` BLOCKED |
| `AUD-026` | **SUPERSEDED** by `CUR-016` | `CUR-016` BLOCKED (hardware unavailable and clean logs pending) |
| `AUD-027` | **SUPERSEDED** by `CUR-010` | `CUR-010` BLOCKED |
| `AUD-028` | **SUPERSEDED** by `CUR-016` | `CUR-016` BLOCKED (hardware unavailable and clean logs pending) |
| `AUD-029` | **SUPERSEDED** by `CUR-015` | `CUR-015` BLOCKED |
| `AUD-030` | **SUPERSEDED** by `CUR-013` | `CUR-013` BLOCKED |
| `AUD-031` | **SUPERSEDED** by `CUR-013` | `CUR-013` BLOCKED |
| `AUD-032` | **SUPERSEDED** by `CUR-009` | `CUR-009` BLOCKED (hardware unavailable) |
| `AUD-033` | **SUPERSEDED** by `CUR-013` | `CUR-013` BLOCKED |
| `AUD-034` | **SUPERSEDED** by `CUR-004` | `CUR-004` BLOCKED |
| `AUD-035` | **SUPERSEDED** by `CUR-014` | `CUR-014` BLOCKED (hardware unavailable) |
| `AUD-036` | **SUPERSEDED** by `CUR-004` | `CUR-004` BLOCKED |
| `AUD-037` | **SUPERSEDED** by `CUR-004` | `CUR-004` BLOCKED |
| `AUD-038` | **SUPERSEDED** by `CUR-004` | `CUR-004` BLOCKED |
| `AUD-039` | **SUPERSEDED** by `CUR-011` | `CUR-011` BLOCKED (hardware unavailable) |
| `AUD-040` | **SUPERSEDED** by `CUR-013` | `CUR-013` BLOCKED |
| `AUD-041` | **SUPERSEDED** by `CUR-013` | `CUR-013` BLOCKED |
| `AUD-042` | **SUPERSEDED** by `CUR-015` | `CUR-015` BLOCKED |
| `AUD-043` | **SUPERSEDED** by `CUR-015` | `CUR-015` BLOCKED |
| `AUD-044` | **SUPERSEDED** by `CUR-004` | `CUR-004` BLOCKED |
| `AUD-045` | **SUPERSEDED** by `CUR-015` | `CUR-015` BLOCKED |
| `AUD-046` | **SUPERSEDED** by `CUR-013` | `CUR-013` BLOCKED |
| `AUD-047` | **SUPERSEDED** by `CUR-007` | `CUR-007` BLOCKED |
| `AUD-048` | **SUPERSEDED** by `CUR-014` | `CUR-014` BLOCKED (hardware unavailable) |
| `AUD-049` | **SUPERSEDED** by `CUR-012` | `CUR-012` BLOCKED |
| `AUD-050` | **SUPERSEDED** by `CUR-012` | `CUR-012` BLOCKED |
| `AUD-051` | **SUPERSEDED** by `CUR-002` | `CUR-002` BLOCKED |
| `AUD-052` | **SUPERSEDED** by `CUR-004` | `CUR-004` BLOCKED |
| `AUD-053` | **SUPERSEDED** by `CUR-004` | `CUR-004` BLOCKED |
| `AUD-054` | **SUPERSEDED** by `CUR-010` | `CUR-010` BLOCKED |
| `AUD-055` | **SUPERSEDED** by `CUR-002` | `CUR-002` BLOCKED |
| `AUD-056` | **SUPERSEDED** by `CUR-007` | `CUR-007` BLOCKED |
| `AUD-057` | **SUPERSEDED** by `CUR-001` | `CUR-001` BLOCKED |
| `AUD-058` | **SUPERSEDED** by `CUR-001` | `CUR-001` BLOCKED |
| `AUD-059` | **SUPERSEDED** by `CUR-003` | `CUR-003` BLOCKED |
| `AUD-060` | **SUPERSEDED** by `CUR-003` | `CUR-003` BLOCKED |
| `AUD-061` | **SUPERSEDED** by `CUR-013` | `CUR-013` BLOCKED |
| `AUD-062` | **SUPERSEDED** by `CUR-004` | `CUR-004` BLOCKED |
| `AUD-063` | **SUPERSEDED** by `CUR-013` | `CUR-013` BLOCKED |
| `AUD-064` | **SUPERSEDED** by `CUR-013` | `CUR-013` BLOCKED |
| `AUD-065` | **SUPERSEDED** by `CUR-002` | `CUR-002` BLOCKED |
| `AUD-066` | **SUPERSEDED** by `CUR-015` | `CUR-015` BLOCKED |
| `AUD-067` | **SUPERSEDED** by `CUR-004` | `CUR-004` BLOCKED |
| `AUD-068` | **SUPERSEDED** by `CUR-002` | `CUR-002` BLOCKED |
| `AUD-069` | **SUPERSEDED** by `CUR-015` | `CUR-015` BLOCKED |
| `AUD-070` | **SUPERSEDED** by `CUR-003` | `CUR-003` BLOCKED |
| `AUD-071` | **SUPERSEDED** by `CUR-013` | `CUR-013` BLOCKED |
| `AUD-072` | **SUPERSEDED** by `CUR-015` | `CUR-015` BLOCKED |
| `AUD-073` | **SUPERSEDED** by `CUR-015` | `CUR-015` BLOCKED |

## Release Statement

The implementation may be present in the candidate worktree, but the evidence
record does not support a resolution or release-readiness claim. The release
remains blocked until all mandatory current rows have accepted clean-candidate
artifacts, including physical smoke and full-diagnostic results for the
hardware-dependent categories.
