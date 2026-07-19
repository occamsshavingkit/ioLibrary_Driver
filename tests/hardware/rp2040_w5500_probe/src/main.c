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

static uint16_t read16(uint32_t address)
{
    return (uint16_t)(((uint16_t)WIZCHIP_READ(address) << 8) |
                      WIZCHIP_READ(WIZCHIP_OFFSET_INC(address, 1u)));
}

static void write16_sequential(uint32_t address, uint16_t value)
{
    WIZCHIP_WRITE(address, (uint8_t)(value >> 8));
    WIZCHIP_WRITE(WIZCHIP_OFFSET_INC(address, 1u), (uint8_t)value);
}

static void write16_burst(uint32_t address, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)(value >> 8), (uint8_t)value};

    WIZCHIP_WRITE_BUF(address, bytes, sizeof(bytes));
}

static void print_result(const char *state, const char *mode,
                         uint16_t tx, uint16_t rx)
{
    printf("PROBE state=%s mode=%s sr=%02x tx=%04x rx=%04x\n", state, mode,
           getSn_SR(PROBE_SOCKET), tx, rx);
}

static void run_pointer_write(const char *state, const char *mode,
                              uint16_t tx_value, uint16_t rx_value)
{
    uint16_t saved_tx = read16(Sn_TX_WR(PROBE_SOCKET));
    uint16_t saved_rx = read16(Sn_RX_RD(PROBE_SOCKET));

    if (mode[0] == 's') {
        write16_sequential(Sn_TX_WR(PROBE_SOCKET), tx_value);
        write16_sequential(Sn_RX_RD(PROBE_SOCKET), rx_value);
    } else if (mode[0] == 'b') {
        write16_burst(Sn_TX_WR(PROBE_SOCKET), tx_value);
        write16_burst(Sn_RX_RD(PROBE_SOCKET), rx_value);
    } else {
        setSn_TX_WR(PROBE_SOCKET, tx_value);
        setSn_RX_RD(PROBE_SOCKET, rx_value);
    }

    print_result(state, mode, read16(Sn_TX_WR(PROBE_SOCKET)),
                 read16(Sn_RX_RD(PROBE_SOCKET)));
    write16_sequential(Sn_TX_WR(PROBE_SOCKET), saved_tx);
    write16_sequential(Sn_RX_RD(PROBE_SOCKET), saved_rx);
}

static void run_pointer_set(const char *state)
{
    run_pointer_write(state, "sequential", 0x0123u, 0x0456u);
    run_pointer_write(state, "burst", 0x0234u, 0x0567u);
    run_pointer_write(state, "api", 0x0345u, 0x0678u);
}

int main(void)
{
    uint8_t memory[8] = {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u};
    int8_t memory_init;
    int8_t phy_link;

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

    run_pointer_set("closed");
    printf("PROBE open_result=%d\n",
           socket(PROBE_SOCKET, Sn_MR_UDP, PROBE_PORT, 0u));
    printf("PROBE open_state=%02x\n", getSn_SR(PROBE_SOCKET));
    run_pointer_set("open");
    close(PROBE_SOCKET);
    printf("PROBE final_state=%02x\n", getSn_SR(PROBE_SOCKET));

    for (;;) {
        sleep_ms(1000u);
    }
}
