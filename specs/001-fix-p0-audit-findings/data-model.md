# Data Model: Fix P0 Audit Findings — W5500 ioLibrary_Driver

No new data entities are introduced. The fixes modify only the behavior of existing functions within their current data context.

## Existing Entities (Affected)

### `WIZCHIP` global instance (`Ethernet/wizchip_conf.c:256`)

The global `_WIZCHIP` struct instance, shared across all socket and chip access paths.

- **Fix AUD-003**: Changes which union member (`IF.SPI` vs `IF.BUS`) is initialized in the static initializer.
- **Fix AUD-005**: Changes argument validation in `ctlwizchip()` which operates on `WIZCHIP`.

### Socket state arrays (`Ethernet/socket.c:62-70`)

- `sock_io_mode`: Per-socket nonblocking/blocking mode bits
- `sock_is_sending`: Per-socket pending-send tracking
- `sock_remained_size`: Per-socket partial-receive tracking

**Fix AUD-004**: Changes the order in which `sock_io_mode` and receive buffer status are evaluated in the TCP `recv()` polling loop.

### `buf` parameter in loopback_udpc() (`Application/loopback/loopback.c:238`)

The caller-provided buffer pointer and its implicit capacity `DATA_BUF_SIZE`.

**Fix AUD-001**: Changes the order of operations so buffer index bounds are checked before use.

### Socket option output parameters (`Ethernet/socket.c:1377-1384`)

The `void* arg` passed to `getsockopt()`, which the caller interprets according to the documented type for each option.

**Fix AUD-002**: Changes the output width for `SO_KEEPALIVEAUTO` from `uint16_t` (2 bytes written) to `uint8_t` (1 byte written), matching the documented type.

## Relationships

```
loopback_udpc() ──calls──▶ recvfrom() ──reads──▶ socket state arrays
                                  ──writes─▶ buf[DATA_BUF_SIZE]

getsockopt()    ──writes─▶ caller's arg buffer

WIZCHIP_READ()  ──calls──▶ WIZCHIP.IF.SPI._read_byte()

ctlwizchip()    ──reads──▶ arg pointer
               ──calls──▶ wizchip_sw_reset(), wizchip_init()
```
