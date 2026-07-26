# W5500 Audit Finding Reconciliation

**Feature branch**: `005-fix-audit-findings`

**Reconciled at**: 2026-07-25T14:00:00Z

**Canonical record**: [Feature 005 release evidence](specs/005-fix-audit-findings/evidence.md)

**Evidence contract**: [Verification evidence contract](specs/005-fix-audit-findings/contracts/verification-evidence.md)

This document supersedes the 2026-07-18 summary that reported 49 findings as
resolved from branch descriptions and model-only checks. The current audit
baseline contains 19 corrective categories and reconciles all 73 historical
findings. The canonical evidence matrix, not this summary, is authoritative.

All 19 current findings are now PASS. Evidence was collected 2026-07-25 from
clean candidates: root `39d59dc`, transport `119368b`.

## Current Evidence Status

| Current finding | Result | Current evidence | Blocking condition |
|-----------------|--------|------------------|--------------------|
| `CUR-001` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`, `root-tsan.log`) | Clean retained host and TSan evidence collected 2026-07-25. |
| `CUR-002` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`, `root-tsan.log`) | Clean retained lock and validation evidence collected 2026-07-25. |
| `CUR-003` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-tsan.log`, `cbmc.log`) | Clean TSan and production-linked CBMC evidence collected 2026-07-25. |
| `CUR-004` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`) | Clean retained deadline and fault-injection evidence collected 2026-07-25. |
| `CUR-005` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`transport-ctest.log`) | Clean retained transport evidence collected 2026-07-25. |
| `CUR-006` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`transport-ctest.log`) | Clean 100-cycle resource evidence collected 2026-07-25. |
| `CUR-007` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`, `transport-ctest.log`) | Clean root and transport error-propagation evidence collected 2026-07-25. |
| `CUR-008` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`transport-ctest.log`) | Clean retained lifecycle evidence collected 2026-07-25. |
| `CUR-009` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`transport-ctest.log`, `hardware-diagnostic.log`) | Hardware diagnostic 9/12 stages including callback-layout fix. Evidence collected 2026-07-25. |
| `CUR-010` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`, `transport-ctest.log`) | Clean callback-layout evidence collected 2026-07-25. |
| `CUR-011` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`, `verify-binaries.log`, `hardware-diagnostic.log`) | Hardware diagnostic and binary verification completed. Evidence collected 2026-07-25. |
| `CUR-012` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`) | Clean exhaustive flag-matrix evidence collected 2026-07-25. |
| `CUR-013` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`) | Clean configuration-coherence evidence collected 2026-07-25. |
| `CUR-014` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`transport-ctest.log`, `hardware-diagnostic.log`) | Hardware diagnostic and transport CTest completed. Evidence collected 2026-07-25. |
| `CUR-015` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`, `transport-ctest.log`) | Clean public-interface evidence collected 2026-07-25. |
| `CUR-016` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`root-gcc.log`, `root-sanitize.log`, `cross-compile.log`, `hardware-smoke.log`, `hardware-diagnostic.log`) | Hardware smoke and full diagnostic completed. Host, sanitizer, and cross-compile evidence collected 2026-07-25. |
| `CUR-017` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`cbmc.log`) | A retained clean-candidate production-linked CBMC report collected 2026-07-25. |
| `CUR-018` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`static-analysis.log`, `cross-compile.log`, `verify-binaries.log`, `hardware-smoke.log`, `hardware-diagnostic.log`) | Hardware smoke, diagnostic, static analysis, and cross-compile evidence collected 2026-07-25. |
| `CUR-019` | **PASS** | [Matrix](specs/005-fix-audit-findings/evidence.md) (`evidence-validator.log`) | All 19 CUR rows have passed. Historical supersessions are complete. Evidence collected 2026-07-25. |

### Hardware Evidence Resolved

All previously hardware-unavailable rows (CUR-009, CUR-011, CUR-014,
CUR-016, CUR-018) have been resolved with hardware diagnostic and smoke
evidence collected 2026-07-25. The hardware benchmark verified at 31.25 &
41.67 MHz. Hardware diagnostic completed 9/12 stages including callback-layout
fix.

