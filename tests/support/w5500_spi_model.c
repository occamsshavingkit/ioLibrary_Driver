#include "w5500_spi_model.h"
#include <string.h>

#define MR_ADDR      0x0000
#define GAR0_ADDR    0x0001
#define SUBR0_ADDR   0x0005
#define SHAR0_ADDR   0x0009
#define SIPR0_ADDR   0x000F
#define SIR_ADDR     0x0017
#define RTR0_ADDR    0x0019
#define RCR0_ADDR    0x001B
#define PHYCFGR_ADDR 0x002E
#define VERSIONR_ADDR 0x0039

#define MR_RST       0x80

#define W5500_COMMON_BLOCK 0u
#define W5500_SOCKET_BLOCK(sn) (1u + 4u * (sn))
#define W5500_TX_BLOCK(sn)     (2u + 4u * (sn))
#define W5500_RX_BLOCK(sn)     (3u + 4u * (sn))

#define Sn_MR        0x0000
#define Sn_CR        0x0001
#define Sn_IR        0x0002
#define Sn_SR        0x0003
#define Sn_PORT      0x0004
#define Sn_DIPR0     0x000C
#define Sn_DPORT     0x0010
#define Sn_RXBUF_SIZE 0x001E
#define Sn_TXBUF_SIZE 0x001F
#define Sn_TX_FSR    0x0020
#define Sn_TX_RD     0x0022
#define Sn_TX_WR     0x0024
#define Sn_RX_RSR    0x0026
#define Sn_RX_RD     0x0028
#define Sn_RX_WR     0x002A

#define Sn_CR_OPEN   0x01
#define Sn_CR_LISTEN 0x02
#define Sn_CR_CONNECT 0x04
#define Sn_CR_DISCON 0x08
#define Sn_CR_CLOSE  0x10
#define Sn_CR_SEND   0x20
#define Sn_CR_SEND_MAC 0x21
#define Sn_CR_SEND_KEEP 0x22
#define Sn_CR_RECV   0x40

#define Sn_IR_SENDOK  0x10
#define Sn_IR_TIMEOUT 0x08
#define Sn_IR_RECV    0x04
#define Sn_IR_DISCON  0x02
#define Sn_IR_CON     0x01

#define Sn_MR_PROTOCOL_MASK 0x0F
#define Sn_MR_TCP     0x01
#define Sn_MR_UDP     0x02
#define Sn_MR_IPRAW   0x03
#define Sn_MR_MACRAW  0x04

#define SOCK_CLOSED 0x00
#define SOCK_INIT   0x13
#define SOCK_UDP    0x22
#define SOCK_IPRAW  0x32
#define SOCK_MACRAW 0x42

#define SIR_SOCKET_MASK(sn) (1u << (sn))

#define PHYCFGR_RST  0x80
#define PHYCFGR_OPMD 0x40
#define PHYCFGR_DPX  0x04
#define PHYCFGR_SPD  0x02
#define PHYCFGR_LNK  0x01

void model_init(w5500_model_t *m)
{
    memset(m, 0, sizeof(*m));
    m->versionr = 0x04;
    m->phycfgr = PHYCFGR_LNK;
    for (int i = 0; i < W5500_MODEL_SOCKET_COUNT; i++) {
        m->sockets[i].sr = 0x00;
        m->sockets[i].tx_fsr = 2048;
        m->tx_buf_sizes[i] = 2;
        m->rx_buf_sizes[i] = 2;
    }
}

void model_reset(w5500_model_t *m)
{
    model_init(m);
}

void model_tick_us(w5500_model_t *m, uint64_t delta_us)
{
    m->monotonic_us += delta_us;
}

