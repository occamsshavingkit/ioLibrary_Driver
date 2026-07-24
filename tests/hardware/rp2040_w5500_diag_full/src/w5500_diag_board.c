#include "w5500_diag_board.h"

#include "pico/critical_section.h"
#include "pico/stdlib.h"
#include "wizchip_qspi_pio.h"

#include <stddef.h>
#include <stdint.h>

extern void reg_wizchip_spistatus_cbfunc(uint8_t (*check_busy)(void),
                                         int8_t (*get_error)(void),
                                         void (*clear_error)(void))
    __attribute__((weak));

static critical_section_t spi_critical_section;
static wiznet_spi_handle_t spi_handle;
static bool critical_section_initialized;

static const wiznet_spi_config_t spi_config = {
    .data_in_pin = 22,
    .data_out_pin = 23,
    .cs_pin = 20,
    .clock_pin = 21,
    .irq_pin = 24,
    .reset_pin = 25,
    .clock_div_major = 2u,
    .clock_div_minor = 0u,
};

static void enter_critical_section(void)
{
    critical_section_enter_blocking(&spi_critical_section);
}

static void exit_critical_section(void)
{
    critical_section_exit(&spi_critical_section);
}

static void select_w5500(void)
{
    (*spi_handle)->frame_start();
}

static void deselect_w5500(void)
{
    (*spi_handle)->frame_end();
}

static uint8_t read_byte(void)
{
    return (*spi_handle)->read_byte();
}

static void write_byte(uint8_t value)
{
    (*spi_handle)->write_byte(value);
}

static void read_burst(uint8_t *buffer, uint16_t length)
{
    (*spi_handle)->read_buffer(buffer, length);
}

static void write_burst(uint8_t *buffer, uint16_t length)
{
    (*spi_handle)->write_buffer(buffer, length);
}

static uint8_t transport_busy(void)
{
    return spi_handle != NULL &&
           wiznet_spi_pio_get_state(spi_handle) == WIZNET_SPI_TRANSFERRING;
}

static int8_t transport_error(void)
{
    return spi_handle != NULL
               ? (int8_t)wiznet_spi_pio_get_last_error(spi_handle)
               : (int8_t)PICO_ERROR_INVALID_STATE;
}

static void clear_transport_error(void)
{
    if (spi_handle != NULL) {
        wiznet_spi_pio_clear_last_error(spi_handle);
    }
}

w5500_diag_board_status_t w5500_diag_board_check_status_contract(bool expected)
{
    return w5500_diag_status_contract_classify(
        expected, reg_wizchip_spistatus_cbfunc);
}

w5500_diag_board_status_t w5500_diag_board_init(void)
{
    if (spi_handle != NULL) {
        return W5500_DIAG_STATUS_OK;
    }

    spi_handle = wiznet_spi_pio_open(&spi_config);
    if (spi_handle == NULL) {
        return W5500_DIAG_STATUS_PIO_OPEN;
    }
    (*spi_handle)->set_active(spi_handle);

    if (!critical_section_initialized) {
        critical_section_init(&spi_critical_section);
        critical_section_initialized = true;
    }

    gpio_put(spi_config.cs_pin, true);
    reg_wizchip_cris_cbfunc(enter_critical_section, exit_critical_section);
    reg_wizchip_cs_cbfunc(select_w5500, deselect_w5500);
    reg_wizchip_spi_cbfunc(read_byte, write_byte);
    reg_wizchip_spiburst_cbfunc(read_burst, write_burst);
    reg_wizchip_spistatus_cbfunc(transport_busy, transport_error,
                                 clear_transport_error);
    return W5500_DIAG_STATUS_OK;
}

void w5500_diag_board_reset(void)
{
    gpio_init(spi_config.reset_pin);
    gpio_set_dir(spi_config.reset_pin, GPIO_OUT);
    gpio_put(spi_config.cs_pin, true);
    gpio_put(spi_config.reset_pin, false);
    sleep_ms(100u);
    gpio_put(spi_config.reset_pin, true);
    sleep_ms(100u);
    gpio_put(spi_config.cs_pin, true);
}
