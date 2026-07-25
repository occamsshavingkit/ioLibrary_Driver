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
#include <string.h>

#define BENCH_SOCKET      0u
#define BENCH_PORT        49002u
#define SCALAR_ITERS      1000u
#define BUFFER_ITERS      100u
#define REGISTER_ITERS    100u
#define INIT_ITERS        20u
#define SOCKET_ITERS      50u
#define MAX_BUFFER_SIZE   16384u

static const uint16_t buffer_sizes[] = {
    4u, 16u, 64u, 256u, 1024u, 4096u, 16384u
};

static critical_section_t spi_lock;
static wiznet_spi_handle_t spi_handle;
static volatile uint32_t frame_count;
static volatile uint32_t byte_count;
static volatile uint64_t result_sink;
static uint8_t transfer_buffer[MAX_BUFFER_SIZE];

static const wiznet_spi_config_t spi_config = {
    .data_in_pin  = 22,
    .data_out_pin = 23,
    .cs_pin       = 20,
    .clock_pin    = 21,
    .irq_pin      = 24,
    .reset_pin    = 25,
    .clock_div_major = 2u,
    .clock_div_minor = 0u,
};

/* ---- instrumented callbacks ---- */

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
    frame_count++;
}

static uint8_t read_byte(void)
{
    byte_count++;
    return (*spi_handle)->read_byte();
}

static void write_byte(uint8_t value)
{
    byte_count++;
    (*spi_handle)->write_byte(value);
}

static void read_burst(uint8_t *buffer, uint16_t length)
{
    byte_count += length;
    (*spi_handle)->read_buffer(buffer, length);
}

