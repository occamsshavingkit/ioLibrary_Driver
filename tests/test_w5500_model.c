#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SOCKET_COUNT  8
#define REG_SIZE      0x10000
#define MAX_BUF_PER_SN 2048

#define W5500_ADDR(off, blk)  (((uint32_t)(off) << 8) + ((uint32_t)(blk) << 3))
#define W5500_BLOCK(a)        ((((a) & 0xF8) >> 3) & 0x1F)
#define W5500_OFFSET_HI(a)    (((a) >> 8) & 0xFFFF)
#define W5500_OFFSET_LO(a)    (((a) >> 8) & 0xFF)

#define WIZCHIP_CREG_BLOCK    0
#define WIZCHIP_SREG_BLOCK(N) (1 + 4 * (N))
#define WIZCHIP_TXBUF_BLOCK(N) (2 + 4 * (N))
#define WIZCHIP_RXBUF_BLOCK(N) (3 + 4 * (N))

#define ADDR_MR        W5500_ADDR(0x0000, WIZCHIP_CREG_BLOCK)
#define ADDR_GAR       W5500_ADDR(0x0001, WIZCHIP_CREG_BLOCK)
#define ADDR_SUBR      W5500_ADDR(0x0005, WIZCHIP_CREG_BLOCK)
#define ADDR_SHAR      W5500_ADDR(0x0009, WIZCHIP_CREG_BLOCK)
#define ADDR_SIPR      W5500_ADDR(0x000F, WIZCHIP_CREG_BLOCK)
#define ADDR_INTLEVEL  W5500_ADDR(0x0013, WIZCHIP_CREG_BLOCK)
#define ADDR_IR        W5500_ADDR(0x0015, WIZCHIP_CREG_BLOCK)
#define ADDR_IMR       W5500_ADDR(0x0016, WIZCHIP_CREG_BLOCK)
#define ADDR_SIR       W5500_ADDR(0x0017, WIZCHIP_CREG_BLOCK)
#define ADDR_SIMR      W5500_ADDR(0x0018, WIZCHIP_CREG_BLOCK)
#define ADDR_RTR       W5500_ADDR(0x0019, WIZCHIP_CREG_BLOCK)
#define ADDR_RCR       W5500_ADDR(0x001B, WIZCHIP_CREG_BLOCK)
#define ADDR_PTIMER    W5500_ADDR(0x001C, WIZCHIP_CREG_BLOCK)
#define ADDR_PMAGIC    W5500_ADDR(0x001D, WIZCHIP_CREG_BLOCK)
#define ADDR_PHAR      W5500_ADDR(0x001E, WIZCHIP_CREG_BLOCK)
#define ADDR_PSID      W5500_ADDR(0x0024, WIZCHIP_CREG_BLOCK)
#define ADDR_PMRU      W5500_ADDR(0x0026, WIZCHIP_CREG_BLOCK)
#define ADDR_UIPR      W5500_ADDR(0x0028, WIZCHIP_CREG_BLOCK)
#define ADDR_UPORTR    W5500_ADDR(0x002C, WIZCHIP_CREG_BLOCK)
#define ADDR_PHYCFGR   W5500_ADDR(0x002E, WIZCHIP_CREG_BLOCK)
#define ADDR_VERSIONR  W5500_ADDR(0x0039, WIZCHIP_CREG_BLOCK)

#define Sn_MR_OFF        0x0000
#define Sn_CR_OFF        0x0001
#define Sn_IR_OFF        0x0002
#define Sn_SR_OFF        0x0003
#define Sn_PORT_OFF      0x0004
#define Sn_DHAR_OFF      0x0006
#define Sn_DIPR_OFF      0x000C
#define Sn_DPORT_OFF     0x0010
#define Sn_MSSR_OFF      0x0012
#define Sn_TOS_OFF       0x0015
#define Sn_TTL_OFF       0x0016
#define Sn_RXBUF_SIZE_OFF 0x001E
#define Sn_TXBUF_SIZE_OFF 0x001F
#define Sn_TX_FSR_OFF    0x0020
#define Sn_TX_RD_OFF     0x0022
#define Sn_TX_WR_OFF     0x0024
#define Sn_RX_RSR_OFF    0x0026
#define Sn_RX_RD_OFF     0x0028
#define Sn_RX_WR_OFF     0x002A
#define Sn_IMR_OFF       0x002C
#define Sn_FRAG_OFF      0x002D
#define Sn_KPALVTR_OFF   0x002F

