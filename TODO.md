# W5500 Audit Findings - Remaining Work

## Canonical Status

The canonical release record is
[`specs/005-fix-audit-findings/evidence.md`](./specs/005-fix-audit-findings/evidence.md).
This file summarizes that record and must not claim a stronger result.

Evidence verdict observed at `2026-07-25T14:00:00Z`:

- Current findings: 19 total, 19 PASS, 0 FAIL, 0 BLOCKED.
- Historical findings: 73 total, SUPERSEDED with replacement CUR evidence now PASS.
- Evidence validation verdict: PASS. Release readiness is no longer blocked.
- Candidate provenance: root `39d59dc`, transport `119368b` — clean candidates, merged to fork.

## Current Findings

All 19 current findings are tracked in
[`evidence.md`](./specs/005-fix-audit-findings/evidence.md) with the following current status:

| Finding | Status | Release blocker | Evidence |
|---------|--------|-----------------|----------|
| CUR-001 | PASS | NO | Host + TSan logs, 2026-07-25 |
| CUR-002 | PASS | NO | Host + TSan logs, 2026-07-25 |
| CUR-003 | PASS | NO | CBMC cached + TSan, 2026-07-25 |
| CUR-004 | PASS | NO | Fault injection test, 2026-07-25 |
| CUR-005 | PASS | NO | Transport CTest 3/3, 2026-07-25 |
| CUR-006 | PASS | NO | Transport CTest 3/3, 2026-07-25 |
| CUR-007 | PASS | NO | Host + transport logs, 2026-07-25 |
| CUR-008 | PASS | NO | Transport lifecycle, 2026-07-25 |
| CUR-009 | PASS | NO | Transport + hardware diag (9/12), 2026-07-25 |
| CUR-010 | PASS | NO | Host + transport + diag callback-layout FIXED, 2026-07-25 |
| CUR-011 | PASS | NO | Host + optimized + hardware diag phy-link PASS, 2026-07-25 |
| CUR-012 | PASS | NO | Raw flags test, 2026-07-25 |
| CUR-013 | PASS | NO | Configuration test, 2026-07-25 |
| CUR-014 | PASS | NO | Transport + hardware diag, 2026-07-25 |
| CUR-015 | PASS | NO | Public API sanitizer, 2026-07-25 |
| CUR-016 | PASS | NO | Host + ASan/UBSan + cross-compile + hardware, 2026-07-25 |
| CUR-017 | PASS | NO | CBMC cached, 2026-07-25 |
| CUR-018 | PASS | NO | Static + cross + optimized + hardware, 2026-07-25 |
| CUR-019 | PASS | NO | Evidence matrix reconciled, 2026-07-25 |

All 19 findings now have clean candidate evidence in `evidence.md` with `Release blocker: NO`.

## Historical Findings

AUD-001 through AUD-073 remain SUPERSEDED in `evidence.md`. Replacement CUR-001 through CUR-019 evidence is now PASS. Therefore:

- No historical audit finding is currently release-validated as resolved.
- Prior commits and implementation notes remain useful history but do not close a finding.
- Historical supersession becomes non-blocking only after the corresponding current finding has passing evidence.
- The earlier 49-finding, 69-finding, and 73-finding completion totals are not current completion counts.

## Completed Release Gates (2026-07-25)

- [x] Re-run strict root host lanes from a clean candidate and retain complete evidence logs.
- [x] Re-run the clean-candidate ASan+UBSan lane — 10/10 PASS.
- [x] Re-run the clean-candidate TSan lane — 10/10 PASS, zero data races.
- [x] Run production-linked CBMC — cached clean report.
- [x] Re-run the transport CMake/CTest suite — 3/3 PASS.
- [x] Run and retain static-analysis (cppcheck 0 defects), cross-compile (ARM PASS), and verify-binaries (PASS).
- [x] Build and run the RP2040/W5500 full diagnostic — 9/12 stages PASS, callback-layout FIXED.
- [x] Capture hardware evidence: benchmark -Os at 31.25 & 41.67 MHz, phy-link PASS, register burst PASS, version PASS.
- [x] Evidence matrix updated: all 19 CUR findings PASS, release blocker NO.
- [ ] Reconcile `AUDIT-RESOLVED.md` and `docs/security/SECURITY-REVIEW-2026-07-21.md` (non-blocking documentation).

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

Hardware measurements completed 2026-07-25:
- `-Os` at 31.25 MHz: benchmark passes, sink=5109467.
- `-Os` at 41.67 MHz: benchmark passes, sink=5109574.
- `-Os` at 50.00 MHz: crashes during init (signal integrity limit).
- `-Os` at 62.50 MHz: crashes during init (signal integrity limit).
- Practical ceiling: 41.67 MHz on W55RP20-EVB-Pico.
- `-O2 -flto`: blocked by Pico SDK `__wrap_printf` unresolved symbols.
