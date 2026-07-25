#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define _WIZCHIP_ 5500
#define _WIZCHIP_IO_MODE_ 0x0203
#include "wizchip_conf.h"
#include "w5500.h"
#include "socket.h"
#include "support/w5500_spi_model.h"

#ifndef BENCH_OPT_LABEL
#if defined(__OPTIMIZE_SIZE__)
#define BENCH_OPT_LABEL "-Os"
#elif defined(__OPTIMIZE__)
#define BENCH_OPT_LABEL "optimized"
#else
#define BENCH_OPT_LABEL "unoptimized"
#endif
#endif

enum {
    SCALAR_ITERATIONS = 1000,
    BUFFER_ITERATIONS = 1000,
    REGISTER_ITERATIONS = 100,
    INIT_ITERATIONS = 100,
    SOCKET_ITERATIONS = 100,
    MAX_BUFFER_SIZE = 16384
};

typedef struct {
    const char *operation;
    const char *path;
    uint32_t size;
    uint32_t iterations;
} bench_case_t;

typedef struct {
    uint16_t size;
    uint8_t burst_enabled;
} buffer_case_t;

typedef struct {
    const char *operation;
    const char *path;
    uint32_t size;
    uint32_t iterations;
    uint64_t spi_bytes;
    uint64_t spi_frames;
    uint64_t elapsed_ns;
} bench_result_t;

static const uint16_t buffer_sizes[] = {
    1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u,
    256u, 512u, 1024u, 2048u, 4096u, 8192u, 16384u
};

static uint8_t transfer_buffer[MAX_BUFFER_SIZE];
static w5500_model_t model;
static uint8_t model_enabled;
static volatile uint64_t spi_byte_count;
static volatile uint64_t spi_frame_count;
static volatile uint64_t result_sink;

static void critical_enter(void) {}
static void critical_exit(void) {}

static void chip_select(void)
{
    if (model_enabled != 0u) {
        model_cs_select(&model);
    }
}

static void chip_deselect(void)
{
    if (model_enabled != 0u) {
        model_cs_deselect(&model);
    }
    ++spi_frame_count;
}

static uint8_t spi_read_byte(void)
{
    ++spi_byte_count;
    return model_enabled != 0u ? model_spi_read_byte(&model) : 0u;
}

static void spi_write_byte(uint8_t value)
{
    ++spi_byte_count;
    if (model_enabled != 0u) {
        model_spi_write_byte(&model, value);
    }
}

static void spi_read_burst(uint8_t *buffer, uint16_t length)
{
    spi_byte_count += length;
    if (model_enabled != 0u) {
        model_spi_read_burst(&model, buffer, length);
    } else {
        memset(buffer, 0, length);
    }
}

static void spi_write_burst(uint8_t *buffer, uint16_t length)
{
    spi_byte_count += length;
    if (model_enabled != 0u) {
        model_spi_write_burst(&model, buffer, length);
    }
}

static uint8_t spi_not_busy(void) { return 0u; }
static int8_t spi_no_error(void) { return 0; }
static void spi_clear_error(void) {}

static uint64_t monotonic_ns(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
           (uint64_t)now.tv_nsec;
}

static uint64_t measurement_start(void)
{
    spi_byte_count = 0u;
    spi_frame_count = 0u;
    return monotonic_ns();
}

static bench_result_t measurement_stop(bench_case_t benchmark,
                                       uint64_t started_ns)
{
    bench_result_t result;

    result.operation = benchmark.operation;
    result.path = benchmark.path;
    result.size = benchmark.size;
    result.iterations = benchmark.iterations;
    result.spi_bytes = spi_byte_count;
    result.spi_frames = spi_frame_count;
    result.elapsed_ns = monotonic_ns() - started_ns;
    return result;
}

static void print_result(const bench_result_t *result)
{
    uint64_t average_ns = result->elapsed_ns / result->iterations;

    printf("%-20s %-9s %7" PRIu32 " %10" PRIu32
           " %12" PRIu64 " %10" PRIu64 " %12" PRIu64
           " %12" PRIu64 "\n",
           result->operation, result->path, result->size, result->iterations,
           result->spi_bytes, result->spi_frames,
           result->elapsed_ns / UINT64_C(1000), average_ns);
}