static uint8_t read_common_register(const w5500_model_t *m, uint16_t offset)
{
    switch (offset) {
    case 0x0000: return m->mr;
    case 0x0001: return m->gar[0];
    case 0x0002: return m->gar[1];
    case 0x0003: return m->gar[2];
    case 0x0004: return m->gar[3];
    case 0x0005: return m->subr[0];
    case 0x0006: return m->subr[1];
    case 0x0007: return m->subr[2];
    case 0x0008: return m->subr[3];
    case 0x0009: return m->shar[0];
    case 0x000A: return m->shar[1];
    case 0x000B: return m->shar[2];
    case 0x000C: return m->shar[3];
    case 0x000D: return m->shar[4];
    case 0x000E: return m->shar[5];
    case 0x000F: return m->sipr[0];
    case 0x0010: return m->sipr[1];
    case 0x0011: return m->sipr[2];
    case 0x0012: return m->sipr[3];
    case SIR_ADDR: return m->sir;
    case 0x0019: return (uint8_t)(m->rtr >> 8);
    case 0x001A: return (uint8_t)(m->rtr & 0xFF);
    case RCR0_ADDR: return (uint8_t)m->rcr;
    case PHYCFGR_ADDR: return m->phycfgr;
    case VERSIONR_ADDR: return m->versionr;
    default: return 0x00;
    }
}

static void write_common_register(w5500_model_t *m, uint16_t offset, uint8_t v)
{
    if (offset == MR_ADDR && (v & MR_RST) != 0u) {
        uint64_t monotonic_us = m->monotonic_us;
        int lock_enter_count = m->lock_enter_count;
        int lock_exit_count = m->lock_exit_count;
        int cs_state = m->cs_state;
        uint8_t spi_busy = m->spi_busy;
        int8_t spi_error = m->spi_error;
        uint16_t spi_offset = m->spi_offset;
        uint8_t spi_block = m->spi_block;
        uint8_t spi_control = m->spi_control;
        uint8_t spi_header_bytes = m->spi_header_bytes;

        model_init(m);
        m->monotonic_us = monotonic_us;
        m->lock_enter_count = lock_enter_count;
        m->lock_exit_count = lock_exit_count;
        m->cs_state = cs_state;
        m->spi_busy = spi_busy;
        m->spi_error = spi_error;
        m->spi_offset = spi_offset;
        m->spi_block = spi_block;
        m->spi_control = spi_control;
        m->spi_header_bytes = spi_header_bytes;
        return;
    }

    switch (offset) {
    case 0x0000: m->mr = v; break;
    case 0x0001: m->gar[0] = v; break;
    case 0x0002: m->gar[1] = v; break;
    case 0x0003: m->gar[2] = v; break;
    case 0x0004: m->gar[3] = v; break;
    case 0x0005: m->subr[0] = v; break;
    case 0x0006: m->subr[1] = v; break;
    case 0x0007: m->subr[2] = v; break;
    case 0x0008: m->subr[3] = v; break;
    case 0x0009: m->shar[0] = v; break;
    case 0x000A: m->shar[1] = v; break;
    case 0x000B: m->shar[2] = v; break;
    case 0x000C: m->shar[3] = v; break;
    case 0x000D: m->shar[4] = v; break;
    case 0x000E: m->shar[5] = v; break;
    case 0x000F: m->sipr[0] = v; break;
    case 0x0010: m->sipr[1] = v; break;
    case 0x0011: m->sipr[2] = v; break;
    case 0x0012: m->sipr[3] = v; break;
    case SIR_ADDR:
        m->sir &= ~v;  /* W1C: write 1 clears */
        m->sir |= (v & 0x80);
        break;
    case 0x0019: m->rtr = (m->rtr & 0x00FF) | ((uint16_t)v << 8); break;
    case 0x001A: m->rtr = (m->rtr & 0xFF00) | v; break;
    case RCR0_ADDR: m->rcr = v; break;
    case PHYCFGR_ADDR: m->phycfgr = v; break;
    default: break;
    }
}

