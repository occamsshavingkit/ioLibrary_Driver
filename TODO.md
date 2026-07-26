# W5500 Audit Findings - Remaining Work

## Canonical Status

The canonical release record is
[`specs/005-fix-audit-findings/evidence.md`](./specs/005-fix-audit-findings/evidence.md).
This file summarizes that record and must not claim a stronger result.

Evidence verdict observed at `2026-07-24T00:44:09Z`:

- Current findings: 19 total, 0 PASS, 0 FAIL, 19 BLOCKED.
- Historical findings: 73 total, 0 currently validated as PASS, 73 labeled SUPERSEDED in the matrix but rejected by the validator because replacement evidence is pending.
- Evidence validation verdict: FAIL. Release readiness remains BLOCKED because every current finding is marked as a release blocker.
- Candidate provenance: the root and transport revisions recorded in the evidence matrix are dirty, so clean-candidate evidence has not been established.

Prior numeric status summaries and broad assurance statements are withdrawn. They described implementation work or older observations, not the validated release verdict.

## Current Findings

All 19 current findings are tracked in
[`evidence.md`](./specs/005-fix-audit-findings/evidence.md) with the following current status:

| Finding | Status | Release blocker | Pending evidence |
|---------|--------|-----------------|------------------|
| CUR-001 | BLOCKED | YES | Clean root host and TSan logs |
| CUR-002 | BLOCKED | YES | Clean root host and TSan logs |
| CUR-003 | BLOCKED | YES | Production-linked CBMC and clean TSan evidence |
| CUR-004 | BLOCKED | YES | Clean root fault-injection evidence |
| CUR-005 | BLOCKED | YES | Clean transport test log |
| CUR-006 | BLOCKED | YES | Clean 100-cycle transport resource evidence |
| CUR-007 | BLOCKED | YES | Clean root and transport logs |
| CUR-008 | BLOCKED | YES | Clean transport lifecycle evidence |
| CUR-009 | BLOCKED | YES | Clean transport log and RP2040/W5500 latency hardware evidence |
| CUR-010 | BLOCKED | YES | Clean root and transport callback-layout evidence |
| CUR-011 | BLOCKED | YES | Clean root/optimized evidence and physical-link hardware evidence |
| CUR-012 | BLOCKED | YES | Clean exhaustive flag-matrix evidence |
| CUR-013 | BLOCKED | YES | Clean configuration-coherence evidence |
| CUR-014 | BLOCKED | YES | Clean transport log and GPIO hardware evidence |
| CUR-015 | BLOCKED | YES | Clean root and transport public-interface evidence |
| CUR-016 | BLOCKED | YES | Clean retained host/build logs and RP2040/W5500 hardware evidence |
| CUR-017 | BLOCKED | YES | Retained clean-candidate production-linked CBMC report |
| CUR-018 | BLOCKED | YES | Audit-claim reconciliation and mandatory host/build/hardware gates |
| CUR-019 | BLOCKED | YES | Passing replacement CUR evidence for historical supersessions |

Do not close any row in this table until `evidence.md` records a fresh PASS with the required command, artifact, clean source provenance, and `Release blocker: NO`.

## Historical Findings

`AUD-001` through `AUD-073` are all labeled `SUPERSEDED`, not PASS, in `evidence.md`. The validator rejects every supersession because each replacement `CUR-*` finding is currently BLOCKED. Therefore:

- No historical audit finding is currently release-validated as resolved.
- Prior commits and implementation notes remain useful history but do not close a finding.
- Historical supersession becomes non-blocking only after the corresponding current finding has passing evidence.
- The earlier 49-finding, 69-finding, and 73-finding completion totals are not current completion counts.

## Pending Release Gates