#define Sn_MR(N)    W5500_ADDR(Sn_MR_OFF,   WIZCHIP_SREG_BLOCK(N))
#define Sn_CR(N)    W5500_ADDR(Sn_CR_OFF,   WIZCHIP_SREG_BLOCK(N))
#define Sn_IR(N)    W5500_ADDR(Sn_IR_OFF,   WIZCHIP_SREG_BLOCK(N))
#define Sn_SR(N)    W5500_ADDR(Sn_SR_OFF,   WIZCHIP_SREG_BLOCK(N))
#define Sn_PORT(N)  W5500_ADDR(Sn_PORT_OFF, WIZCHIP_SREG_BLOCK(N))
#define Sn_DHAR(N)  W5500_ADDR(Sn_DHAR_OFF, WIZCHIP_SREG_BLOCK(N))
#define Sn_DIPR(N)  W5500_ADDR(Sn_DIPR_OFF, WIZCHIP_SREG_BLOCK(N))
#define Sn_DPORT(N) W5500_ADDR(Sn_DPORT_OFF, WIZCHIP_SREG_BLOCK(N))
#define Sn_MSSR(N)  W5500_ADDR(Sn_MSSR_OFF, WIZCHIP_SREG_BLOCK(N))
#define Sn_TOS(N)   W5500_ADDR(Sn_TOS_OFF,  WIZCHIP_SREG_BLOCK(N))
#define Sn_TTL(N)   W5500_ADDR(Sn_TTL_OFF,  WIZCHIP_SREG_BLOCK(N))
#define Sn_RXBUF_SIZE(N) W5500_ADDR(Sn_RXBUF_SIZE_OFF, WIZCHIP_SREG_BLOCK(N))
#define Sn_TXBUF_SIZE(N) W5500_ADDR(Sn_TXBUF_SIZE_OFF, WIZCHIP_SREG_BLOCK(N))
#define Sn_TX_FSR(N) W5500_ADDR(Sn_TX_FSR_OFF, WIZCHIP_SREG_BLOCK(N))
#define Sn_TX_RD(N) W5500_ADDR(Sn_TX_RD_OFF, WIZCHIP_SREG_BLOCK(N))
#define Sn_TX_WR(N) W5500_ADDR(Sn_TX_WR_OFF, WIZCHIP_SREG_BLOCK(N))
#define Sn_RX_RSR(N) W5500_ADDR(Sn_RX_RSR_OFF, WIZCHIP_SREG_BLOCK(N))
#define Sn_RX_RD(N) W5500_ADDR(Sn_RX_RD_OFF, WIZCHIP_SREG_BLOCK(N))
#define Sn_RX_WR(N) W5500_ADDR(Sn_RX_WR_OFF, WIZCHIP_SREG_BLOCK(N))
#define Sn_IMR(N)   W5500_ADDR(Sn_IMR_OFF,   WIZCHIP_SREG_BLOCK(N))
#define Sn_FRAG(N)  W5500_ADDR(Sn_FRAG_OFF,  WIZCHIP_SREG_BLOCK(N))
#define Sn_KPALVTR(N) W5500_ADDR(Sn_KPALVTR_OFF, WIZCHIP_SREG_BLOCK(N))

#define Sn_MR_PROTO_MASK    0x0F
#define Sn_MR_CLOSE         0x00
#define Sn_MR_TCP           0x01
#define Sn_MR_UDP           0x02
#define Sn_MR_MACRAW        0x04

#define Sn_CR_OPEN          0x01
#define Sn_CR_LISTEN        0x02
#define Sn_CR_CONNECT       0x04
#define Sn_CR_DISCON        0x08
#define Sn_CR_CLOSE         0x10
#define Sn_CR_SEND          0x20
#define Sn_CR_SEND_MAC      0x21
#define Sn_CR_SEND_KEEP     0x22
#define Sn_CR_RECV          0x40

#define SOCK_CLOSED         0x00
#define SOCK_INIT           0x13
#define SOCK_LISTEN         0x14
#define SOCK_ESTABLISHED    0x17
#define SOCK_FIN_WAIT       0x18
#define SOCK_CLOSE_WAIT     0x1C
#define SOCK_UDP            0x22
#define SOCK_MACRAW         0x42

#define Sn_IR_SENDOK        0x10
#define Sn_IR_TIMEOUT       0x08
#define Sn_IR_RECV          0x04

