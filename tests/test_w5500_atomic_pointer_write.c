#include <stdint.h>
#include <stdio.h>

#include "wizchip_conf.h"
#include "w5500.h"

static unsigned int critical_depth;
static unsigned int maximum_critical_depth;

static void test_critical_enter(void)
{
    ++critical_depth;
    if (critical_depth > maximum_critical_depth) {
        maximum_critical_depth = critical_depth;
    }
}

static void test_critical_exit(void)
{
    if (critical_depth > 0) {
        --critical_depth;
    }
}

static void test_select(void) {}
static void test_deselect(void) {}
static uint8_t test_read_byte(void) { return 0; }
static void test_write_byte(uint8_t value) { (void)value; }
static void test_read_burst(uint8_t *buffer, uint16_t length)
{
    uint16_t index;

    for (index = 0; index < length; ++index) {
        buffer[index] = 0;
    }
}

static void test_write_burst(uint8_t *buffer, uint16_t length)
{
    (void)buffer;
    (void)length;
}

static int verify_nonrecursive_setter(const char *name, void (*setter)(void))
{
    critical_depth = 0;
    maximum_critical_depth = 0;
    setter();

    if (critical_depth != 0 || maximum_critical_depth != 1) {
        fprintf(stderr,
                "%s recursively entered CRIS: final depth=%u, max depth=%u\n",
                name, critical_depth, maximum_critical_depth);
        return 1;
    }

    return 0;
}

static void set_tx_write_pointer(void)
{
    setSn_TX_WR(0, 0x1234);
}

static void set_rx_read_pointer(void)
{
    setSn_RX_RD(0, 0x5678);
}

int main(void)
{
    reg_wizchip_cris_cbfunc(test_critical_enter, test_critical_exit);
    reg_wizchip_cs_cbfunc(test_select, test_deselect);
    reg_wizchip_spi_cbfunc(test_read_byte, test_write_byte);
    reg_wizchip_spiburst_cbfunc(test_read_burst, test_write_burst);

    if (verify_nonrecursive_setter("setSn_TX_WR", set_tx_write_pointer) != 0) {
        return 1;
    }
    if (verify_nonrecursive_setter("setSn_RX_RD", set_rx_read_pointer) != 0) {
        return 1;
    }

    return 0;
}