static void select_spi_path(uint8_t burst_enabled)
{
    reg_wizchip_spiburst_cbfunc(burst_enabled != 0u ? spi_read_burst : NULL,
                                burst_enabled != 0u ? spi_write_burst : NULL);
}

static void measure_scalar_access(void)
{
    uint64_t started_ns;
    bench_result_t result;
    uint32_t iteration;

    select_spi_path(1u);
    started_ns = measurement_start();
    for (iteration = 0u; iteration < SCALAR_ITERATIONS; ++iteration) {
        result_sink += getVERSIONR();
    }
    result = measurement_stop((bench_case_t){
        "scalar read8", "burst", 1u, SCALAR_ITERATIONS
    }, started_ns);
    print_result(&result);

    started_ns = measurement_start();
    for (iteration = 0u; iteration < SCALAR_ITERATIONS; ++iteration) {
        setSn_MR(0u, (uint8_t)iteration);
    }
    result = measurement_stop((bench_case_t){
        "scalar write8", "burst", 1u, SCALAR_ITERATIONS
    }, started_ns);
    print_result(&result);
}

static void measure_buffer_read(buffer_case_t benchmark)
{
    const char *path = benchmark.burst_enabled != 0u ? "burst" : "fallback";
    uint64_t started_ns;
    bench_result_t result;
    uint32_t iteration;

    select_spi_path(benchmark.burst_enabled);
    started_ns = measurement_start();
    for (iteration = 0u; iteration < BUFFER_ITERATIONS; ++iteration) {
        WIZCHIP_READ_BUF(Sn_RX_RSR(0u), transfer_buffer, benchmark.size);
    }
    result_sink += transfer_buffer[benchmark.size - 1u];
    result = measurement_stop((bench_case_t){
        "buffer read", path, benchmark.size, BUFFER_ITERATIONS
    }, started_ns);
    print_result(&result);
}

static void measure_buffer_write(buffer_case_t benchmark)
{
    const char *path = benchmark.burst_enabled != 0u ? "burst" : "fallback";
    uint64_t started_ns;
    bench_result_t result;
    uint32_t iteration;

    select_spi_path(benchmark.burst_enabled);
    started_ns = measurement_start();
    for (iteration = 0u; iteration < BUFFER_ITERATIONS; ++iteration) {
        WIZCHIP_WRITE_BUF(Sn_TX_WR(0u), transfer_buffer, benchmark.size);
    }
    result = measurement_stop((bench_case_t){
        "buffer write", path, benchmark.size, BUFFER_ITERATIONS
    }, started_ns);
    print_result(&result);
}

static void measure_buffers(void)
{
    size_t index;
    uint8_t burst_enabled;

    memset(transfer_buffer, 0x5a, sizeof(transfer_buffer));
    for (burst_enabled = 1u;; --burst_enabled) {
        for (index = 0u;
             index < sizeof(buffer_sizes) / sizeof(buffer_sizes[0]);
             ++index) {
            buffer_case_t benchmark = {buffer_sizes[index], burst_enabled};

            measure_buffer_read(benchmark);
            measure_buffer_write(benchmark);
        }
        if (burst_enabled == 0u) {
            break;
        }
    }
}

static void measure_register_helpers(void)
{
    uint64_t started_ns;
    bench_result_t result;
    uint32_t iteration;
    uint16_t value = 0u;
    int8_t status = 0;

    select_spi_path(1u);
    started_ns = measurement_start();
    for (iteration = 0u; iteration < REGISTER_ITERATIONS; ++iteration) {
        result_sink += getSn_PORT(0u);
    }
    result = measurement_stop((bench_case_t){
        "register read16", "burst", 2u, REGISTER_ITERATIONS
    }, started_ns);
    print_result(&result);

    started_ns = measurement_start();
    for (iteration = 0u; iteration < REGISTER_ITERATIONS; ++iteration) {
        setSn_PORT(0u, (uint16_t)(5000u + iteration));
    }
    result = measurement_stop((bench_case_t){
        "register write16", "burst", 2u, REGISTER_ITERATIONS
    }, started_ns);
    print_result(&result);

    started_ns = measurement_start();
    for (iteration = 0u; iteration < REGISTER_ITERATIONS; ++iteration) {
        status = getSn_TX_FSR_stable(0u, &value);
        result_sink += value;
    }
    if (status != SOCK_OK) {
        fprintf(stderr, "getSn_TX_FSR_stable failed: %d\n", status);
        exit(EXIT_FAILURE);
    }
    result = measurement_stop((bench_case_t){
        "stable TX FSR", "burst", 2u, REGISTER_ITERATIONS
    }, started_ns);
    print_result(&result);

    started_ns = measurement_start();
    for (iteration = 0u; iteration < REGISTER_ITERATIONS; ++iteration) {
        status = getSn_RX_RSR_stable(0u, &value);
        result_sink += value;
    }
    if (status != SOCK_OK) {
        fprintf(stderr, "getSn_RX_RSR_stable failed: %d\n", status);
        exit(EXIT_FAILURE);
    }
    result = measurement_stop((bench_case_t){
        "stable RX RSR", "burst", 2u, REGISTER_ITERATIONS
    }, started_ns);
    print_result(&result);
}