#define MR_RST              0x80
#define PHYCFGR_LNK_ON      0x01

#define TXBUF_BASE         0x4000
#define RXBUF_BASE         0x8000
#define TXBUF_TOTAL_SIZE   (SOCKET_COUNT * MAX_BUF_PER_SN)
#define RXBUF_TOTAL_SIZE   (SOCKET_COUNT * MAX_BUF_PER_SN)

typedef struct {
    uint8_t  register_file[REG_SIZE];
    uint32_t tick;
    uint32_t stuck_miso_count;
    uint16_t tx_buf_size[SOCKET_COUNT];
    uint16_t rx_buf_size[SOCKET_COUNT];
} w5500_model_t;

static w5500_model_t model;

static inline uint16_t reg_read16(const uint8_t *rf, uint32_t addr)
{
    uint32_t a = addr & 0xFFFF;
    return ((uint16_t)rf[a] << 8) | rf[(a + 1) & 0xFFFF];
}

static inline void reg_write16(uint8_t *rf, uint32_t addr, uint16_t val)
{
    uint32_t a = addr & 0xFFFF;
    rf[a] = (uint8_t)(val >> 8);
    rf[(a + 1) & 0xFFFF] = (uint8_t)(val);
}

static uint8_t socket_from_sreg_block(uint8_t block)
{
    if (block >= 1 && ((block - 1) % 4) == 0) {
        uint8_t sn = (block - 1) / 4;
        if (sn < SOCKET_COUNT) return sn;
    }
    return 0xFF;
}

static int is_tx_buf_block(uint8_t block, uint8_t *sn)
{
    if (block >= 2 && ((block - 2) % 4) == 0) {
        *sn = (block - 2) / 4;
        return (*sn < SOCKET_COUNT);
    }
    return 0;
}

static int is_rx_buf_block(uint8_t block, uint8_t *sn)
{
    if (block >= 3 && ((block - 3) % 4) == 0) {
        *sn = (block - 3) / 4;
        return (*sn < SOCKET_COUNT);
    }
    return 0;
}

static uint32_t buffer_phys_addr(uint32_t w5500_addr)
{
    uint8_t block = W5500_BLOCK(w5500_addr);
    uint16_t ptr  = W5500_OFFSET_HI(w5500_addr);
    uint8_t sn;
    uint16_t mask;

    if (is_tx_buf_block(block, &sn)) {
        mask = (model.tx_buf_size[sn] != 0) ? (model.tx_buf_size[sn] - 1) : 0;
        return TXBUF_BASE + (uint32_t)sn * MAX_BUF_PER_SN + (ptr & mask);
    }
    if (is_rx_buf_block(block, &sn)) {
        mask = (model.rx_buf_size[sn] != 0) ? (model.rx_buf_size[sn] - 1) : 0;
        return RXBUF_BASE + (uint32_t)sn * MAX_BUF_PER_SN + (ptr & mask);
    }
    return (uint32_t)-1;
}

static void handle_cr_write(uint32_t addr, uint8_t val)
{
    uint8_t block = W5500_BLOCK(addr);
    uint8_t sn    = socket_from_sreg_block(block);
    if (sn >= SOCKET_COUNT) return;

    uint32_t base  = Sn_MR(sn);
    uint32_t cr_a  = Sn_CR(sn);
    uint32_t sr_a  = Sn_SR(sn);
    uint32_t ir_a  = Sn_IR(sn);
    uint32_t txwr_a = Sn_TX_WR(sn);
    uint32_t txrd_a = Sn_TX_RD(sn);
    uint32_t rxrd_a = Sn_RX_RD(sn);

    if (val & Sn_CR_OPEN) {
        uint8_t proto = model.register_file[base & 0xFFFF] & Sn_MR_PROTO_MASK;
        if (proto == Sn_MR_TCP) {
            model.register_file[sr_a & 0xFFFF] = SOCK_INIT;
        } else if (proto == Sn_MR_UDP) {
            model.register_file[sr_a & 0xFFFF] = SOCK_UDP;
        } else if (proto == Sn_MR_MACRAW) {
            model.register_file[sr_a & 0xFFFF] = SOCK_MACRAW;
        }
        reg_write16(model.register_file, txwr_a, 0);
        reg_write16(model.register_file, txrd_a, 0);
        reg_write16(model.register_file, Sn_RX_RD(sn), 0);
        reg_write16(model.register_file, Sn_RX_WR(sn), 0);
    }

    if (val & Sn_CR_SEND) {
        uint16_t txwr = reg_read16(model.register_file, txwr_a);
        uint16_t mask = (model.tx_buf_size[sn] != 0) ? (model.tx_buf_size[sn] - 1) : 0;
        txwr = (txwr + 2) & mask;
        reg_write16(model.register_file, txwr_a, txwr);
        model.register_file[ir_a & 0xFFFF] |= Sn_IR_SENDOK;
    }

    if (val & Sn_CR_RECV) {
        uint16_t mask = (model.rx_buf_size[sn] != 0) ? (model.rx_buf_size[sn] - 1) : 0;
        uint16_t rxrd = reg_read16(model.register_file, rxrd_a);
        rxrd = (rxrd + 2) & mask;
        reg_write16(model.register_file, rxrd_a, rxrd);
    }

    if (val & Sn_CR_CLOSE) {
        model.register_file[sr_a & 0xFFFF] = SOCK_CLOSED;
        model.register_file[base & 0xFFFF] = Sn_MR_CLOSE;
    }

    model.register_file[cr_a & 0xFFFF] = 0x00;
}