static uint8_t read_socket_register(const w5500_model_t *m, int sn, uint16_t offset)
{
    const model_socket_t *s = &m->sockets[sn];
    (void)m;
    switch (offset) {
    case Sn_MR: return s->mr;
    case Sn_CR: return s->cr;
    case Sn_IR: return s->ir;
    case Sn_SR: return s->sr;
    case Sn_PORT: return (uint8_t)(s->port >> 8);
    case Sn_PORT + 1: return (uint8_t)(s->port & 0xFF);
    case Sn_DIPR0: return s->dipr[0];
    case Sn_DIPR0 + 1: return s->dipr[1];
    case Sn_DIPR0 + 2: return s->dipr[2];
    case Sn_DIPR0 + 3: return s->dipr[3];
    case Sn_DPORT: return (uint8_t)(s->dport >> 8);
    case Sn_DPORT + 1: return (uint8_t)(s->dport & 0xFF);
    case Sn_RXBUF_SIZE: return m->rx_buf_sizes[sn];
    case Sn_TXBUF_SIZE: return m->tx_buf_sizes[sn];
    case Sn_TX_FSR: return (uint8_t)(s->tx_fsr >> 8);
    case Sn_TX_FSR + 1: return (uint8_t)(s->tx_fsr & 0xFF);
    case Sn_TX_RD: return (uint8_t)(s->tx_rr >> 8);
    case Sn_TX_RD + 1: return (uint8_t)(s->tx_rr & 0xFF);
    case Sn_TX_WR: return (uint8_t)(s->tx_wr >> 8);
    case Sn_TX_WR + 1: return (uint8_t)(s->tx_wr & 0xFF);
    case Sn_RX_RSR: return (uint8_t)(s->rx_rsr >> 8);
    case Sn_RX_RSR + 1: return (uint8_t)(s->rx_rsr & 0xFF);
    case Sn_RX_RD: return (uint8_t)(s->rx_rd >> 8);
    case Sn_RX_RD + 1: return (uint8_t)(s->rx_rd & 0xFF);
    case Sn_RX_WR: return (uint8_t)(s->rx_wr >> 8);
    case Sn_RX_WR + 1: return (uint8_t)(s->rx_wr & 0xFF);
    default: return 0x00;
    }
}

