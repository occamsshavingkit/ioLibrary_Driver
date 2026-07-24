# Feature Specification: Fix P3 Audit Findings — W5500 ioLibrary_Driver

**Branch**: `004-fix-p3-audit-findings` | **Created**: 2026-07-18

## P3 Findings (Efficiency & Hardening)

| AUD | Category | Summary |
|-----|----------|---------|
| AUD-029 | Diagnostics | Default payload logging off; bounded metadata |
| AUD-030 | Efficiency | Sequential VDM 16-bit transfers |
| AUD-031 | Efficiency | Deduplicate mode reads in sendto() |
| AUD-032 | Efficiency | Burst SPI path docs |
| AUD-033 | Efficiency | Single RX pointer commit |
| AUD-034 | Efficiency | Avoid duplicate RX probes |
| AUD-035 | Efficiency | INTn/SIR scheduling guidance |
| AUD-046 | Efficiency | Cache buffer sizes |
| AUD-047 | Correctness | Early return for len==0 in wiz_recv_ignore |
| AUD-048 | Efficiency | Read IR+SIR in one VDM frame |

## Requirements

- **FR-001**: Each fix on separate branch from fork/master, single commit, independent.
- **FR-002**: AUD-029: Disable raw payload logging in production builds. Log metadata only.
- **FR-003**: AUD-030: Add 16-bit VDM read/write helpers using WIZCHIP_READ_BUF/WRITE_BUF.
- **FR-004**: AUD-031: Read Sn_MR once, validate first, write dest once in sendto().
- **FR-005**: AUD-032: Document burst SPI callback path in header comments.
- **FR-006**: AUD-033: Maintain local RX pointer; commit once per packet in recvfrom.
- **FR-007**: AUD-047: Add `if (len == 0) return;` to wiz_recv_ignore().
- **FR-008**: AUD-046: Cache Sn_TXBUF_SIZE/Sn_RXBUF_SIZE after init, invalidate on reconfigure.
- **FR-009**: AUD-048: Read IR+SIR in single 3-byte VDM burst in wizchip_getinterrupt().

## Success Criteria

- All branches compile for W5500. Each single commit. Clean apply to fork/master.
