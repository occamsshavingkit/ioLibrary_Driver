#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define _WIZCHIP_ 5500
#include "wizchip_conf.h"
#include "w5500.h"
#include "socket.h"
#include "support/w5500_spi_model.h"

static unsigned int critical_depth;
static unsigned int failures;
static w5500_model_t model;

static unsigned int transaction_selects;
static unsigned int transaction_deselects;
static unsigned int transaction_byte_reads;
static unsigned int transaction_byte_writes;
static unsigned int transaction_burst_reads;
static unsigned int transaction_burst_writes;
static unsigned int transaction_status_clears;
static unsigned int transaction_busy_checks;
static unsigned int transaction_error_checks;
static uint8_t transaction_busy;
static int8_t transaction_error;
static uint8_t transaction_bytes[16];
static size_t transaction_bytes_len;

static void test_critical_enter(void) { ++critical_depth; }
static void test_critical_exit(void) { if (critical_depth) --critical_depth; }
static void test_select(void) { model_cs_select(&model); }
static void test_deselect(void) { model_cs_deselect(&model); }
static uint8_t test_read_byte(void) { return model_spi_read_byte(&model); }
static void test_write_byte(uint8_t v) { model_spi_write_byte(&model, v); }
static void test_read_burst(uint8_t *buf, uint16_t len)
{
    model_spi_read_burst(&model, buf, len);
}
static void test_write_burst(uint8_t *buf, uint16_t len)
{
    model_spi_write_burst(&model, buf, len);
}
static uint8_t test_spi_busy(void) { return 0; }
static int8_t test_spi_error(void) { return 0; }
static void test_spi_clear_error(void) {}

static void transaction_select(void) { ++transaction_selects; }
static void transaction_deselect(void) { ++transaction_deselects; }
static uint8_t transaction_read_byte(void)
{
    ++transaction_byte_reads;
    return 0;
}
static void transaction_write_byte(uint8_t value)
{
    ++transaction_byte_writes;
    if (transaction_bytes_len < sizeof(transaction_bytes)) {
        transaction_bytes[transaction_bytes_len++] = value;
    }
}
static void transaction_read_burst(uint8_t *buffer, uint16_t length)
{
    ++transaction_burst_reads;
    memset(buffer, 0, length);
}
static void transaction_write_burst(uint8_t *buffer, uint16_t length)
{
    uint16_t i;

    ++transaction_burst_writes;
    for (i = 0; i < length && transaction_bytes_len < sizeof(transaction_bytes);
         ++i) {
        transaction_bytes[transaction_bytes_len++] = buffer[i];
    }
}
static uint8_t transaction_check_busy(void)
{
    ++transaction_busy_checks;
    return transaction_busy;
}
static int8_t transaction_get_error(void)
{
    ++transaction_error_checks;
    return transaction_error;
}
static void transaction_clear_error(void) { ++transaction_status_clears; }

#define CHECK(cond, msg) do { \
    if (!(cond)) { ++failures; fprintf(stderr, "FAIL: %s\n", msg); } \
} while(0)

static void init(void)
{
    uint8_t mem[8] = {2,2,2,2,2,2,2,2};
    uint8_t ip[4] = {192, 0, 2, 1};

    model_init(&model);
    reg_wizchip_cris_cbfunc(test_critical_enter, test_critical_exit);
    reg_wizchip_cs_cbfunc(test_select, test_deselect);
    reg_wizchip_spi_cbfunc(test_read_byte, test_write_byte);
    reg_wizchip_spiburst_cbfunc(test_read_burst, test_write_burst);
    wizchip_init(mem, mem);
    setSIPR(ip);
}

static void reset_transaction_trace(void)
{
    transaction_selects = 0;
    transaction_deselects = 0;
    transaction_byte_reads = 0;
    transaction_byte_writes = 0;
    transaction_burst_reads = 0;
    transaction_burst_writes = 0;
    transaction_status_clears = 0;
    transaction_busy_checks = 0;
    transaction_error_checks = 0;
    transaction_busy = 0;
    transaction_error = 0;
    transaction_bytes_len = 0;
    memset(transaction_bytes, 0, sizeof(transaction_bytes));
}

static void install_transaction_trace(void)
{
    reg_wizchip_cs_cbfunc(transaction_select, transaction_deselect);
    reg_wizchip_spi_cbfunc(transaction_read_byte, transaction_write_byte);
    reg_wizchip_spiburst_cbfunc(transaction_read_burst,
                               transaction_write_burst);
    reg_wizchip_spistatus_cbfunc(transaction_check_busy,
                                 transaction_get_error,
                                 transaction_clear_error);
    reset_transaction_trace();
}

