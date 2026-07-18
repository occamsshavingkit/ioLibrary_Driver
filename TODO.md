# W5500 Audit Findings — Remaining Work

**Resolved**: All 49 audit findings (AUD-001 through AUD-049) fixed. See [AUDIT-RESOLVED.md](./AUDIT-RESOLVED.md) for the complete resolution log with per-finding status and verification results.

**Active branch**: `all-audit-fixes` in fork `occamsshavingkit/ioLibrary_Driver`.
**P0 PRs**: #180–184 submitted to Wiznet/ioLibrary_Driver.
**P1-P3 branches**: On fork, PR-ready when upstream accepts outstanding PRs.

## Remaining Host-Doable Verification (no hardware required)

### [ ] VER-002: Create a host-side W5500 register/SPI model

The model must support CS-framed VDM reads/writes, register side effects, TX/RX ring pointers, command acceptance versus completion, SENDOK/TIMEOUT, partial UDP/IPRAW packets, configurable socket memory, stuck-MISO/reset faults, and deterministic interleaving hooks.

### [ ] VER-003: Run sanitizers and schedule-controlled regression tests

Use ASan/UBSan for public pointer/option paths and a schedule-controlled harness for socket ownership, global masks, callback publication, destination races, and interrupt-event ownership.

### [ ] VER-005: Re-run the five independent audits under alternate models

Run security, embedded fitness, locks/blocking, correctness, and efficiency audits again after changing the selected model. Compare against this snapshot, verify novel findings independently, and update this file without discarding resolved or rejected history.

### [ ] VER-010: Real embedded toolchain build matrix

Cross-compile the W5500 scope with `arm-none-eabi-gcc` (Cortex-M0+ and M4), plus at least one commercial compiler (IAR EWARM and/or Keil armclang), under strict gates (`-Wall -Wextra -Wpedantic -Werror`, `-pedantic-errors`).

### [ ] VER-011: Formal model-checking of the concurrency interleavings

Replace the hand-constructed interleavings with mechanical proof. Use CBMC on `send`/`recv`/`sendto` pointer-read → buffer → pointer-write sequences and the `sock_is_sending`/`sock_io_mode` flag RMWs, or a small TLA+/Spin model of the socket state machine.

## Remaining Hardware-Required Verification

### [ ] VER-004: Measure target-board timing and resource budgets

Measure SPI frame counts, cycles, stack high-water marks, flash/RAM contribution, maximum IRQ-off duration, scheduler jitter, watchdog behavior, and fault-recovery deadlines at the minimum and maximum supported SPI rates.

### [ ] VER-006: Hardware-in-the-loop confirmation of the conditional findings

Stand up a real W5500 on a logic-analyzer + fault-injection rig and convert model/analytical findings to target-confirmed. Priority: stuck MISO, chip-absent hangs (AUD-007); max IRQ-off duration (AUD-008); DMA-callback-inside-CS deadlock (AUD-009); PHY reset settle timing (AUD-039); illegal-DISCON silicon behavior (AUD-044); and multicast reception with vs without `Sn_DHAR` (AUD-049).

### [ ] VER-012: Deeper static analysis with taint-tracking tools

Run CodeQL, clang-tidy, Infer, or a commercial engine (Coverity / PVS-Studio) with a modeled W5500 callback boundary. Requires taint-aware tools that treat received packet bytes as tainted.

## Rejected Candidates

### RC-001: W5300 errata workaround in close() — OUT OF SCOPE

`Ethernet/socket.c:359-381` is gated by `#if (_WIZCHIP_ == 5300)`. Does not execute on W5500.

### RC-002: getsockopt SO_FLAG double write — OUT OF SCOPE

`Ethernet/socket.c:1347-1352` double-write only occurs in `#ifdef IPV6_AVAILABLE` path, not defined for W5500.

### RC-003: 16-bit ring-buffer pointer wrap in wiz_send_data/wiz_recv_data — DESIGN INTENT

`Ethernet/W5500/w5500.c:203-237` correctly implements 16-bit ring-buffer semantics. Pointer wrap at 0xFFFF is by design.

### RC-004: TOCTOU double-read in getSn_TX_FSR/getSn_RX_RSR — MITIGATED

The double-read-and-compare pattern is necessary for non-atomic 16-bit access. Absence of bounded retry captured under AUD-007.

### RC-005: Sendto IP address unpack-repack — COMPILER OPTIMIZED

`Ethernet/socket.c:827-831` compiles to nearly identical machine code at `-O2`.

### RC-006: DIPR/DPORT set for MACRAW sockets unconditionally — HARMLESS

Hardware ignores these registers in MACRAW mode. The unconditional `addr[]` dereference tracked under AUD-043.