static void write_burst(uint8_t *buffer, uint16_t length)
{
    byte_count += length;
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

/* ---- transport init ---- */

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
    reg_wizchip_spistatus_cbfunc(transport_busy, transport_error,
                                 clear_transport_error);
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

/* ---- measurement helpers ---- */

static void bench_reset_counters(void)
{
    frame_count = 0u;
    byte_count  = 0u;
}

static void bench_start(uint64_t *started_us)
{
    bench_reset_counters();
    *started_us = time_us_64();
}

static void bench_stop(uint64_t started_us,
                       uint32_t *out_frames, uint32_t *out_bytes,
                       uint64_t *out_elapsed_us)
{
    *out_elapsed_us = time_us_64() - started_us;
    *out_frames     = frame_count;
    *out_bytes      = byte_count;
}

static void print_row(const char *op, const char *path,
                      uint32_t size, uint32_t iters,
                      uint32_t frames, uint32_t bytes,
                      uint64_t elapsed_us)
{
    printf("%-20s %-9s %7lu %10lu"
           " %12lu %12lu %12llu %12llu\n",
           op, path, (unsigned long)size, (unsigned long)iters,
           (unsigned long)frames, (unsigned long)bytes,
           (unsigned long long)elapsed_us,
           (unsigned long long)(iters != 0u ? elapsed_us * 1000u / iters : 0u));
}

/* ---- benchmark operations ---- */

static void bench_scalar(void)
{
    uint64_t started_us, elapsed;
    uint32_t frames, bytes;
    uint32_t i;

    bench_start(&started_us);
    for (i = 0u; i < SCALAR_ITERS; i++) {
        result_sink += getVERSIONR();
    }
    bench_stop(started_us, &frames, &bytes, &elapsed);
    print_row("scalar_read8", "burst", 1u, SCALAR_ITERS,
              frames, bytes, elapsed);

    bench_start(&started_us);
    for (i = 0u; i < SCALAR_ITERS; i++) {
        setSn_MR(BENCH_SOCKET, (uint8_t)(i & 0xFFu));
    }
    bench_stop(started_us, &frames, &bytes, &elapsed);
    print_row("scalar_write8", "burst", 1u, SCALAR_ITERS,
              frames, bytes, elapsed);
}

static void bench_buffers(void)
{
    size_t idx;
    uint32_t addrsel;

    memset(transfer_buffer, 0x5au, sizeof(transfer_buffer));
    for (idx = 0u; idx < sizeof(buffer_sizes) / sizeof(buffer_sizes[0]); idx++) {
        uint16_t sz = buffer_sizes[idx];
        uint64_t started_us, elapsed;
        uint32_t frames, bytes;
        uint32_t i;

        /* burst read */
        bench_start(&started_us);
        for (i = 0u; i < BUFFER_ITERS; i++) {
            addrsel = ((uint32_t)getSn_RX_RD(BENCH_SOCKET) << 8) +
                      (WIZCHIP_RXBUF_BLOCK(BENCH_SOCKET) << 3);
            WIZCHIP_READ_BUF(addrsel, transfer_buffer, sz);
        }
        result_sink += transfer_buffer[sz - 1u];
        bench_stop(started_us, &frames, &bytes, &elapsed);
        print_row("buffer_read", "burst", sz, BUFFER_ITERS,
                  frames, bytes, elapsed);

        /* burst write */
        bench_start(&started_us);
        for (i = 0u; i < BUFFER_ITERS; i++) {
            addrsel = ((uint32_t)getSn_TX_WR(BENCH_SOCKET) << 8) +
                      (WIZCHIP_TXBUF_BLOCK(BENCH_SOCKET) << 3);
            WIZCHIP_WRITE_BUF(addrsel, transfer_buffer, sz);
        }
        bench_stop(started_us, &frames, &bytes, &elapsed);
        print_row("buffer_write", "burst", sz, BUFFER_ITERS,
                  frames, bytes, elapsed);
    }
}

static void bench_registers(void)
{
    uint64_t started_us, elapsed;
    uint32_t frames, bytes;
    uint32_t i;
    uint16_t value = 0u;
    int8_t status;

    bench_start(&started_us);
    for (i = 0u; i < REGISTER_ITERS; i++) {
        result_sink += getSn_PORT(BENCH_SOCKET);
    }
    bench_stop(started_us, &frames, &bytes, &elapsed);
    print_row("register_read16", "burst", 2u, REGISTER_ITERS,
              frames, bytes, elapsed);

    bench_start(&started_us);
    for (i = 0u; i < REGISTER_ITERS; i++) {
        setSn_PORT(BENCH_SOCKET, (uint16_t)(5000u + i));
    }
    bench_stop(started_us, &frames, &bytes, &elapsed);
    print_row("register_write16", "burst", 2u, REGISTER_ITERS,
              frames, bytes, elapsed);

    bench_start(&started_us);
    for (i = 0u; i < REGISTER_ITERS; i++) {
        status = getSn_TX_FSR_stable(BENCH_SOCKET, &value);
        result_sink += value;
        if (status != SOCK_OK) {
            printf("BENCH_WARN stable_tx_fsr=%d at iter=%lu\n",
                   status, (unsigned long)i);
            break;
        }
    }
    bench_stop(started_us, &frames, &bytes, &elapsed);
    print_row("stable_TX_FSR", "burst", 2u, REGISTER_ITERS,
              frames, bytes, elapsed);

    bench_start(&started_us);
    for (i = 0u; i < REGISTER_ITERS; i++) {
        status = getSn_RX_RSR_stable(BENCH_SOCKET, &value);
        result_sink += value;
        if (status != SOCK_OK) {
            printf("BENCH_WARN stable_rx_rsr=%d at iter=%lu\n",
                   status, (unsigned long)i);
            break;
        }
    }
    bench_stop(started_us, &frames, &bytes, &elapsed);
    print_row("stable_RX_RSR", "burst", 2u, REGISTER_ITERS,
              frames, bytes, elapsed);
}

static void bench_init(void)
{
    uint8_t memory[8] = {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u};
    uint64_t started_us, elapsed;
    uint32_t frames, bytes;
    uint32_t i;
    int8_t status = 0;

    bench_start(&started_us);
    for (i = 0u; i < INIT_ITERS; i++) {
        status = wizchip_init(memory, memory);
        if (status != 0) {
            printf("BENCH_ERR wizchip_init=%d at iter=%lu\n",
                   status, (unsigned long)i);
            break;
        }
    }
    bench_stop(started_us, &frames, &bytes, &elapsed);
    print_row("wizchip_init", "hw", 16384u, i != 0u ? i : 1u,
              frames, bytes, elapsed);
}

static void bench_socket_cycle(void)
{
    uint8_t memory[8] = {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u};
    uint8_t ip_address[4] = {192u, 0u, 2u, 1u};
    uint64_t started_us, elapsed;
    uint32_t frames, bytes;
    uint32_t i;
    int8_t status;

    /* re-init with known state */
    (void)wizchip_init(memory, memory);
    setSIPR(ip_address);

    bench_start(&started_us);
    for (i = 0u; i < SOCKET_ITERS; i++) {
        status = socket(BENCH_SOCKET, Sn_MR_TCP, 5000u, 0u);
        if (status != 0 || close(BENCH_SOCKET) != SOCK_OK) {
            printf("BENCH_ERR socket_cycle=%d at iter=%lu\n",
                   status, (unsigned long)i);
            break;
        }
    }
    bench_stop(started_us, &frames, &bytes, &elapsed);
    print_row("socket_open_close", "hw", 0u, i != 0u ? i : 1u,
              frames, bytes, elapsed);
}

/* ---- main ---- */

int main(void)
{
    uint8_t memory[8] = {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u};
    int8_t status;
    uint8_t version;

    stdio_init_all();
    while (!stdio_usb_connected()) {
        sleep_ms(10u);
    }

    printf("BENCH boot\n");

    if (!init_transport()) {
        printf("BENCH transport=FAIL\n");
        return 1;
    }

    reset_w5500();
    version = getVERSIONR();
    printf("BENCH version=%02x\n", version);
    if (version != 0x04u) {
        printf("BENCH version=UNEXPECTED\n");
        return 1;
    }

    status = wizchip_init(memory, memory);
    if (status != 0) {
        printf("BENCH init=%d\n", status);
        return 1;
    }

    /* open a UDP socket once for the register and stable-helper benches */
    status = socket(BENCH_SOCKET, Sn_MR_UDP, BENCH_PORT, 0u);
    if (status != BENCH_SOCKET) {
        printf("BENCH open_socket=%d\n", status);
    }

    /* CSV header */
    printf("%-20s %-9s %7s %10s %12s %12s %12s %12s\n",
           "operation", "path", "bytes", "iters",
           "frames", "spi_bytes", "elapsed_us", "avg_ns");

    bench_scalar();
    bench_buffers();
    bench_registers();
    bench_init();
    bench_socket_cycle();

    printf("BENCH done sink=%llu\n", (unsigned long long)result_sink);

    for (;;) {
        sleep_ms(1000u);
    }
}