static void test_register_access(void)
{
    setSn_MR(0, Sn_MR_UDP);
    CHECK(getSn_MR(0) == Sn_MR_UDP, "getSn_MR(0)");
    setSn_PORT(0, 49002);
    CHECK(getSn_PORT(0) == 49002, "getSn_PORT(0)");
    setSn_DIPR(0, (uint8_t*)"\xc0\xa8\x02\x32");
    setSn_DPORT(0, 5000);
    CHECK(getSn_DPORT(0) == 5000, "getSn_DPORT(0)");
    setSn_TX_WR(0, 0x0100);
    CHECK(getSn_TX_WR(0) == 0x0100, "getSn_TX_WR(0)");
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

    CHECK(close(0) == SOCK_OK, "close socket 0");

    sn = socket(1, Sn_MR_TCP, 49003, SF_IO_NONBLOCK);
    CHECK(sn == 1, "socket TCP");
    CHECK(close(1) == SOCK_OK, "close socket 1");

    sn = socket(0, Sn_MR_MACRAW, 0, SF_IO_NONBLOCK);
    CHECK(sn == 0, "socket MACRAW");
    CHECK(close(0) == SOCK_OK, "close socket 0 MACRAW");

    sn = socket(3, Sn_MR_IPRAW, 49004, SF_IO_NONBLOCK);
    CHECK(sn == 3, "socket IPRAW");
    CHECK(close(3) == SOCK_OK, "close socket 3");
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
    uint8_t mem_invalid[8] = {1,0,0,0,0,0,0,0};
    uint8_t mem_valid[8] = {2,2,2,2,2,2,2,2};
    int8_t ret;

    ret = wizchip_init(mem_invalid, mem_valid);
    CHECK(ret != 0, "wizchip_init invalid tx rejected");

    ret = wizchip_init(mem_valid, mem_invalid);
    CHECK(ret != 0, "wizchip_init invalid rx rejected");

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

static void test_spi_status_registration_preserves_data_callbacks(void)
{
    reg_wizchip_spi_cbfunc(test_read_byte, test_write_byte);
    reg_wizchip_spiburst_cbfunc(test_read_burst, test_write_burst);
    reg_wizchip_spistatus_cbfunc(test_spi_busy, test_spi_error,
                                 test_spi_clear_error);

    CHECK(WIZCHIP.SPI_STATUS._check_busy == test_spi_busy,
          "SPI busy callback is registered");
    CHECK(WIZCHIP.SPI_STATUS._get_error == test_spi_error,
          "SPI error callback is registered");
    CHECK(WIZCHIP.SPI_STATUS._clear_error == test_spi_clear_error,
          "SPI clear-error callback is registered");
    CHECK(WIZCHIP.IF.SPI._read_byte == test_read_byte,
          "SPI status registration preserves byte read callback");
    CHECK(WIZCHIP.IF.SPI._write_byte == test_write_byte,
          "SPI status registration preserves byte write callback");
    CHECK(WIZCHIP.IF.SPI._read_burst == test_read_burst,
          "SPI status registration preserves burst read callback");
    CHECK(WIZCHIP.IF.SPI._write_burst == test_write_burst,
          "SPI status registration preserves burst write callback");
}

static void test_spi_status_storage_is_independent(void)
{
    uintptr_t spi_begin = (uintptr_t)&WIZCHIP.IF.SPI;
    uintptr_t spi_end = spi_begin + sizeof(WIZCHIP.IF.SPI);
    uintptr_t status_begin = (uintptr_t)&WIZCHIP.SPI_STATUS;
    uintptr_t status_end = status_begin + sizeof(WIZCHIP.SPI_STATUS);

    CHECK(spi_end <= status_begin || status_end <= spi_begin,
          "SPI status storage does not overlap SPI callback storage");
    CHECK(sizeof(WIZCHIP.SPI_STATUS) >=
              sizeof(WIZCHIP.SPI_STATUS._check_busy) +
              sizeof(WIZCHIP.SPI_STATUS._get_error) +
              sizeof(WIZCHIP.SPI_STATUS._clear_error),
           "SPI status storage independently contains all three callbacks");
}

static void test_spi_status_offsets_are_independent(void)
{
    size_t spi_begin = offsetof(_WIZCHIP, IF) +
                       offsetof(union _IF, SPI);
    size_t spi_end = spi_begin + sizeof(WIZCHIP.IF.SPI);
    size_t status_begin = offsetof(_WIZCHIP, SPI_STATUS);
    size_t status_end = status_begin + sizeof(WIZCHIP.SPI_STATUS);

    CHECK(spi_end <= status_begin || status_end <= spi_begin,
          "SPI status field offset does not overlap SPI callback offsets");
    CHECK(sizeof(WIZCHIP.IF.SPI) >=
              sizeof(WIZCHIP.IF.SPI._read_byte) +
              sizeof(WIZCHIP.IF.SPI._write_byte) +
              sizeof(WIZCHIP.IF.SPI._read_burst) +
              sizeof(WIZCHIP.IF.SPI._write_burst),
          "SPI callback field independently contains all data callbacks");
}

static void test_wizchip_read8_checked_rejects_null_value(void)
{
    install_transaction_trace();

    CHECK(wizchip_read8_checked(GAR, NULL) == SOCKERR_ARG,
          "checked byte read rejects a null result pointer");
    CHECK(transaction_selects == 0 && transaction_deselects == 0,
          "rejected checked byte read does not toggle CS");
}

static void test_wizchip_write8_checked_exists(void)
{
    install_transaction_trace();

    CHECK(wizchip_write8_checked(GAR, 0x5a) == 0,
          "checked byte write reports success");
    CHECK(transaction_selects == 1 && transaction_deselects == 1,
          "checked byte write balances CS");
}

static void test_wizchip_read_buf_checked_rejects_null_nonzero_buffer(void)
{
    install_transaction_trace();

    CHECK(wizchip_read_buf_checked(GAR, NULL, 1) == SOCKERR_ARG,
          "checked buffer read rejects null with nonzero length");
    CHECK(transaction_selects == 0 && transaction_deselects == 0,
          "rejected checked buffer read does not toggle CS");
}

static void test_wizchip_write_buf_checked_rejects_null_nonzero_buffer(void)
{
    install_transaction_trace();

    CHECK(wizchip_write_buf_checked(GAR, NULL, 1) == SOCKERR_ARG,
          "checked buffer write rejects null with nonzero length");
    CHECK(transaction_selects == 0 && transaction_deselects == 0,
          "rejected checked buffer write does not toggle CS");
}

static void test_checked_zero_length_transfers_are_noops(void)
{
    install_transaction_trace();

    CHECK(wizchip_read_buf_checked(GAR, NULL, 0) == 0,
          "zero-length checked read accepts a null buffer");
    CHECK(wizchip_write_buf_checked(GAR, NULL, 0) == 0,
          "zero-length checked write accepts a null buffer");
    CHECK(transaction_selects == 0 && transaction_deselects == 0,
          "zero-length checked transfers do not toggle CS");
    CHECK(transaction_byte_reads == 0 && transaction_byte_writes == 0 &&
              transaction_burst_reads == 0 && transaction_burst_writes == 0,
          "zero-length checked transfers do not invoke data callbacks");
    CHECK(transaction_status_clears == 0 && transaction_busy_checks == 0 &&
              transaction_error_checks == 0,
          "zero-length checked transfers do not invoke status callbacks");
}

static void test_checked_status_failures_are_sticky_and_balance_cs(void)
{
    install_transaction_trace();
    transaction_error = -1;

    CHECK(wizchip_write8_checked(GAR, 0x5a) == SOCKERR_IO,
          "checked write propagates SPI error status");
    CHECK(wizchip_get_last_error() == SOCKERR_IO,
          "SPI error remains latched at the root");
    CHECK(wizchip_get_state() == WIZCHIP_STATE_FAULTED,
          "SPI error faults the root");
    CHECK(transaction_selects == 1 && transaction_deselects == 1,
          "failed checked write balances CS");

    transaction_error = 0;
    CHECK(wizchip_get_last_error() == SOCKERR_IO,
          "later clean transport status does not erase the latched error");
    CHECK(wizchip_recover() == 0, "root recovers after SPI error");
    wizchip_clear_last_error();

    install_transaction_trace();
    transaction_busy = 1;
    CHECK(wizchip_write8_checked(GAR, 0x5a) == SOCKERR_IO,
          "checked write propagates SPI busy status");
    CHECK(wizchip_get_last_error() == SOCKERR_IO,
          "SPI busy failure remains latched at the root");
    CHECK(wizchip_get_state() == WIZCHIP_STATE_FAULTED,
          "SPI busy failure faults the root");
    CHECK(transaction_selects == 1 && transaction_deselects == 1,
          "busy checked write balances CS");

    CHECK(wizchip_recover() == 0, "root recovers after SPI busy");
    wizchip_clear_last_error();
}

static void test_checked_buffer_write_uses_one_vdm_frame(void)
{
    static const uint8_t payload[2] = {0x12, 0x34};
    uint32_t address = Sn_TX_WR(0);

    install_transaction_trace();

    CHECK(wizchip_write_buf_checked(address, payload, sizeof(payload)) ==
               0,
          "checked buffer write reports success");
    CHECK(transaction_selects == 1 && transaction_deselects == 1,
          "checked buffer write uses one CS-delimited frame");
    CHECK(transaction_bytes_len == 5,
          "one VDM frame contains one header and the complete payload");
    CHECK(transaction_bytes[0] == (uint8_t)(address >> 16) &&
              transaction_bytes[1] == (uint8_t)(address >> 8) &&
              transaction_bytes[2] == (uint8_t)(address | _W5500_SPI_WRITE_) &&
              transaction_bytes[3] == payload[0] &&
              transaction_bytes[4] == payload[1],
          "VDM frame preserves address, write control, and payload order");
}

static void test_root_error_contract(void)
{
    CHECK(SOCKERR_IO == -3, "SOCKERR_IO is -3");
    CHECK(SOCKERR_NOTREADY == -4, "SOCKERR_NOTREADY is -4");
    CHECK(SOCKERR_IO < 0, "SOCKERR_IO is negative");
    CHECK(SOCKERR_NOTREADY < 0, "SOCKERR_NOTREADY is negative");
    CHECK(SOCKERR_IO != SOCKERR_NOTREADY, "root error codes are distinct");
}

static void test_root_lifecycle_before_init(void)
{
    wizchip_state_t state = wizchip_get_state();
    int8_t ret;

    CHECK(state >= WIZCHIP_STATE_UNINITIALIZED &&
          state <= WIZCHIP_STATE_FAULTED,
          "wizchip_get_state returns a valid state");
    CHECK(state == WIZCHIP_STATE_UNINITIALIZED,
          "root starts uninitialized");

    ret = socket(0, Sn_MR_UDP, 49002, 0);
    CHECK(ret == SOCKERR_NOTREADY,
          "socket rejects a root that is not ready");
    CHECK(wizchip_get_last_error() == SOCKERR_NOTREADY,
          "not-ready rejection is observable");

    wizchip_clear_last_error();
    CHECK(wizchip_get_last_error() == 0,
          "clear removes the recorded error");
    CHECK(wizchip_get_state() == WIZCHIP_STATE_UNINITIALIZED,
          "clear does not change root lifecycle state");
}

static void test_root_lifecycle_after_init(void)
{
    CHECK(wizchip_get_state() == WIZCHIP_STATE_READY,
          "successful init transitions root to ready");

    wizchip_clear_last_error();
    CHECK(wizchip_get_last_error() == 0,
          "ready root has no recorded error after clear");
    CHECK(wizchip_get_state() == WIZCHIP_STATE_READY,
          "clear keeps a ready root ready");

    CHECK(wizchip_recover() == 0,
          "recover verifies and restores the root");
    CHECK(wizchip_get_state() == WIZCHIP_STATE_READY,
          "verified recover leaves the root ready");
    CHECK(wizchip_get_last_error() == 0,
          "verified recover clears the recorded error");
}

static void test_socket_ok_contract(void)
{
    int8_t sn;
    sn = socket(0, Sn_MR_UDP, 50000, 0);
    CHECK(sn == 0, "socket(0) returns socket number for UDP");
    close(0);

    sn = socket(5, Sn_MR_TCP, 50001, SF_IO_NONBLOCK);
    CHECK(sn == 5, "socket(5) returns SOCK_OK for TCP");
    close(5);
}

static void test_lock_release_after_operations(void)
{
    int8_t sn;
    critical_depth = 0;
    sn = socket(0, Sn_MR_UDP, 50002, SF_IO_NONBLOCK);
    CHECK(sn == 0, "socket");
    close(0);
    CHECK(critical_depth == 0, "lock fully released after close");
}

int main(void)
{
    failures = 0;

    test_root_error_contract();
    test_root_lifecycle_before_init();
    init();
    test_root_lifecycle_after_init();
    test_register_access();
    test_socket_api();
    test_socket_bounds();
    test_wizchip_init();
    test_keeptimer();
    test_spi_status_registration_preserves_data_callbacks();
    test_spi_status_storage_is_independent();
    test_spi_status_offsets_are_independent();
    test_socket_ok_contract();
    test_lock_release_after_operations();
    test_wizchip_read8_checked_rejects_null_value();
    test_wizchip_write8_checked_exists();
    test_wizchip_read_buf_checked_rejects_null_nonzero_buffer();
    test_wizchip_write_buf_checked_rejects_null_nonzero_buffer();
    test_checked_zero_length_transfers_are_noops();
    test_checked_status_failures_are_sticky_and_balance_cs();
    test_checked_buffer_write_uses_one_vdm_frame();

    if (failures) {
        fprintf(stderr, "\n%u FAILURES\n", failures);
        return 1;
    }
    printf("PASS: all %s checks\n", __FILE__);
    return 0;
}
