#include "w5500_diag_status_contract.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

_WIZCHIP WIZCHIP;

static uint8_t read_byte(void)
{
    return 0u;
}

static void write_byte(uint8_t value)
{
    (void)value;
}

static void read_burst(uint8_t *buffer, uint16_t length)
{
    (void)buffer;
    (void)length;
}

static void write_burst(uint8_t *buffer, uint16_t length)
{
    (void)buffer;
    (void)length;
}

static void install_transport(void)
{
    WIZCHIP.IF.SPI._read_byte = read_byte;
    WIZCHIP.IF.SPI._write_byte = write_byte;
    WIZCHIP.IF.SPI._read_burst = read_burst;
    WIZCHIP.IF.SPI._write_burst = write_burst;
}

static void register_status(uint8_t (*check_busy)(void),
                            uint8_t (*get_error)(void))
{
    WIZCHIP.IF.SPI_STATUS._check_busy = check_busy;
    WIZCHIP.IF.SPI_STATUS._get_error = get_error;
}

static bool transport_is_installed(void)
{
    return WIZCHIP.IF.SPI._read_byte == read_byte &&
           WIZCHIP.IF.SPI._write_byte == write_byte &&
           WIZCHIP.IF.SPI._read_burst == read_burst &&
           WIZCHIP.IF.SPI._write_burst == write_burst;
}

static uint8_t status_ok(void)
{
    return 0u;
}

int main(void)
{
    w5500_diag_board_status_t result;
    bool layout_is_aliased;

    install_transport();
    assert(w5500_diag_status_contract_classify(true, NULL) ==
           W5500_DIAG_STATUS_NO_API);
    assert(transport_is_installed());

    assert(w5500_diag_status_contract_classify(false, NULL) ==
           W5500_DIAG_STATUS_NOT_CLAIMED);
    assert(transport_is_installed());

    register_status(status_ok, status_ok);
    layout_is_aliased = !transport_is_installed();
    install_transport();

    result = w5500_diag_status_contract_classify(true, register_status);
    assert(result == (layout_is_aliased ? W5500_DIAG_STATUS_ALIAS
                                        : W5500_DIAG_STATUS_OK));
    assert(transport_is_installed());

    return 0;
}