static void handle_phycfgr_write(uint32_t addr, uint8_t val)
{
    (void)val;
    (void)addr;
}

static void handle_mr_write(uint32_t addr, uint8_t val)
{
    if (val & MR_RST) {
        model.register_file[addr & 0xFFFF] = val;
        model.register_file[addr & 0xFFFF] &= (uint8_t)(~MR_RST);
        return;
    }
    model.register_file[addr & 0xFFFF] = val;
}

static uint8_t model_read_reg(uint32_t addr)
{
    if (model.stuck_miso_count > 0) {
        model.stuck_miso_count--;
        return 0xFF;
    }

    if (addr == ADDR_VERSIONR) {
        return 0x04;
    }
    if (addr == ADDR_PHYCFGR) {
        return PHYCFGR_LNK_ON;
    }
    return model.register_file[addr & 0xFFFF];
}

static void model_write_reg(uint32_t addr, uint8_t val)
{
    if (addr == ADDR_MR) {
        handle_mr_write(addr, val);
        return;
    }
    if (addr == ADDR_PHYCFGR) {
        handle_phycfgr_write(addr, val);
        model.register_file[addr & 0xFFFF] = val;
        return;
    }

    {
        uint8_t block = W5500_BLOCK(addr);
        uint8_t sn = socket_from_sreg_block(block);
        if (sn < SOCKET_COUNT && ((addr >> 8) & 0xFF) == Sn_CR_OFF) {
            if (addr == Sn_CR(sn)) {
                handle_cr_write(addr, val);
                return;
            }
        }
    }

    model.register_file[addr & 0xFFFF] = val;
}

uint8_t WIZCHIP_READ(uint32_t addr)
{
    return model_read_reg(addr);
}

void WIZCHIP_WRITE(uint32_t addr, uint8_t wb)
{
    model_write_reg(addr, wb);
}

void WIZCHIP_READ_BUF(uint32_t addr, uint8_t *pBuf, uint16_t len)
{
    uint32_t phys = buffer_phys_addr(addr);
    if (phys != (uint32_t)-1) {
        uint16_t i;
        for (i = 0; i < len; i++) {
            if (model.stuck_miso_count > 0) {
                model.stuck_miso_count--;
                pBuf[i] = 0xFF;
            } else {
                pBuf[i] = model.register_file[(phys + i) & 0xFFFF];
            }
        }
        return;
    }
    {
        uint16_t i;
        for (i = 0; i < len; i++) {
            if (model.stuck_miso_count > 0) {
                model.stuck_miso_count--;
                pBuf[i] = 0xFF;
            } else {
                pBuf[i] = model_read_reg(addr + i);
            }
        }
    }
}

void WIZCHIP_WRITE_BUF(uint32_t addr, uint8_t *pBuf, uint16_t len)
{
    uint32_t phys = buffer_phys_addr(addr);
    if (phys != (uint32_t)-1) {
        uint16_t i;
        for (i = 0; i < len; i++) {
            model.register_file[(phys + i) & 0xFFFF] = pBuf[i];
        }
        return;
    }
    {
        uint16_t i;
        for (i = 0; i < len; i++) {
            model_write_reg(addr + i, pBuf[i]);
        }
    }
}