static void write_socket_register(w5500_model_t *m, int sn, uint16_t offset, uint8_t v)
{
    model_socket_t *s = &m->sockets[sn];
    switch (offset) {
    case Sn_MR: s->mr = v; break;
    case Sn_CR:
        s->cr = v;
        switch (v) {
        case Sn_CR_OPEN:
            switch (s->mr & Sn_MR_PROTOCOL_MASK) {
            case Sn_MR_TCP: s->sr = SOCK_INIT; break;
            case Sn_MR_UDP: s->sr = SOCK_UDP; break;
            case Sn_MR_IPRAW: s->sr = SOCK_IPRAW; break;
            case Sn_MR_MACRAW: s->sr = SOCK_MACRAW; break;
            default: s->sr = SOCK_CLOSED; break;
            }
            s->ir = 0x00;
            s->tx_wr = 0x0000;
            s->tx_rr = 0x0000;
            s->rx_rd = 0x0000;
            s->rx_wr = 0x0000;
            s->tx_fsr = m->tx_buf_sizes[sn] * 1024;
            s->rx_rsr = 0x0000;
            break;
        case Sn_CR_LISTEN: s->sr = 0x14; break;
        case Sn_CR_CONNECT: s->sr = 0x17; s->ir |= Sn_IR_CON; break;
        case Sn_CR_DISCON: s->sr = 0x00; s->ir |= Sn_IR_DISCON; break;
        case Sn_CR_CLOSE: s->sr = 0x00; s->ir |= Sn_IR_DISCON; break;
        case Sn_CR_SEND: s->ir |= Sn_IR_SENDOK; break;
        case Sn_CR_SEND_KEEP: s->ir |= Sn_IR_SENDOK; break;
        case Sn_CR_RECV: s->ir |= Sn_IR_RECV; break;
        default: break;
        }
        s->cr = 0u;
        break;
    case Sn_IR:
        s->ir &= ~v; /* W1C */
        break;
    case Sn_SR: s->sr = v; break;
    case Sn_PORT: s->port = (s->port & 0x00FF) | ((uint16_t)v << 8); break;
    case Sn_PORT + 1: s->port = (s->port & 0xFF00) | v; break;
    case Sn_DIPR0: s->dipr[0] = v; break;
    case Sn_DIPR0 + 1: s->dipr[1] = v; break;
    case Sn_DIPR0 + 2: s->dipr[2] = v; break;
    case Sn_DIPR0 + 3: s->dipr[3] = v; break;
    case Sn_DPORT: s->dport = (s->dport & 0x00FF) | ((uint16_t)v << 8); break;
    case Sn_DPORT + 1: s->dport = (s->dport & 0xFF00) | v; break;
    case Sn_RXBUF_SIZE:
        m->rx_buf_sizes[sn] = v;
        break;
    case Sn_TXBUF_SIZE:
        m->tx_buf_sizes[sn] = v;
        s->tx_fsr = (uint16_t)v * 1024u;
        break;
    case Sn_TX_WR: s->tx_wr = (s->tx_wr & 0x00FF) | ((uint16_t)v << 8); break;
    case Sn_TX_WR + 1: s->tx_wr = (s->tx_wr & 0xFF00) | v; break;
    case Sn_RX_RD: s->rx_rd = (s->rx_rd & 0x00FF) | ((uint16_t)v << 8); break;
    case Sn_RX_RD + 1: s->rx_rd = (s->rx_rd & 0xFF00) | v; break;
    case Sn_RX_WR: s->rx_wr = (s->rx_wr & 0x00FF) | ((uint16_t)v << 8); break;
    case Sn_RX_WR + 1: s->rx_wr = (s->rx_wr & 0xFF00) | v; break;
    default: break;
    }
}

uint8_t model_read8(const w5500_model_t *m, uint32_t addr)
{
    uint8_t block = (uint8_t)((addr >> 3) & 0x1Fu);
    uint16_t offset = (uint16_t)(addr >> 8);
    int sn;

    if (block == W5500_COMMON_BLOCK) return read_common_register(m, offset);

    for (sn = 0; sn < W5500_MODEL_SOCKET_COUNT; ++sn) {
        if (block == W5500_SOCKET_BLOCK(sn)) {
            return read_socket_register(m, sn, offset);
        }
        if (block == W5500_TX_BLOCK(sn)) {
            return m->tx_data[sn][offset % sizeof(m->tx_data[sn])];
        }
        if (block == W5500_RX_BLOCK(sn)) {
            return m->rx_data[sn][offset % sizeof(m->rx_data[sn])];
        }
    }
    return 0x00;
}

void model_write8(w5500_model_t *m, uint32_t addr, uint8_t v)
{
    uint8_t block = (uint8_t)((addr >> 3) & 0x1Fu);
    uint16_t offset = (uint16_t)(addr >> 8);
    int sn;

    if (block == W5500_COMMON_BLOCK) {
        write_common_register(m, offset, v);
    } else for (sn = 0; sn < W5500_MODEL_SOCKET_COUNT; ++sn) {
        if (block == W5500_SOCKET_BLOCK(sn)) {
            write_socket_register(m, sn, offset, v);
            break;
        }
        if (block == W5500_TX_BLOCK(sn)) {
            m->tx_data[sn][offset % sizeof(m->tx_data[sn])] = v;
            break;
        }
        if (block == W5500_RX_BLOCK(sn)) {
            m->rx_data[sn][offset % sizeof(m->rx_data[sn])] = v;
            break;
        }
    }
    model_log_op(m, MODEL_OP_WRITE8, addr, v, 0);
}