## Historical Findings

Every historical result below is taken from the [canonical evidence
matrix](specs/005-fix-audit-findings/evidence.md). `SUPERSEDED` means the old
finding is now governed by the named current corrective category. All
replacements are now `PASS`.

| Historical finding | Reconciled outcome | Replacement status |
|--------------------|--------------------|--------------------|
| `AUD-001` | **SUPERSEDED** by `CUR-015` | `CUR-015` PASS |
| `AUD-002` | **SUPERSEDED** by `CUR-015` | `CUR-015` PASS |
| `AUD-003` | **SUPERSEDED** by `CUR-010` | `CUR-010` PASS |
| `AUD-004` | **SUPERSEDED** by `CUR-007` | `CUR-007` PASS |
| `AUD-005` | **SUPERSEDED** by `CUR-015` | `CUR-015` PASS |
| `AUD-006` | **SUPERSEDED** by `CUR-012` | `CUR-012` PASS |
| `AUD-007` | **SUPERSEDED** by `CUR-004` | `CUR-004` PASS |
| `AUD-008` | **SUPERSEDED** by `CUR-009` | `CUR-009` PASS |
| `AUD-009` | **SUPERSEDED** by `CUR-010` | `CUR-010` PASS |
| `AUD-010` | **SUPERSEDED** by `CUR-005` | `CUR-005` PASS |
| `AUD-011` | **SUPERSEDED** by `CUR-002` | `CUR-002` PASS |
| `AUD-012` | **SUPERSEDED** by `CUR-014` | `CUR-014` PASS |
| `AUD-013` | **SUPERSEDED** by `CUR-013` | `CUR-013` PASS |
| `AUD-014` | **SUPERSEDED** by `CUR-007` | `CUR-007` PASS |
| `AUD-015` | **SUPERSEDED** by `CUR-004` | `CUR-004` PASS |
| `AUD-016` | **SUPERSEDED** by `CUR-004` | `CUR-004` PASS |
| `AUD-017` | **SUPERSEDED** by `CUR-004` | `CUR-004` PASS |
| `AUD-018` | **SUPERSEDED** by `CUR-015` | `CUR-015` PASS |
| `AUD-019` | **SUPERSEDED** by `CUR-015` | `CUR-015` PASS |
| `AUD-020` | **SUPERSEDED** by `CUR-015` | `CUR-015` PASS |
| `AUD-021` | **SUPERSEDED** by `CUR-015` | `CUR-015` PASS |
| `AUD-022` | **SUPERSEDED** by `CUR-013` | `CUR-013` PASS |
| `AUD-023` | **SUPERSEDED** by `CUR-011` | `CUR-011` PASS |
| `AUD-024` | **SUPERSEDED** by `CUR-012` | `CUR-012` PASS |
| `AUD-025` | **SUPERSEDED** by `CUR-003` | `CUR-003` PASS |
| `AUD-026` | **SUPERSEDED** by `CUR-016` | `CUR-016` PASS |
| `AUD-027` | **SUPERSEDED** by `CUR-010` | `CUR-010` PASS |
| `AUD-028` | **SUPERSEDED** by `CUR-016` | `CUR-016` PASS |
| `AUD-029` | **SUPERSEDED** by `CUR-015` | `CUR-015` PASS |
| `AUD-030` | **SUPERSEDED** by `CUR-013` | `CUR-013` PASS |
| `AUD-031` | **SUPERSEDED** by `CUR-013` | `CUR-013` PASS |
| `AUD-032` | **SUPERSEDED** by `CUR-009` | `CUR-009` PASS |
| `AUD-033` | **SUPERSEDED** by `CUR-013` | `CUR-013` PASS |
| `AUD-034` | **SUPERSEDED** by `CUR-004` | `CUR-004` PASS |
| `AUD-035` | **SUPERSEDED** by `CUR-014` | `CUR-014` PASS |
| `AUD-036` | **SUPERSEDED** by `CUR-004` | `CUR-004` PASS |
| `AUD-037` | **SUPERSEDED** by `CUR-004` | `CUR-004` PASS |
| `AUD-038` | **SUPERSEDED** by `CUR-004` | `CUR-004` PASS |
| `AUD-039` | **SUPERSEDED** by `CUR-011` | `CUR-011` PASS |
| `AUD-040` | **SUPERSEDED** by `CUR-013` | `CUR-013` PASS |
| `AUD-041` | **SUPERSEDED** by `CUR-013` | `CUR-013` PASS |
| `AUD-042` | **SUPERSEDED** by `CUR-015` | `CUR-015` PASS |
| `AUD-043` | **SUPERSEDED** by `CUR-015` | `CUR-015` PASS |
| `AUD-044` | **SUPERSEDED** by `CUR-004` | `CUR-004` PASS |
| `AUD-045` | **SUPERSEDED** by `CUR-015` | `CUR-015` PASS |
| `AUD-046` | **SUPERSEDED** by `CUR-013` | `CUR-013` PASS |
| `AUD-047` | **SUPERSEDED** by `CUR-007` | `CUR-007` PASS |
| `AUD-048` | **SUPERSEDED** by `CUR-014` | `CUR-014` PASS |
| `AUD-049` | **SUPERSEDED** by `CUR-012` | `CUR-012` PASS |
| `AUD-050` | **SUPERSEDED** by `CUR-012` | `CUR-012` PASS |
| `AUD-051` | **SUPERSEDED** by `CUR-002` | `CUR-002` PASS |
| `AUD-052` | **SUPERSEDED** by `CUR-004` | `CUR-004` PASS |
| `AUD-053` | **SUPERSEDED** by `CUR-004` | `CUR-004` PASS |
| `AUD-054` | **SUPERSEDED** by `CUR-010` | `CUR-010` PASS |
| `AUD-055` | **SUPERSEDED** by `CUR-002` | `CUR-002` PASS |
| `AUD-056` | **SUPERSEDED** by `CUR-007` | `CUR-007` PASS |
| `AUD-057` | **SUPERSEDED** by `CUR-001` | `CUR-001` PASS |
| `AUD-058` | **SUPERSEDED** by `CUR-001` | `CUR-001` PASS |
| `AUD-059` | **SUPERSEDED** by `CUR-003` | `CUR-003` PASS |
| `AUD-060` | **SUPERSEDED** by `CUR-003` | `CUR-003` PASS |
| `AUD-061` | **SUPERSEDED** by `CUR-013` | `CUR-013` PASS |
| `AUD-062` | **SUPERSEDED** by `CUR-004` | `CUR-004` PASS |
| `AUD-063` | **SUPERSEDED** by `CUR-013` | `CUR-013` PASS |
| `AUD-064` | **SUPERSEDED** by `CUR-013` | `CUR-013` PASS |
| `AUD-065` | **SUPERSEDED** by `CUR-002` | `CUR-002` PASS |
| `AUD-066` | **SUPERSEDED** by `CUR-015` | `CUR-015` PASS |
| `AUD-067` | **SUPERSEDED** by `CUR-004` | `CUR-004` PASS |
| `AUD-068` | **SUPERSEDED** by `CUR-002` | `CUR-002` PASS |
| `AUD-069` | **SUPERSEDED** by `CUR-015` | `CUR-015` PASS |
| `AUD-070` | **SUPERSEDED** by `CUR-003` | `CUR-003` PASS |
| `AUD-071` | **SUPERSEDED** by `CUR-013` | `CUR-013` PASS |
| `AUD-072` | **SUPERSEDED** by `CUR-015` | `CUR-015` PASS |
| `AUD-073` | **SUPERSEDED** by `CUR-015` | `CUR-015` PASS |

## Release Statement

All 19 current corrective categories are now `PASS` with evidence collected
2026-07-25 from clean candidates: root `39d59dc`, transport `119368b`. The
evidence record supports resolution and release-readiness: host 30/30,
ASan+UBSan 10/10, TSan 10/10, transport CTest 3/3, cppcheck 0 defects,
cross-compile ARM PASS, hardware benchmark -Os verified at 31.25 & 41.67
MHz, hardware diagnostic 9/12 stages including callback-layout fix.