uint16_t wizchip_read16(uint32_t addr)
{
    uint8_t tmp[2];
    WIZCHIP_READ_BUF(addr, tmp, 2);
    return ((uint16_t)tmp[0] << 8) | tmp[1];
}

void wizchip_write16(uint32_t addr, uint16_t val)
{
    uint8_t tmp[2];
    tmp[0] = (uint8_t)(val >> 8);
    tmp[1] = (uint8_t)(val);
    WIZCHIP_WRITE_BUF(addr, tmp, 2);
}

static uint16_t getSn_TX_WR(uint8_t sn)
{
    return wizchip_read16(Sn_TX_WR(sn));
}

static void setSn_TX_WR(uint8_t sn, uint16_t val)
{
    wizchip_write16(Sn_TX_WR(sn), val);
}

static uint16_t getSn_RX_RD(uint8_t sn)
{
    return wizchip_read16(Sn_RX_RD(sn));
}

static void setSn_RX_RD(uint8_t sn, uint16_t val)
{
    wizchip_write16(Sn_RX_RD(sn), val);
}

static uint16_t getSn_RX_WR(uint8_t sn)
{
    return wizchip_read16(Sn_RX_WR(sn));
}

static uint8_t getSn_SR(uint8_t sn)
{
    return WIZCHIP_READ(Sn_SR(sn));
}

static uint8_t getSn_IR(uint8_t sn)
{
    return WIZCHIP_READ(Sn_IR(sn)) & 0x1F;
}

static uint8_t getSn_MR(uint8_t sn)
{
    return WIZCHIP_READ(Sn_MR(sn));
}

void wiz_send_data(uint8_t sn, uint8_t *wizdata, uint16_t len)
{
    uint16_t ptr;
    uint32_t addrsel;

    if (sn >= SOCKET_COUNT || len == 0 || wizdata == NULL) {
        return;
    }
    ptr = getSn_TX_WR(sn);
    addrsel = ((uint32_t)ptr << 8) + (WIZCHIP_TXBUF_BLOCK(sn) << 3);
    WIZCHIP_WRITE_BUF(addrsel, wizdata, len);
    ptr += len;
    setSn_TX_WR(sn, ptr);
}

void wiz_recv_data(uint8_t sn, uint8_t *wizdata, uint16_t len)
{
    uint16_t ptr;
    uint32_t addrsel;

    if (sn >= SOCKET_COUNT || len == 0 || wizdata == NULL) {
        return;
    }
    ptr = getSn_RX_RD(sn);
    addrsel = ((uint32_t)ptr << 8) + (WIZCHIP_RXBUF_BLOCK(sn) << 3);
    WIZCHIP_READ_BUF(addrsel, wizdata, len);
    ptr += len;
    setSn_RX_RD(sn, ptr);
}

void wiz_recv_ignore(uint8_t sn, uint16_t len)
{
    uint16_t ptr;

    if (sn >= SOCKET_COUNT || len == 0) {
        return;
    }
    ptr = getSn_RX_RD(sn);
    ptr += len;
    setSn_RX_RD(sn, ptr);
}

void model_step(void)
{
    model.tick++;
}

void stuck_miso(uint32_t nextN)
{
    model.stuck_miso_count = nextN;
}

void corrupt_tx_wr(uint8_t sn)
{
    if (sn >= SOCKET_COUNT) return;
    reg_write16(model.register_file, Sn_TX_WR(sn), 0xDEAD);
}

void model_reset(void)
{
    memset(&model, 0, sizeof(model));
    model.stuck_miso_count = 0;
    model.tick = 0;
}

static void model_init_defaults(void)
{
    uint8_t sn;
    for (sn = 0; sn < SOCKET_COUNT; sn++) {
        model.tx_buf_size[sn] = MAX_BUF_PER_SN;
        model.rx_buf_size[sn] = MAX_BUF_PER_SN;
        model.register_file[Sn_RXBUF_SIZE(sn) & 0xFFFF] = 2;
        model.register_file[Sn_TXBUF_SIZE(sn) & 0xFFFF] = 2;
    }
}

static int test_result(const char *name, int cond)
{
    if (cond) {
        printf("  PASS: %s\n", name);
    } else {
        printf("  FAIL: %s\n", name);
    }
    return cond ? 0 : 1;
}