- [ ] Re-run strict root host lanes from a clean candidate and retain complete evidence logs.
- [ ] Re-run the clean-candidate ASan+UBSan lane and retain the instrumented runtime evidence.
- [ ] Re-run the clean-candidate TSan lane and retain the race-analysis evidence.
- [ ] Run production-linked CBMC and retain a clean-candidate report.
- [ ] Re-run the transport CMake/CTest suite from clean root and transport candidates and retain its log, including lifecycle and 100-cycle resource coverage.
- [ ] Run and retain static-analysis, cross-compile, and optimized-binary verification evidence from the clean candidate.
- [ ] Build and run the RP2040/W5500 smoke and full diagnostic gates on the required hardware.
- [ ] Capture hardware evidence for USB-SOF latency during a maximum transfer, physical-link behavior, GPIO dispatch, lifecycle/fault handling, and resource exhaustion.
- [ ] Re-run `python3 tests/check_audit_evidence.py specs/005-fix-audit-findings/evidence.md` after the required artifacts exist and correct every rejected status or artifact reference.
- [ ] Reconcile `AUDIT-RESOLVED.md` and `docs/security/SECURITY-REVIEW-2026-07-21.md` to the same validated verdict.

## Prior Observations

The 2026-07-20 RP2040/W5500 probe transcript recorded a VERSIONR value of `0x04`, DHCP lease acquisition, a UDP receive-pointer delta of `0x0009`, and three BOOTSEL/USB flashing cycles. The current evidence matrix does not accept that transcript as clean-candidate release evidence. It must not be used to close `CUR-009`, `CUR-011`, `CUR-014`, `CUR-016`, or `CUR-018`.

Prior host model, sanitizer-named binary, cross-compile, static-analysis, and handwritten CBMC-model claims are likewise not substitutes for the production-linked, clean-candidate artifacts required by the current evidence contract.

## Rejected Candidates

The earlier audit classified `RC-001` through `RC-018` as out of scope, duplicate, benign, unreachable, design intent, or otherwise non-actionable. Those classifications are historical context only. They do not alter the 19 current BLOCKED findings or satisfy any pending release gate.

## SPI Hot-Path Optimization

Implemented on this branch and measured with the rewritten host benchmark
(`make -C tests bench`; SPI frame and byte counts are deterministic, host
wall clock covers driver and callback overhead only):

| Operation (100 iterations) | SPI frames before → after | SPI bytes before → after |
|---|---|---|
| 16-bit register read | 200 → 100 | 800 → 500 |
| stable TX FSR / RX RSR read | 400 → 200 | 1600 → 1000 |
| full `wizchip_init` | 4400 → 2900 | 20400 → 14400 |
| socket open/close cycle | 1400 → 1300 | 6000 → 5600 |

Status of the original priorities:

- Priorities 1 and 4 (batched status checks via transport-health cache):
  done. Reads skip busy/error callbacks after one clean check; any write
  invalidates the cache. The flag is accessed only inside the CRIS critical
  section; the TSan lane passes with the caching path active.
- Priority 2 (init verification): done. `wizchip_init` verifies with one
  VERSIONR read instead of a 16-register read-back.
- Priority 3 (`WIZCHIP_SAFE_SPI`): done. Per-transaction checks compile in
  when defined; `test_public_api_sanitizer` builds with it in every lane via
  a target-specific `override` in `tests/Makefile`.
- Priority 5 (cached socket mode): done. `sock_mode_cached_or_read()` and
  direct `sock_mode[sn]` use in `getsockopt`/`recvfrom` paths.
- Priority 6 (16-bit VDM reads): done. `wizchip_read16_5500` reads both
  bytes in one VDM frame.

Transport (WIZnet-PICO-C): `dma_channel_abort_bounded` now short-circuits
when the channel is already idle while keeping the full bounded abort for
busy channels and the stale-IRQ acknowledge; verified by the transport
CTest suite on host.

Remaining: on-hardware wall-clock measurement on RP2040/W55RP20 across
`-Os`, `-O2`, and `-O2 -flto`. This depends on the blocked hardware
evidence gates above and must not be claimed from host numbers.
