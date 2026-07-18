# Research: Fix P0 Audit Findings — W5500 ioLibrary_Driver

All technical decisions are pre-determined by the audit findings in TODO.md. Each finding is source-confirmed and includes a prescribed action with verification criteria.

## AUD-001: UDP loopback OOB write

- **Decision**: Move the `ret <= 0` check before `buf[ret] = 0x00` in `loopback_udpc()`.
- **Rationale**: The audit confirmed that `ret == DATA_BUF_SIZE` writes one byte past a `DATA_BUF_SIZE` buffer (stack-buffer-overflow under ASan), and negative returns index before the buffer. Checking before indexing is the minimal correct fix.
- **Alternatives considered**: Changing the buffer capacity contract, using a safer recvfrom wrapper. Rejected as unnecessary scope creep — the check reorder is sufficient and minimal.

## AUD-002: SO_KEEPALIVEAUTO getter width

- **Decision**: Change `*(uint16_t*) arg =` to `*(uint8_t*) arg =` in `getsockopt()` at `socket.c:1383`.
- **Rationale**: The setter at `socket.c:1334` correctly reads `*(uint8_t*)arg`. The documentation at `socket.h:583` specifies `uint8_t`. The getter writes `uint16_t`, overwriting an adjacent byte. Minimal fix: match the getter to the setter and documentation.
- **Alternatives considered**: Typed option wrappers, argument-size parameter. Rejected as out of scope for a single-width fix.

## AUD-003: SPI callback initialization

- **Decision**: Conditionally initialize `WIZCHIP.IF.SPI` members with type-correct stubs when `_WIZCHIP_IO_MODE_` is a SPI mode, using preprocessor conditionals in the static initializer.
- **Rationale**: W5500 accesses `WIZCHIP.IF.SPI._read_byte()` and `WIZCHIP.IF.SPI._write_byte()` (signatures: `uint8_t (*)(void)`, `void (*)(uint8_t)`). The initializer only sets `WIZCHIP.IF.BUS._read_data()` (signature: `iodata_t (*)(uint32_t)`) — incompatible signatures through a union. Any pre-registration access invokes wrong function signatures. The fix must use `#if` in the static initializer to set the correct union member.
- **Alternatives considered**: Runtime initialization in `wizchip_init()`. Rejected because the global is a file-scope static initializer; converting to lazy init would be a larger refactor beyond a P0 fix.

## AUD-004: Nonblocking TCP recv() check order

- **Decision**: Swap the two conditions in the non-IPv6 `while(1)` loop at `socket.c:687-692` so `recvsize != 0` is checked before `sock_io_mode & (1 << sn)`.
- **Rationale**: The IPv6 branch at lines 679-685 already uses the correct order (check data first, then nonblocking mode). Matching this for W5500 fixes the stall without introducing new logic.
- **Alternatives considered**: Removing the nonblocking check entirely. Rejected — nonblocking sockets on empty buffers must still return `SOCK_BUSY` promptly.

## AUD-005: ctlwizchip() NULL dereference

- **Decision**: Move `uint8_t tmp = *(uint8_t*) arg;` inside only the cases that require it (`CW_SYS_LOCK`, `CW_SYS_UNLOCK`, `CW_GET_SYSLOCK` for W6100/W6300), and for W5500 builds where those cases are dead code, simply guard the dereference.
- **Rationale**: `CW_RESET_WIZCHIP` requires no argument, `CW_INIT_WIZCHIP` explicitly supports `arg == NULL`. The unconditional dereference at line 436 can fault at -O0. Each command's argument validation must be scoped to that command.
- **Alternatives considered**: Adding a NULL check before the dereference. Rejected as insufficient — the variable must remain valid only where used.

## Research Conclusions

All 5 findings have straightforward, minimal fixes. No new dependencies, no architecture changes. Each fix is independent and can be applied, tested, and submitted as a separate atomic commit/PR.
