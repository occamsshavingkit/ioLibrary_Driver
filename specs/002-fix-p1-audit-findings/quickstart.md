# Quickstart: Validating P1 Audit Fixes — W5500 ioLibrary_Driver

## Prerequisites

- C compiler with `-fsanitize=address,undefined` (GCC or Clang)
- arm-none-eabi-gcc for cross-compile verification
- Git
- A build configured with `_WIZCHIP_ == 5500` and SPI IO mode

## Per-Fix Verification

### AUD-006: Protocol rejection

```bash
# Exhaust protocol/socket combinations. Unsupported combos must return immediately.
# Compile with _WIZCHIP_=5500 and verify:
#   socket(1, Sn_MR_MACRAW, 0, 0) → SOCKERR_SOCKMODE
#   socket(0, Sn_MR_UDP6, 0, 0)    → SOCKERR_SOCKMODE
```

### AUD-007/036: Bounded polling deadlines

```bash
# Mock stuck SPI (MISO held high). All polling APIs must return within
# _WIZCHIP_POLL_MAX_ iterations. Verify recv() does not deadlock in
# SOCK_CLOSE_WAIT with zero RX data.
```

### AUD-008: Interrupt masking documentation

```bash
# Verify the documentation comment is present in wizchip_conf.h near
# WIZCHIP_CRITICAL_ENTER/EXIT macros warning about DMA+IRQ masking.
```

### AUD-009: SPI callback documentation

```bash
# Verify wizchip_conf.h documents that SPI callbacks must be synchronous.
```

### AUD-010: VERSIONR probe

```bash
# Disconnect MISO. wizchip_init() must return failure within bounded retries.
# Mock VERSIONR != 0x04. wizchip_init() must fail with version mismatch.
```

### AUD-013: Buffer layout validation

```bash
# Call wizchip_init with unsupported sizes (3, 5, 7). Must fail before SPI write.
# Call with over-limit totals (>16 KiB). Must fail.
```

### AUD-014: IPRAW partial receive

```bash
# Queue 100-byte IPRAW packet. Call recvfrom with 50-byte buffer twice.
# Must return 50, 50, then remaining size zero.
```

### AUD-015: Nonblocking sendto()

```bash
# UDP socket with SF_IO_NONBLOCK, SENDOK held low.
# sendto() must return after issuing SEND command, before SENDOK.
```

### AUD-016: API return semantics

```bash
# Verify socket.h has comments distinguishing command-accepted from
# command-completed return values.
```

### AUD-017: Application send loop SOCK_BUSY

```bash
# Mock send() returning 0 (SOCK_BUSY). Application loop must not spin.
# It must yield and retry on next invocation with correct offset.
```

### AUD-018: UDP peer metadata persistence

```bash
# Receive multi-chunk datagram. Clobber stack between calls.
# Response must use original peer address from first chunk.
```

### AUD-039: PHY reset settle

```bash
# Logic-analyzer SPI during wizphy_reset().
# No PHYCFGR read before ~200 µs settle time.
```

### AUD-040/041: Atomic 16-bit access

```bash
# Static: expand each 16-bit setter/getter macro. Verify single CRITICAL section.
# Dynamic: ISR stress test with Sn_CR_RECV while setSn_RX_RD called.
# No torn values observed at the chip register.
```

### AUD-037: SENDOK race resolution

```bash
# Review socket.c:582-596 (second sock_is_sending block).
# If unreachable (per fourth-pass analysis): remove and add comment.
# If reachable: implement Sn_IR re-read and reconciliation.
```

### AUD-008/009/011/012: Architectural fixes

```bash
# Verify documentation comments are present for each finding.
# Verify small critical-section guards are added where easy.
# Verify Sn_IR snapshot pattern used in polling loops.
```

## System-Level Validation

```bash
# For each fix branch:
git checkout fix/aud-NNN-description
# Compile under all 7 _WIZCHIP_ values:
arm-none-eabi-gcc -fsyntax-only -D_WIZCHIP_=XXXX -I Ethernet ...
# Verify single commit:
git log --oneline fork/master..HEAD | wc -l  # must be 1

# ASan/UBSan for logic defects:
gcc -fsanitize=address,undefined -O0 -O2 test_harness.c
./a.out  # must exit 0, no sanitizer abort
```
