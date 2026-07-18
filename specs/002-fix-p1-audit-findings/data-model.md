# Data Model: Fix P1 Audit Findings — W5500 ioLibrary_Driver

No new data entities are introduced. Most fixes modify existing function behavior or add validation checks. Three findings introduce new state.

## New State

### Polling iteration counter (AUD-007/036)

Each impacted polling loop gets a local `uint32_t` iteration counter compared against `_WIZCHIP_POLL_MAX_`. No global state changes.

### Pending datagram send tracker array `sock_is_sending_dgram[]` (AUD-015)

Per-socket bit in a new `sock_is_sending_dgram` array (analogous to `sock_is_sending` for TCP), tracking in-progress `sendto()` calls for nonblocking completion resolution.

### Persistent peer metadata (AUD-018)

In `loopback.c`, `destip` and `destport` variables in `loopback_udpc()` and related functions converted from automatic to `static`. Reinitialized only on `PACK_COMPLETED` or a new first chunk.

## Affected Existing Entities

### `WIZCHIP` global (AUD-008, AUD-010, AUD-013)

- **AUD-008**: Documentation added near `WIZCHIP_CRITICAL_ENTER/EXIT` macros warning against combining global IRQ masking with DMA callbacks.
- **AUD-010**: `wizchip_sw_reset()` gains VERSIONR validation and bounded retry. `wizchip_init()` calls the updated reset and fails on version mismatch.
- **AUD-013**: `wizchip_init()` adds pre-reset buffer layout validation with wider accumulator.

### Socket state arrays (`Ethernet/socket.c:62-70`)

- `sock_io_mode`: AUD-011 adds documentation and small critical-section guards.
- `sock_is_sending`: AUD-037 removes the unreachable second check block and adds explanatory comments.

### `socket()` function (`Ethernet/socket.c:202-249`)

- AUD-006: New protocol rejection checks added before the `while` wait loop at line 336.

### `sendto()` function (`Ethernet/socket.c:858-915`)

- AUD-015: Nonblocking path added; completion tracking via `sock_is_sending_dgram`.

### `recvfrom()` IPRAW path (`Ethernet/socket.c:1119-1221`)

- AUD-014: `pack_len` calculation and `wiz_recv_data()` moved outside zero-remainder condition.

### 16-bit accessor macros (`Ethernet/W5500/w5500.h`)

- AUD-040: Setter macros (setSn_TX_WR, setSn_RX_RD, etc.) gain single CRITICAL section wrappers.
- AUD-041: Getter macros for hardware-mutated registers (getSn_TX_RD, getSn_TX_WR, etc.) gain seqlock retry or CRITICAL wrapping.

### Application send loops (`Application/loopback/loopback.c`, `Application/multicast/multicast.c`)

- AUD-017: `SOCK_BUSY` handling added to all send loops.
- AUD-018: Peer metadata persistence via static variables.

### `recv()` TCP path (`Ethernet/socket.c:663-694`)

- AUD-036: `SOCK_CLOSE_WAIT`/zero-data branch gets explicit return or break.

### PHY reset (`Ethernet/wizchip_conf.c`)

- AUD-039: `wizphy_reset()` gains settle delay and bounded stability poll.

## Relationships

```text
socket_init() ──call──▶ AUD-006 guards ──▶ setSn_MR() [or return SOCKERR_SOCKMODE]
                           │
wizchip_init() ──call──▶ AUD-013 validation ──▶ wizchip_sw_reset() [AUD-010: VERSIONR check]
                           │
sendto() [nonblocking] ──▶ AUD-015: return after SEND ──▶ sock_is_sending_dgram
                           │
recvfrom() [IPRAW] ──▶ AUD-014: always calc pack_len ──▶ wiz_recv_data()
                           │
recv() [TCP] ──▶ AUD-007 deadline counter ──▶ AUD-036: early return
                           │
setSn_TX_WR() [16-bit] ──▶ AUD-040: single CRITICAL section
                           │
getSn_TX_RD() [16-bit] ──▶ AUD-041: seqlock retry
```