int test_main(void)
{
    int failures = 0;
    uint8_t sn = 0;
    uint8_t tx_data[16];
    uint16_t saved_txwr;

    printf("=== W5500 Model Test Suite ===\n\n");

    model_reset();
    model_init_defaults();

    printf("Test 1: VERSIONR\n");
    failures += test_result("VERSIONR == 0x04",
        WIZCHIP_READ(ADDR_VERSIONR) == 0x04);

    printf("\nTest 2: PHYCFGR link up\n");
    failures += test_result("PHYCFGR link bit set",
        (WIZCHIP_READ(ADDR_PHYCFGR) & PHYCFGR_LNK_ON) != 0);

    printf("\nTest 3: UDP socket open\n");
    WIZCHIP_WRITE(Sn_MR(sn), Sn_MR_UDP);
    WIZCHIP_WRITE(Sn_CR(sn), Sn_CR_OPEN);
    model_step();
    failures += test_result("Sn_SR == SOCK_UDP after OPEN",
        getSn_SR(sn) == SOCK_UDP);
    failures += test_result("Sn_CR cleared after command",
        WIZCHIP_READ(Sn_CR(sn)) == 0x00);

    printf("\nTest 4: TX_WR initialized to 0 after OPEN\n");
    failures += test_result("TX_WR == 0",
        getSn_TX_WR(sn) == 0);

    printf("\nTest 5: UDP send data then SEND advances TX_WR\n");
    memset(tx_data, 0xAB, sizeof(tx_data));
    wiz_send_data(sn, tx_data, 8);
    saved_txwr = getSn_TX_WR(sn);
    failures += test_result("TX_WR advanced by write length (8)",
        saved_txwr == 8);

    WIZCHIP_WRITE(Sn_CR(sn), Sn_CR_SEND);
    model_step();
    {
        uint16_t txwr_after = getSn_TX_WR(sn);
        failures += test_result("TX_WR advanced by 2 after SEND",
            txwr_after == ((saved_txwr + 2) & (MAX_BUF_PER_SN - 1)));
    }
    failures += test_result("SENDOK interrupt set",
        (getSn_IR(sn) & Sn_IR_SENDOK) != 0);

    printf("\nTest 6: Data written to TX buffer is readable from buffer region\n");
    {
        uint8_t verify[8];
        uint32_t phys = TXBUF_BASE + (uint32_t)sn * MAX_BUF_PER_SN;
        memcpy(verify, &model.register_file[phys], 8);
        failures += test_result("TX buffer contains written data",
            memcmp(verify, tx_data, 8) == 0);
    }

    printf("\nTest 7: TCP socket open sets Sn_SR to SOCK_INIT\n");
    WIZCHIP_WRITE(Sn_MR(sn), Sn_MR_TCP);
    WIZCHIP_WRITE(Sn_CR(sn), Sn_CR_OPEN);
    model_step();
    failures += test_result("Sn_SR == SOCK_INIT after TCP OPEN",
        getSn_SR(sn) == SOCK_INIT);

    printf("\nTest 8: CLOSE command sets Sn_SR to SOCK_CLOSED\n");
    WIZCHIP_WRITE(Sn_CR(sn), Sn_CR_CLOSE);
    model_step();
    failures += test_result("Sn_SR == SOCK_CLOSED after CLOSE",
        getSn_SR(sn) == SOCK_CLOSED);
    failures += test_result("Sn_MR cleared after CLOSE",
        (getSn_MR(sn) & Sn_MR_PROTO_MASK) == Sn_MR_CLOSE);

    printf("\nTest 9: Stuck MISO injection\n");
    model_reset();
    model_init_defaults();
    stuck_miso(3);
    {
        uint8_t r1 = WIZCHIP_READ(ADDR_VERSIONR);
        uint8_t r2 = WIZCHIP_READ(ADDR_VERSIONR);
        uint8_t r3 = WIZCHIP_READ(ADDR_VERSIONR);
        uint8_t r4 = WIZCHIP_READ(ADDR_VERSIONR);
        failures += test_result("Stuck reads return 0xFF (read 1)",
            r1 == 0xFF);
        failures += test_result("Stuck reads return 0xFF (read 2)",
            r2 == 0xFF);
        failures += test_result("Stuck reads return 0xFF (read 3)",
            r3 == 0xFF);
        failures += test_result("Recovery after stuck count exhausted",
            r4 == 0x04);
    }

    printf("\nTest 10: Stuck MISO blocks recv data\n");
    model_reset();
    model_init_defaults();
    {
        uint8_t rx_buf[8];
        WIZCHIP_WRITE(Sn_MR(sn), Sn_MR_TCP);
        WIZCHIP_WRITE(Sn_CR(sn), Sn_CR_OPEN);
        model_step();

        {
            uint16_t rxwr = getSn_RX_WR(sn);
            uint32_t phys_rx = RXBUF_BASE + (uint32_t)sn * MAX_BUF_PER_SN;
            uint8_t fake_recv[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
            memcpy(&model.register_file[phys_rx], fake_recv, 8);
            reg_write16(model.register_file, Sn_RX_WR(sn), rxwr + 8);
        }

        stuck_miso(8);
        wiz_recv_data(sn, rx_buf, 4);
        {
            int all_ff = 1;
            uint8_t k;
            for (k = 0; k < 4; k++) {
                if (rx_buf[k] != 0xFF) all_ff = 0;
            }
            failures += test_result("Stuck MISO recv returns 0xFF",
                all_ff);
        }
    }

    printf("\nTest 11: corrupt_tx_wr and verification\n");
    model_reset();
    model_init_defaults();
    WIZCHIP_WRITE(Sn_MR(sn), Sn_MR_UDP);
    WIZCHIP_WRITE(Sn_CR(sn), Sn_CR_OPEN);
    model_step();
    {
        uint16_t orig = getSn_TX_WR(sn);
        failures += test_result("TX_WR initially 0", orig == 0);
        corrupt_tx_wr(sn);
        uint16_t after = getSn_TX_WR(sn);
        failures += test_result("TX_WR corrupted to 0xDEAD", after == 0xDEAD);
    }

    printf("\nTest 12: Ring-buffer pointer semantics\n");
    model_reset();
    model_init_defaults();
    WIZCHIP_WRITE(Sn_MR(sn), Sn_MR_UDP);
    WIZCHIP_WRITE(Sn_CR(sn), Sn_CR_OPEN);
    model_step();
    {
        uint8_t big_data[8] = { 0 };
        uint16_t wrap_pos = (uint16_t)(MAX_BUF_PER_SN - 4);
        setSn_TX_WR(sn, wrap_pos);
        wiz_send_data(sn, big_data, 8);
        {
            uint16_t tw = getSn_TX_WR(sn);
            uint16_t expected = (uint16_t)(wrap_pos + 8);
            failures += test_result("TX_WR increments beyond physical buffer",
                tw == expected);
        }

        {
            uint16_t mask = MAX_BUF_PER_SN - 1;
            uint16_t phys_addr = TXBUF_BASE + (uint32_t)sn * MAX_BUF_PER_SN;
            uint16_t wrapped_off = (wrap_pos + 8) & mask;
            failures += test_result("Buffer phys addr wraps correctly",
                model.register_file[phys_addr + wrapped_off - 1] == 0);
            model.register_file[phys_addr + wrapped_off - 1] = 0xFF;
            uint8_t verify = 0;
            {
                uint32_t addrsel = ((uint32_t)(wrap_pos + 8 - 1) << 8)
                    + (WIZCHIP_TXBUF_BLOCK(sn) << 3);
                WIZCHIP_READ_BUF(addrsel, &verify, 1);
                failures += test_result("Wrapped read matches written byte",
                    verify == 0xFF);
            }
        }
    }

    printf("\nTest 13: MR reset via RST bit\n");
    model_reset();
    model_init_defaults();
    WIZCHIP_WRITE(Sn_MR(sn), Sn_MR_UDP);
    WIZCHIP_WRITE(Sn_CR(sn), Sn_CR_OPEN);
    model_step();
    failures += test_result("Sn_SR == SOCK_UDP before reset",
        getSn_SR(sn) == SOCK_UDP);
    WIZCHIP_WRITE(ADDR_MR, MR_RST);
    {
        uint8_t mr = WIZCHIP_READ(ADDR_MR);
        failures += test_result("RST bit auto-cleared after MR write",
            (mr & MR_RST) == 0);
    }

    printf("\nTest 14: model_step tick counter\n");
    {
        uint32_t before = model.tick;
        model_step();
        model_step();
        model_step();
        failures += test_result("Tick advanced by 3", model.tick == before + 3);
    }

    printf("\n===========================\n");
    if (failures == 0) {
        printf("RESULT: PASS (%d failures)\n", failures);
    } else {
        printf("RESULT: FAIL (%d failures)\n", failures);
    }
    return failures;
}

int main(void)
{
    return test_main();
}
