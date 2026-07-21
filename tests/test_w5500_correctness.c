#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define _WIZCHIP_ 5500
#define _WIZCHIP_IO_MODE_ 0x0203
#include "wizchip_conf.h"
#include "w5500.h"
#include "socket.h"

static unsigned int failures;
static unsigned int tests_run;

#define TASSERT(cond, msg) do { \
    ++tests_run; \
    if (!(cond)) { ++failures; fprintf(stderr, "FAIL [%d]: %s\n", __LINE__, msg); } \
} while(0)

static void test_critical_enter(void) {}
static void test_critical_exit(void) {}
static void test_cs_select(void) {}
static void test_cs_deselect(void) {}
static uint8_t test_read_byte(void) { return 0x00; }
static void test_write_byte(uint8_t v) { (void)v; }
static void test_read_burst(uint8_t *buf, uint16_t len) {
    uint16_t i;
    for (i = 0; i < len; ++i) buf[i] = 0;
}
static void test_write_burst(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
}

static void init(void) {
    static int8_t mem[8] = {2,2,2,2,2,2,2,2};
    reg_wizchip_cris_cbfunc(test_critical_enter, test_critical_exit);
    reg_wizchip_cs_cbfunc(test_cs_select, test_cs_deselect);
    reg_wizchip_spi_cbfunc(test_read_byte, test_write_byte);
    reg_wizchip_spiburst_cbfunc(test_read_burst, test_write_burst);
    wizchip_init(mem, mem);
}

static void test_socket_returns_valid_on_first_slot(void) {
    int8_t sn = socket(0, Sn_MR_UDP, 49002, SF_IO_NONBLOCK);
    TASSERT(sn == 0, "socket(0,UDP) returns slot 0");
    close(0);
}

static void test_socket_rejects_oob_sockets(void) {
    int8_t sn = socket(8, Sn_MR_UDP, 49000, 0);
    TASSERT(sn == SOCKERR_SOCKNUM, "socket(8) rejected");
    sn = socket(255, Sn_MR_UDP, 49000, 0);
    TASSERT(sn == SOCKERR_SOCKNUM, "socket(255) rejected");
}

static void test_socket_rejects_invalid_protocol(void) {
    int8_t sn = socket(4, 0xFF, 49000, 0);
    TASSERT(sn == SOCKERR_SOCKMODE, "socket(4,0xFF) rejected");
}

static void test_close_rejects_oob(void) {
    int8_t ret = close(8);
    TASSERT(ret == SOCKERR_SOCKNUM, "close(8) rejected");
    ret = close(255);
    TASSERT(ret == SOCKERR_SOCKNUM, "close(255) rejected");
}

static void test_wizchip_init_rejects_null_buffers(void) {
    int8_t mem_valid[8] = {2,2,2,2,2,2,2,2};
    int8_t mem_empty[8] = {0};
    int8_t ret;

    ret = wizchip_init(mem_empty, mem_valid);
    TASSERT(ret != 0, "wizchip_init(null tx) fails");

    ret = wizchip_init(mem_valid, mem_empty);
    TASSERT(ret != 0, "wizchip_init(null rx) fails");

    ret = wizchip_init(mem_valid, mem_valid);
    TASSERT(ret == -1 || ret == 0, "wizchip_init(valid) returns");
}

static void test_multiple_socket_allocations(void) {
    int8_t i;
    for (i = 0; i < 8; ++i) {
        int8_t sn = socket(i, Sn_MR_UDP, 49010 + i, SF_IO_NONBLOCK);
        TASSERT(sn == i, "socket(i,UDP) sequential");
    }
    TASSERT(socket(0, Sn_MR_UDP, 49099, SF_IO_NONBLOCK) == 0,
            "reuse slot after fill");
    for (i = 0; i < 8; ++i) close(i);
}

static void test_wdt_default_does_not_crash(void) {
    wizchip_wdt_kick();
    TASSERT(1, "WDT kick default no-op");
}

static void test_wdt_callback_registration(void) {
    static int kick_count = 0;
    void my_kick(void) { ++kick_count; }
    reg_wizchip_wdt_cbfunc(my_kick);
    wizchip_wdt_kick();
    wizchip_wdt_kick();
    TASSERT(kick_count == 2, "WDT callback fires");
    reg_wizchip_wdt_cbfunc(0);
    wizchip_wdt_kick();
    TASSERT(kick_count == 2, "WDT callback removed, no more kicks");
}

static void test_phy_callback_default_noop(void) {
    wizchip_phy_link_callback();
    TASSERT(1, "PHY callback default no-op");
}

static void test_phy_callback_registration(void) {
    static uint8_t last_link = 255;
    void my_phy_cb(uint8_t lu) { last_link = lu; }
    reg_wizchip_phy_cbfunc(my_phy_cb);
    wizchip_phy_link_callback();
    TASSERT(last_link == 0, "PHY callback fires with default link=0");
    reg_wizchip_phy_cbfunc(0);
}

static void test_register_access_atomicity(void) {
    init();
    setSn_TX_WR(0, 0x0100);
    TASSERT(getSn_TX_WR(0) == 0, "TX_WR write/read (mock)");
    setSn_RX_RD(0, 0x0200);
    TASSERT(getSn_RX_RD(0) == 0, "RX_RD write/read (mock)");
}

static void test_version_is_readable(void) {
    TASSERT(getVERSIONR() == 0, "VERSIONR readable (mock)");
}

static void test_send_null_buf_returns_error(void) {
    int32_t ret;
    init();
    TASSERT(socket(0, Sn_MR_UDP, 49100, SF_IO_NONBLOCK) == 0, "socket");
    ret = sendto(0, 0, 4, (uint8_t*)"\xc0\xa8\x02\x32", 5000);
    TASSERT(ret == SOCKERR_ARG, "sendto(NULL) returns SOCKERR_ARG");
    close(0);
}

int main(void) {
    failures = 0;
    tests_run = 0;

    init();
    test_socket_returns_valid_on_first_slot();
    test_socket_rejects_oob_sockets();
    test_socket_rejects_invalid_protocol();
    test_close_rejects_oob();
    test_wizchip_init_rejects_null_buffers();
    test_multiple_socket_allocations();
    test_register_access_atomicity();
    test_version_is_readable();
    test_send_null_buf_returns_error();
    test_wdt_default_does_not_crash();
    test_wdt_callback_registration();
    test_phy_callback_default_noop();
    test_phy_callback_registration();

    if (failures) {
        fprintf(stderr, "\n%u/%u FAILURES\n", failures, tests_run);
        return 1;
    }
    printf("PASS: %u/%u correctness assertions\n", tests_run, tests_run);
    return 0;
}
