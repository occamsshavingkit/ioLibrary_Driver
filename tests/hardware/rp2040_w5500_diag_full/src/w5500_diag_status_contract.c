#include "w5500_diag_status_contract.h"

#include <stddef.h>

static uint8_t status_not_busy(void)
{
    return 0u;
}

static int8_t status_no_error(void)
{
    return 0;
}

static void status_clear(void)
{
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
    uint8_t (*saved_check_busy)(void) = WIZCHIP.SPISTATUS._check_busy;
    int8_t (*saved_get_error)(void) = WIZCHIP.SPISTATUS._get_error;
    void (*saved_clear_error)(void) = WIZCHIP.SPISTATUS._clear_error;
    bool transport_changed;
    bool status_installed;

    if (registrar == NULL) {
        return expected ? W5500_DIAG_STATUS_NO_API
                        : W5500_DIAG_STATUS_NOT_CLAIMED;
    }

    registrar(status_not_busy, status_no_error, status_clear);
    transport_changed = WIZCHIP.IF.SPI._read_byte != saved_read_byte ||
                        WIZCHIP.IF.SPI._write_byte != saved_write_byte ||
                        WIZCHIP.IF.SPI._read_burst != saved_read_burst ||
                        WIZCHIP.IF.SPI._write_burst != saved_write_burst;
    status_installed =
        WIZCHIP.SPISTATUS._check_busy == status_not_busy &&
        WIZCHIP.SPISTATUS._get_error == status_no_error &&
        WIZCHIP.SPISTATUS._clear_error == status_clear;

    WIZCHIP.SPISTATUS._check_busy = saved_check_busy;
    WIZCHIP.SPISTATUS._get_error = saved_get_error;
    WIZCHIP.SPISTATUS._clear_error = saved_clear_error;

    if (transport_changed) {
        WIZCHIP.IF.SPI._read_byte = saved_read_byte;
        WIZCHIP.IF.SPI._write_byte = saved_write_byte;
        WIZCHIP.IF.SPI._read_burst = saved_read_burst;
        WIZCHIP.IF.SPI._write_burst = saved_write_burst;
    }

    return !transport_changed && status_installed ? W5500_DIAG_STATUS_OK
                                                   : W5500_DIAG_STATUS_ALIAS;
}
