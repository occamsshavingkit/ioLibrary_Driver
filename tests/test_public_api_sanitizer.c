#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define _WIZCHIP_ 5500
#include "wizchip_conf.h"
#include "w5500.h"
#include "socket.h"

static unsigned int critical_depth;
static unsigned int failures;

static void test_critical_enter(void) { ++critical_depth; }
static void test_critical_exit(void) { if (critical_depth) --critical_depth; }
static void test_select(void) {}
static void test_deselect(void) {}
static uint8_t test_read_byte(void) { return 0; }
static void test_write_byte(uint8_t v) { (void)v; }
static void test_read_burst(uint8_t *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; ++i) buf[i] = 0;
}
static void test_write_burst(uint8_t *buf, uint16_t len) { (void)buf; (void)len; }

#define CHECK(cond, msg) do { \
    if (!(cond)) { ++failures; fprintf(stderr, "FAIL: %s\n", msg); } \
} while(0)

static void init(void)
{
    int8_t mem[8] = {2,2,2,2,2,2,2,2};
    reg_wizchip_cris_cbfunc(test_critical_enter, test_critical_exit);
    reg_wizchip_cs_cbfunc(test_select, test_deselect);
    reg_wizchip_spi_cbfunc(test_read_byte, test_write_byte);
    reg_wizchip_spiburst_cbfunc(test_read_burst, test_write_burst);
    wizchip_init(mem, mem);
}

static void test_register_access(void)
{
    setSn_MR(0, Sn_MR_UDP);
    CHECK(getSn_MR(0) == 0, "getSn_MR(0)");
    setSn_PORT(0, 49002);
    CHECK(getSn_PORT(0) == 0, "getSn_PORT(0)");
    setSn_DIPR(0, (uint8_t*)"\xc0\xa8\x02\x32");
    setSn_DPORT(0, 5000);
    CHECK(getSn_DPORT(0) == 0, "getSn_DPORT(0)");
    setSn_TX_WR(0, 0x0100);
    CHECK(getSn_TX_WR(0) == 0, "getSn_TX_WR(0)");
    setSn_RX_RD(0, 0x0000);
    CHECK(getSn_RX_RD(0) == 0, "getSn_RX_RD(0)");
    WIZCHIP_READ(Sn_CR(0));
    WIZCHIP_READ(Sn_IR(0));
    WIZCHIP_READ(Sn_SR(0));
    getVERSIONR();
    getPHYCFGR();
}

static void test_socket_api(void)
{
    int8_t sn;
    uint8_t buf[64];
    uint8_t ip[4] = {192,168,2,50};
    uint16_t port = 5000;
    int32_t ret;

    sn = socket(0, Sn_MR_UDP, 49002, SF_IO_NONBLOCK);
    CHECK(sn == 0, "socket UDP");

    memset(buf, 0xcc, sizeof(buf));
    buf[0] = 0x42;
    ret = sendto(0, buf, 4, ip, 5000);
    (void)ret;

    ret = recvfrom(0, buf, sizeof(buf), ip, &port);
    (void)ret;

    CHECK(close(0) == 0, "close socket 0");

    sn = socket(1, Sn_MR_TCP, 49003, SF_IO_NONBLOCK);
    CHECK(sn == 1, "socket TCP");
    CHECK(close(1) == 0, "close socket 1");

    sn = socket(2, Sn_MR_MACRAW, 0, SF_IO_NONBLOCK);
    CHECK(sn == 2, "socket MACRAW");
    CHECK(close(2) == 0, "close socket 2");

    sn = socket(3, Sn_MR_IPRAW, 49004, SF_IO_NONBLOCK);
    CHECK(sn == 3, "socket IPRAW");
    CHECK(close(3) == 0, "close socket 3");
}

static void test_socket_bounds(void)
{
    int8_t sn;

    sn = socket(8, Sn_MR_UDP, 49000, 0);
    CHECK(sn == SOCKERR_SOCKNUM, "socket 8 rejected");
    sn = socket(255, Sn_MR_UDP, 49000, 0);
    CHECK(sn == SOCKERR_SOCKNUM, "socket 255 rejected");

    sn = socket(4, 0xFF, 49000, 0);
    CHECK(sn == SOCKERR_SOCKMODE, "socket bad mode rejected");
}

static void test_wizchip_init(void)
{
    int8_t mem_zero[8] = {0};
    int8_t mem_valid[8] = {2,2,2,2,2,2,2,2};
    int8_t ret;

    ret = wizchip_init(mem_zero, mem_valid);
    CHECK(ret != 0, "wizchip_init null tx rejected");

    ret = wizchip_init(mem_valid, mem_zero);
    CHECK(ret != 0, "wizchip_init null rx rejected");

    ret = wizchip_init(mem_valid, mem_valid);
    CHECK(ret == 0, "wizchip_init valid");
}

static void test_keeptimer(void)
{
    setSn_KPALVTR(0, 22);
    WIZCHIP_READ(Sn_KPALVTR(0));

    WIZCHIP_WRITE(Sn_CR(0), Sn_CR_SEND);
    WIZCHIP_WRITE(Sn_CR(0), Sn_CR_RECV);
    WIZCHIP_WRITE(Sn_CR(0), Sn_CR_CLOSE);
}

int main(void)
{
    failures = 0;

    init();
    test_register_access();
    test_socket_api();
    test_socket_bounds();
    test_wizchip_init();
    test_keeptimer();

    if (failures) {
        fprintf(stderr, "\n%u FAILURES\n", failures);
        return 1;
    }
    printf("PASS: all %s checks\n", __FILE__);
    return 0;
}