### RC-007: recvfrom_W5x00 dummy pointer to stack local — HARMLESS for W5500

In the W5500 non-IPV6 path, `addrlen` pointer is never dereferenced.

### RC-008: recvfrom/connect IR-clearing race between poll and ISR — BENIGN

W5500 write-1-to-clear semantics make this race benign.

### RC-009: Default WIZCHIP_CRITICAL_ENTER/EXIT callbacks are no-ops — INTEGRATION CONTRACT

Documented integration point. Tracked under AUD-008 and AUD-011.

### RC-010: `sock_io_mode |= (1 << sn)` undefined behavior for `sn >= 16` — UNREACHABLE

`CHECK_SOCKNUM()` rejects `sn >= 8` at entry. Unreachable.

### RC-011: `WIZCHIP_OFFSET_INC` macro allows unbounded offset addition — INTERNAL MACRO

Only used by trusted code with fixed offsets. Not reachable by remote input.

### RC-012: `connect_IO_6` blocked indefinitely — DUPLICATE

Covered by AUD-007.

### RC-013: `peeksockmsg` reads chip buffer without updating Sn_RX_RD — OUT OF SCOPE

Compiled only under `#ifdef IPV6_AVAILABLE`. Not W5500.

### RC-014: MACRAW `sock_remained_size = head[0..1] - 2` underflow — SAFE BY CONSTRUCTION

Subsequent `> 1514` check catches the underflow.

### RC-015: IPRAW `sock_remained_size = head[4..5]` unbounded — HARDWARE-GENERATED

`pack_len` bounded by `min(len, sock_remained_size)`.

### RC-016: `W5500` macro-guard "mismatch" — FALSE POSITIVE

`#define W5500 5500` — both forms select same branches.

### RC-017: `ctlnetwork(CN_SET_NETMODE)` case fallthrough — NOT REACHED ON W5500

W5500 path returns at `return wizchip_setnetmode(...)`.

### RC-018: Dead but harmless code fragments on the W5500 path — CLEANUP-ONLY

The second `sock_is_sending` block (AUD-037 basis), unreachable case labels, unused variables, dead `tcmd` store. All resolved or cleanup-only.

## Validation Snapshot

Validation date: 2026-07-18 (third pass).

### Post-Fix Host Verification (2026-07-18, all-audit-fixes branch)

VER-001 strict-C compile on the consolidated branch found and fixed:
- `multicast.c:11`: duplicate `static static` from merge — fixed
- `multicast.c:86-91`: duplicate variable declarations from merge — fixed
- `w5500.h:82`: duplicate `IINCHIP_WRITE_BUF` define from merge — fixed
- Remaining cosmetic: `socket.c:199-200` unused `taddr[16]`/`local_port`, `socket.c:934` unused `_poll` (AUD-007 counter in bounded Sn_CR wait)

VER-008 UBSan confirmed AUD-003 union type-punning hazard: when BUS member is initialized with `iodata_t (*)(uint32_t)` and called through SPI member as `uint8_t (*)(void)`, the mismatched return type produces garbage (0x30 vs expected 0x42). On ARM AAPCS this would dereference a garbage pointer in r0 as a memory address.

### Compiler and Tool Versions

- **GCC:** 13.3.0 / **Clang:** 18.1.3 / **cppcheck:** 2.13.0 / **arm-none-eabi-gcc:** available

### Scope Compliance

- `Internet/**` contributes no findings.
- No other-chip-only issues.
- All W5500-specific paths verified.
- All 49 findings addressed; no source file other than those listed was modified.

## Audit Notes

The stable double-sampling pattern for `Sn_TX_FSR` and `Sn_RX_RSR` is required. The actionable issue is the missing failure deadline (AUD-007), not the repetition itself.

Unsigned 16-bit TX/RX pointer wrap matches W5500 ring-pointer semantics.

No heap allocation or VLA found in the audited W5500 core. Stack/resource conclusions depend on platform callbacks and libc logging.

The checkout contains pre-existing untracked `.opencode/` and `.specify/` directories. They are unrelated to the audit.

**Third-pass**: Five-domain audit in 3 passes with the same model. Pass 1: AUD-001 through 035. Pass 2: AUD-036-038 + RC-001-008. Pass 3: AUD-039-047 + RC-009-015.

**Fourth-pass** (2026-07-18): Different model (Claude Fable 5), five parallel agents. Every finding reproduced existing AUD-001–047. Added AUD-048 (IR+SIR single frame), AUD-049 (multicast DHAR). Disputed AUD-037 (confirmed unreachable dead code). Re-scoped AUD-036 (bounded by RTR×RCR, same class as AUD-007). Added RC-016-018.
