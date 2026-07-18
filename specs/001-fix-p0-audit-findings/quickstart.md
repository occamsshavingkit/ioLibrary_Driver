# Quickstart: Validating P0 Audit Fixes — W5500 ioLibrary_Driver

## Prerequisites

- C compiler with `-fsanitize=address,undefined` support (GCC or Clang)
- Git
- A build configured with `_WIZCHIP_ == 5500` and SPI IO mode

## Verification per Fix

### AUD-001: UDP loopback buffer validation

Validate that `loopback_udpc()` no longer writes OOB on negative or full-capacity recvfrom() returns.

```bash
# Compile with ASan/UBSan, mock recvfrom() to return DATA_BUF_SIZE, -13, and 0
# Expected: no ASan abort, no UBSan fault, function returns error for negative
```

### AUD-002: SO_KEEPALIVEAUTO getter width

Validate that the getter writes exactly 1 byte.

```bash
# Place uint8_t canaries around a uint8_t destination
# Call getsockopt(sn, SO_KEEPALIVEAUTO, &value)
# Expected: canaries intact, value contains valid 8-bit keepalive setting
```

### AUD-003: SPI callback initialization

Validate that WIZCHIP_READ() fails safely before SPI callbacks are registered.

```bash
# Build with _WIZCHIP_ == 5500, SPI mode
# Call WIZCHIP_READ(MR) before registering any SPI callbacks
# Expected: no HardFault, no BUS callback invoked, deterministic error returned
```

### AUD-004: Nonblocking TCP recv() check order

Validate that a nonblocking socket returns available data.

```bash
# Open TCP socket with SF_IO_NONBLOCK
# Inject 1 byte into RX buffer
# Call recv() — expected: returns 1 byte
# Call recv() again — expected: returns SOCK_BUSY
```

### AUD-005: ctlwizchip() NULL argument safety

Validate that documented no-argument calls do not dereference NULL.

```bash
# Build at -O0 and -O2
# Call ctlwizchip(CW_RESET_WIZCHIP, NULL) — expected: reset executes, no fault
# Call ctlwizchip(CW_INIT_WIZCHIP, NULL) — expected: default buffers used, no fault
# Call a command requiring arg with NULL — expected: clean error return
```

## System-Level Validation

All 5 fixes are independent, but a combined smoke test should verify:

1. `loopback_udpc()` runs with valid and edge-case datagrams without OOB access
2. `getsockopt()` returns correct widths for all socket options
3. `WIZCHIP_READ/WRITE` work correctly after SPI callback registration
4. Nonblocking TCP sockets can send and receive data
5. `ctlwizchip()` handles all command types with documented argument shapes

## Upstream PR Validation

Each fix must be verified before submitting its PR:

```bash
# For each AUD fix:
git checkout -b fix/aud-XXX-fix-description fork/master
# ... apply fix ...
git diff --check          # verify whitespace is clean
git show                   # verify only intended changes
# ... push and create PR against Wiznet/ioLibrary_Driver master ...
```
