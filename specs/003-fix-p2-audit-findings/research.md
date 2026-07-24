# Research: Fix P2 Audit Findings — W5500 ioLibrary_Driver

All decisions from TODO.md. Audit findings are source-confirmed with prescribed actions.

- **AUD-019**: Use `sn` var instead of hardcoded 0 in multicast helpers. Use `port` parameter instead of 3000.
- **AUD-020**: Compare `(getSn_MR(sn) & 0x0F)` with exact protocol values; merge `sock_io_mode` into SO_FLAG.
- **AUD-021**: In zero-payload completion path, preserve PACK_FIRST instead of overwriting with PACK_COMPLETED.
- **AUD-022**: `wizchip_setnetmode()`: clear controlled mask then OR requested value.
- **AUD-023**: `wizphy_setphypmode()`: compare `(tmp & PHYCFGR_OPMDC_ALLA)` for exact equality.
- **AUD-024**: Define allowed masks per protocol; reject `flag & ~mask` before register writes.
- **AUD-025**: Consistent pre-use increment with wrap check; avoid 0 and privileged ports.
- **AUD-026**: `#ifndef _WIZCHIP_` → `#error "Define _WIZCHIP_ to your WIZnet chip"`.
- **AUD-027**: Return error instead of spinning on compile-time-false mode checks.
- **AUD-028**: Fix legacy alias, macro spelling, format specifiers, braces around misleading indentation.
- **AUD-038**: Return `SOCKERR_SOCKCLOSED` after `close(sn)` in send() timeout path.
- **AUD-042**: Add `if (!addr || !port) return SOCKERR_ARG;` to IPv4 recvfrom() paths.
- **AUD-043**: Skip addr/port handling block when `Sn_MR_MACRAW` in sendto().
- **AUD-044**: Track in-progress disconnect with per-socket bit; poll on retry, don't re-issue DISCON.
- **AUD-045**: Add `if (!wizdata && len > 0) return;` guards.
- **AUD-049**: Compute group MAC from IP and call setSn_DHAR(sn, mac) before OPEN.
