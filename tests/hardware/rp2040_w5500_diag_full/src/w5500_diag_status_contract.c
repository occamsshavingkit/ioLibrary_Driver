#include "w5500_diag_status_contract.h"

#include <stddef.h>

static uint8_t status_ok(void)
{
    return 0u;
}

w5500_diag_board_status_t w5500_diag_status_contract_classify(
    bool expected, w5500_diag_status_registrar_t registrar)
{
    uint8_t (*saved_read_byte)(void) = WIZCHIP.IF.SPI._read_byte;
    void (*saved_write_byte)(uint8_t) = WIZCHIP.IF.SPI._write_byte;
    void (*saved_read_burst)(uint8_t *, uint16_t) =
        WIZCHIP.IF.SPI._read_burst;
    void (*saved_write_burst)(uint8_t *, uint16_t) =
        WIZCHIP.IF.SPI._write_burst;
    bool transport_changed;
    bool status_installed;

    if (registrar == NULL) {
        return expected ? W5500_DIAG_STATUS_NO_API
                        : W5500_DIAG_STATUS_NOT_CLAIMED;
    }

    registrar(status_ok, status_ok);
    transport_changed = WIZCHIP.IF.SPI._read_byte != saved_read_byte ||
                        WIZCHIP.IF.SPI._write_byte != saved_write_byte ||
                        WIZCHIP.IF.SPI._read_burst != saved_read_burst ||
                        WIZCHIP.IF.SPI._write_burst != saved_write_burst;
    status_installed = WIZCHIP.IF.SPI_STATUS._check_busy == status_ok &&
                       WIZCHIP.IF.SPI_STATUS._get_error == status_ok;

    if (transport_changed) {
        WIZCHIP.IF.SPI._read_byte = saved_read_byte;
        WIZCHIP.IF.SPI._write_byte = saved_write_byte;
        WIZCHIP.IF.SPI._read_burst = saved_read_burst;
        WIZCHIP.IF.SPI._write_burst = saved_write_burst;
    }

    return !transport_changed && status_installed ? W5500_DIAG_STATUS_OK
                                                   : W5500_DIAG_STATUS_ALIAS;
}