static void measure_full_init(void)
{
    uint8_t memory_sizes[8] = {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u};
    uint64_t started_ns;
    bench_result_t result;
    uint32_t iteration;
    int8_t status = 0;

    model_init(&model);
    model_enabled = 1u;
    select_spi_path(1u);
    started_ns = measurement_start();
    for (iteration = 0u; iteration < INIT_ITERATIONS; ++iteration) {
        status = wizchip_init(memory_sizes, memory_sizes);
        result_sink += (uint8_t)status;
        if (status != 0) {
            break;
        }
    }
    if (status != 0) {
        fprintf(stderr, "wizchip_init failed: %d\n", status);
        exit(EXIT_FAILURE);
    }
    result = measurement_stop((bench_case_t){
        "full wizchip_init", "model", 16384u, INIT_ITERATIONS
    }, started_ns);
    print_result(&result);
}

static void measure_socket_cycle(void)
{
    uint8_t memory_sizes[8] = {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u};
    uint8_t ip_address[4] = {192u, 0u, 2u, 1u};
    uint64_t started_ns;
    bench_result_t result;
    uint32_t iteration;
    int8_t status;

    model_init(&model);
    model_enabled = 1u;
    select_spi_path(1u);
    if (wizchip_init(memory_sizes, memory_sizes) != 0) {
        fputs("socket benchmark setup failed\n", stderr);
        exit(EXIT_FAILURE);
    }
    setSIPR(ip_address);

    started_ns = measurement_start();
    for (iteration = 0u; iteration < SOCKET_ITERATIONS; ++iteration) {
        status = socket(0u, Sn_MR_TCP, 5000u, 0u);
        if (status != 0 || close(0u) != SOCK_OK) {
            fprintf(stderr, "socket cycle failed at iteration %" PRIu32 "\n",
                    iteration);
            exit(EXIT_FAILURE);
        }
    }
    result = measurement_stop((bench_case_t){
        "socket open/close", "model", 0u, SOCKET_ITERATIONS
    }, started_ns);
    print_result(&result);
}

static void install_callbacks(void)
{
    reg_wizchip_cris_cbfunc(critical_enter, critical_exit);
    reg_wizchip_cs_cbfunc(chip_select, chip_deselect);
    reg_wizchip_spi_cbfunc(spi_read_byte, spi_write_byte);
    reg_wizchip_spistatus_cbfunc(spi_not_busy, spi_no_error,
                                 spi_clear_error);
    select_spi_path(1u);
}

int main(void)
{
    /* For RP2040 hardware timing, run on target with a hardware timer. */
    install_callbacks();

    printf("W5500 host SPI benchmark (%s)\n", BENCH_OPT_LABEL);
    puts("Host wall-clock results include driver and callback overhead only.");
    printf("%-20s %-9s %7s %10s %12s %10s %12s %12s\n",
           "operation", "path", "bytes", "iterations", "SPI bytes",
           "frames", "elapsed us", "avg ns/op");

    model_enabled = 0u;
    measure_scalar_access();
    measure_buffers();
    measure_register_helpers();
    measure_full_init();
    measure_socket_cycle();

    return result_sink == UINT64_MAX ? EXIT_FAILURE : EXIT_SUCCESS;
}
