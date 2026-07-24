#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define _WIZCHIP_ 5500
#define _WIZCHIP_IO_MODE_ 0x0203
#include "wizchip_conf.h"
#include "w5500.h"
#include "socket.h"
#include "support/w5500_spi_model.h"

static unsigned int failures;
static unsigned int tests_run;
static int transaction_lock_log[18];
static unsigned int transaction_lock_log_count;
static w5500_model_t model;

#define TASSERT(cond, msg) do { \
    ++tests_run; \
    if (!(cond)) { ++failures; fprintf(stderr, "FAIL [%d]: %s\n", __LINE__, msg); } \
} while(0)

static void test_critical_enter(void) {}
static void test_critical_exit(void) {}
static void test_cs_select(void) { model_cs_select(&model); }
static void test_cs_deselect(void) { model_cs_deselect(&model); }
static uint8_t test_read_byte(void) { return model_spi_read_byte(&model); }
static void test_write_byte(uint8_t v) { model_spi_write_byte(&model, v); }
static void test_read_burst(uint8_t *buf, uint16_t len) {
    model_spi_read_burst(&model, buf, len);
}
static void test_write_burst(uint8_t *buf, uint16_t len) {
    model_spi_write_burst(&model, buf, len);
}

static void transaction_sock_enter(uint8_t sn)
{
    transaction_lock_log[transaction_lock_log_count++] = sn;
}

static void transaction_sock_exit(uint8_t sn)
{
    transaction_lock_log[transaction_lock_log_count++] = 10 + sn;
}

static void transaction_global_enter(void)
{
    transaction_lock_log[transaction_lock_log_count++] = 100;
}

static void transaction_global_exit(void)
{
    transaction_lock_log[transaction_lock_log_count++] = 101;
}

static void init(void)
{
    uint8_t mem[8] = {2,2,2,2,2,2,2,2};
    uint8_t ip[4] = {192, 0, 2, 1};

    model_init(&model);
    reg_wizchip_cris_cbfunc(test_critical_enter, test_critical_exit);
    reg_wizchip_cs_cbfunc(test_cs_select, test_cs_deselect);
    reg_wizchip_spi_cbfunc(test_read_byte, test_write_byte);
    reg_wizchip_spiburst_cbfunc(test_read_burst, test_write_burst);
    wizchip_init(mem, mem);
    setSIPR(ip);
}

static int  kick_count;
static void file_scope_wdt_kick(void) { ++kick_count; }

static uint8_t last_link;
static void file_scope_phy_cb(uint8_t lu) { last_link = lu; }

static void test_socket_returns_valid_on_first_slot(void)
{
    int8_t sn = socket(0, Sn_MR_UDP, 49002, SF_IO_NONBLOCK);
    TASSERT(sn == 0, "socket(0,UDP) returns slot 0");
    close(0);
}

static void test_socket_rejects_oob_sockets(void)
{
    int8_t sn = socket(8, Sn_MR_UDP, 49000, 0);
    TASSERT(sn == SOCKERR_SOCKNUM, "socket(8) rejected");
    sn = socket(255, Sn_MR_UDP, 49000, 0);
    TASSERT(sn == SOCKERR_SOCKNUM, "socket(255) rejected");
}

static void test_socket_rejects_invalid_protocol(void)
{
    int8_t sn = socket(4, 0xFF, 49000, 0);
    TASSERT(sn == SOCKERR_SOCKMODE, "socket(4,0xFF) rejected");
}

static void test_close_rejects_oob(void)
{
    int8_t ret = close(8);
    TASSERT(ret == SOCKERR_SOCKNUM, "close(8) rejected");
    ret = close(255);
    TASSERT(ret == SOCKERR_SOCKNUM, "close(255) rejected");
}

static void test_wizchip_init_rejects_null_buffers(void)
{
    uint8_t mem_invalid[8] = {1,0,0,0,0,0,0,0};
    uint8_t mem_valid[8] = {2,2,2,2,2,2,2,2};
    int8_t ret;

    ret = wizchip_init(mem_invalid, mem_valid);
    TASSERT(ret != 0, "wizchip_init(invalid tx size) fails");

    ret = wizchip_init(mem_valid, mem_invalid);
    TASSERT(ret != 0, "wizchip_init(invalid rx size) fails");

    ret = wizchip_init(mem_valid, mem_valid);
    TASSERT(ret == -1 || ret == 0, "wizchip_init(valid) returns");
}

