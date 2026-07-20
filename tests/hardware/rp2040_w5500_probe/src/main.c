#include "hardware/gpio.h"
#include "pico/critical_section.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "socket.h"
#include "wizchip_conf.h"
#include "W5500/w5500.h"
#include "wizchip_qspi_pio.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define PROBE_SOCKET 0u
#define PROBE_PORT 49002u

static critical_section_t spi_lock;
static wiznet_spi_handle_t spi_handle;

static const wiznet_spi_config_t spi_config = {
    .data_in_pin = 22,
    .data_out_pin = 23,
    .cs_pin = 20,
    .clock_pin = 21,
    .irq_pin = 24,
    .reset_pin = 25,
    .clock_div_major = PIO_CLOCK_DIV_MAJOR,
    .clock_div_minor = PIO_CLOCK_DIV_MINOR,
};

static void enter_spi_lock(void)
{
    critical_section_enter_blocking(&spi_lock);
}

static void exit_spi_lock(void)
{
    critical_section_exit(&spi_lock);
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

static bool init_transport(void)
{
    spi_handle = wiznet_spi_pio_open(&spi_config);
    if (spi_handle == NULL) {
        return false;
    }

    (*spi_handle)->set_active(spi_handle);
    critical_section_init(&spi_lock);
    gpio_put(spi_config.cs_pin, true);
    reg_wizchip_cris_cbfunc(enter_spi_lock, exit_spi_lock);
    reg_wizchip_cs_cbfunc(select_w5500, deselect_w5500);
    reg_wizchip_spi_cbfunc(read_byte, write_byte);
    reg_wizchip_spiburst_cbfunc(read_burst, write_burst);
    return true;
}

static void reset_w5500(void)
{
    gpio_init(spi_config.reset_pin);
    gpio_set_dir(spi_config.reset_pin, GPIO_OUT);
    gpio_put(spi_config.cs_pin, true);
    gpio_put(spi_config.reset_pin, false);
    sleep_ms(100u);
    gpio_put(spi_config.reset_pin, true);
    sleep_ms(100u);
}

int main(void)
{
    uint8_t memory[8] = {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u};
    uint8_t payload = 0xa5u;
    int8_t memory_init;
    int8_t phy_link;
    uint16_t tx_before;
    uint16_t tx_after;

    stdio_init_all();
    while (!stdio_usb_connected()) {
        sleep_ms(10u);
    }

    printf("PROBE boot\n");
    if (!init_transport()) {
        printf("PROBE transport=FAIL\n");
        return 1;
    }
    printf("PROBE transport=PASS\n");

    reset_w5500();
    printf("PROBE version=%02x\n", getVERSIONR());
    memory_init = wizchip_init(memory, memory);
    phy_link = wizphy_getphylink();
    printf("PROBE memory_init=%d phy_link=%d\n", memory_init, phy_link);

    printf("PROBE open_result=%d\n",
           socket(PROBE_SOCKET, Sn_MR_UDP, PROBE_PORT, 0u));
    printf("PROBE open_state=%02x port=%u\n", getSn_SR(PROBE_SOCKET),
           getSn_PORT(PROBE_SOCKET));
    tx_before = getSn_TX_WR(PROBE_SOCKET);
    wiz_send_data(PROBE_SOCKET, &payload, sizeof(payload));
    setSn_CR(PROBE_SOCKET, Sn_CR_SEND);
    while (getSn_CR(PROBE_SOCKET) != 0u) {
    }
    tx_after = getSn_TX_WR(PROBE_SOCKET);
    printf("PROBE tx_wr before=%04x after=%04x expected=%04x\n", tx_before,
           tx_after, (uint16_t)(tx_before + sizeof(payload)));
    close(PROBE_SOCKET);
    printf("PROBE final_state=%02x\n", getSn_SR(PROBE_SOCKET));

    for (;;) {
        sleep_ms(1000u);
    }
}