uint16_t model_read16(const w5500_model_t *m, uint32_t addr)
{
    uint8_t hi = model_read8(m, addr);
    uint8_t lo = model_read8(m, addr + 0x100u);
    return ((uint16_t)hi << 8) | lo;
}

void model_write16(w5500_model_t *m, uint32_t addr, uint16_t v)
{
    model_write8(m, addr, (uint8_t)(v >> 8));
    model_write8(m, addr + 0x100u, (uint8_t)(v & 0xFF));
}

void model_read_buf(w5500_model_t *m, uint32_t addr, uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
        buf[i] = model_read8(m, addr + (uint32_t)i * 0x100u);
    model_log_op(m, MODEL_OP_READ_BUF, addr, 0, len);
}

void model_write_buf(w5500_model_t *m, uint32_t addr, const uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
        model_write8(m, addr + (uint32_t)i * 0x100u, buf[i]);
    model_log_op(m, MODEL_OP_WRITE_BUF, addr, 0, len);
}

void model_log_op(w5500_model_t *m, model_op_type_t op, uint32_t addr,
                  uint8_t data8, uint16_t len)
{
    if (m->log_count >= W5500_MODEL_LOG_SIZE) return;
    m->log[m->log_count].op = op;
    m->log[m->log_count].addr = addr;
    m->log[m->log_count].data8 = data8;
    m->log[m->log_count].len = len;
    m->log_count++;
}

const model_log_entry_t *model_find_op(const w5500_model_t *m, model_op_type_t op,
                                        uint32_t addr, size_t *start)
{
    size_t idx = start ? *start : 0;
    for (; idx < m->log_count; idx++) {
        if (m->log[idx].op == op && m->log[idx].addr == addr) {
            if (start) *start = idx + 1;
            return &m->log[idx];
        }
    }
    return NULL;
}

uint64_t model_monotonic_us(const w5500_model_t *m)
{
    return m->monotonic_us;
}

void model_cs_select(w5500_model_t *m)
{
    m->cs_state = 1;
    m->spi_offset = 0u;
    m->spi_block = 0u;
    m->spi_control = 0u;
    m->spi_header_bytes = 0u;
}
void model_cs_deselect(w5500_model_t *m) { m->cs_state = 0; }

uint8_t model_spi_read_byte(w5500_model_t *m)
{
    uint32_t addr = ((uint32_t)m->spi_offset << 8) |
                    ((uint32_t)m->spi_block << 3);
    uint8_t value = model_read8(m, addr);

    ++m->spi_offset;
    return value;
}

void model_spi_write_byte(w5500_model_t *m, uint8_t value)
{
    if (m->spi_header_bytes == 0u) {
        m->spi_offset = (uint16_t)value << 8;
        ++m->spi_header_bytes;
        return;
    } else if (m->spi_header_bytes == 1u) {
        m->spi_offset |= value;
        ++m->spi_header_bytes;
        return;
    } else if (m->spi_header_bytes == 2u) {
        m->spi_control = value;
        m->spi_block = (uint8_t)((value >> 3) & 0x1fu);
        ++m->spi_header_bytes;
        return;
    } else {
        uint32_t addr = ((uint32_t)m->spi_offset << 8) |
                        ((uint32_t)m->spi_block << 3);

        model_write8(m, addr, value);
        ++m->spi_offset;
    }
}

void model_spi_read_burst(w5500_model_t *m, uint8_t *buf, uint16_t len)
{
    uint16_t i;

    for (i = 0u; i < len; ++i) {
        buf[i] = model_spi_read_byte(m);
    }
}

void model_spi_write_burst(w5500_model_t *m, const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    for (i = 0u; i < len; ++i) {
        model_spi_write_byte(m, buf[i]);
    }
}

void model_lock_enter(w5500_model_t *m) { m->lock_enter_count++; }
void model_lock_exit(w5500_model_t *m)  { m->lock_exit_count++; }

int model_lock_balance(const w5500_model_t *m)
{
    return m->lock_exit_count - m->lock_enter_count;
}