static void test_reset_is_all_socket_transaction_on_failure(void)
{
    static const int expected[] = {
        100, 0, 1, 2, 3, 4, 5, 6, 7,
        17, 16, 15, 14, 13, 12, 11, 10, 101
    };
    unsigned int i;

    transaction_lock_log_count = 0;
    reg_wizchip_lock_cbfunc(transaction_sock_enter, transaction_sock_exit,
                            transaction_global_enter,
                            transaction_global_exit);

    TASSERT(wizchip_sw_reset() == 0,
            "model VERSIONR makes reset verification succeed");
    TASSERT(transaction_lock_log_count ==
                sizeof(expected) / sizeof(expected[0]),
            "reset balances the global and all socket locks");
    for (i = 0; i < transaction_lock_log_count &&
                i < sizeof(expected) / sizeof(expected[0]); ++i) {
        TASSERT(transaction_lock_log[i] == expected[i],
                "reset uses global, ascending socket, descending release order");
    }
    TASSERT(wizchip_get_state() == WIZCHIP_STATE_READY,
            "verified reset publishes READY state");

    reg_wizchip_lock_cbfunc(0, 0, 0, 0);
}

static void test_multiple_socket_allocations(void)
{
    int8_t i;
    for (i = 0; i < 8; ++i) {
        int8_t sn = socket(i, Sn_MR_UDP, 49010 + i, SF_IO_NONBLOCK);
        TASSERT(sn == i, "socket(i,UDP) sequential");
    }
    TASSERT(socket(0, Sn_MR_UDP, 49099, SF_IO_NONBLOCK) == 0,
            "reuse slot after fill");
    for (i = 0; i < 8; ++i) close(i);
}

static void test_wdt_default_does_not_crash(void)
{
    wizchip_wdt_kick();
    TASSERT(1, "WDT kick default no-op");
}

static void test_wdt_callback_registration(void)
{
    kick_count = 0;
    reg_wizchip_wdt_cbfunc(file_scope_wdt_kick);
    wizchip_wdt_kick();
    wizchip_wdt_kick();
    TASSERT(kick_count == 2, "WDT callback fires");
    reg_wizchip_wdt_cbfunc(0);
    wizchip_wdt_kick();
    TASSERT(kick_count == 2, "WDT callback removed, no more kicks");
}

static void test_phy_callback_default_noop(void)
{
    wizchip_phy_link_callback(PHY_LINK_OFF);
    TASSERT(1, "PHY callback default no-op");
}

static void test_phy_callback_registration(void)
{
    last_link = 255;
    reg_wizchip_phy_cbfunc(file_scope_phy_cb);
    wizchip_phy_link_callback(PHY_LINK_ON);
    TASSERT(last_link == PHY_LINK_ON, "PHY callback forwards link state");
    reg_wizchip_phy_cbfunc(0);
}

static void test_register_access_atomicity(void)
{
    init();
    setSn_TX_WR(0, 0x0100);
    TASSERT(getSn_TX_WR(0) == 0x0100, "TX_WR write/read persists");
    setSn_RX_RD(0, 0x0200);
    TASSERT(getSn_RX_RD(0) == 0x0200, "RX_RD write/read persists");
}

static void test_version_is_readable(void)
{
    TASSERT(getVERSIONR() == 0x04, "VERSIONR readable from model");
}

static void test_send_null_buf_returns_error(void)
{
    int32_t ret;
    init();
    TASSERT(socket(0, Sn_MR_UDP, 49100, SF_IO_NONBLOCK) == 0, "socket");
    ret = sendto(0, 0, 4, (uint8_t*)"\xc0\xa8\x02\x32", 5000);
    TASSERT(ret == SOCKERR_ARG, "sendto(NULL) returns SOCKERR_ARG");
    close(0);
}

int main(void)
{
    failures = 0;
    tests_run = 0;

    init();
    test_socket_returns_valid_on_first_slot();
    test_socket_rejects_oob_sockets();
    test_socket_rejects_invalid_protocol();
    test_close_rejects_oob();
    test_wizchip_init_rejects_null_buffers();
    test_reset_is_all_socket_transaction_on_failure();
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
