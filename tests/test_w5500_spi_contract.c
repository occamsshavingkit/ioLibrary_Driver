#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wizchip_conf.h"
#include "w5500.h"

static unsigned int failures;
static unsigned int select_calls;
static unsigned int deselect_calls;
static unsigned int byte_write_calls;
static unsigned int burst_write_calls;
static uint8_t written_bytes[8];
static uint16_t written_length;

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL: %s\n", (message)); \
    } \
} while (0)

static void critical_enter(void) {}
static void critical_exit(void) {}
static void chip_select(void) { ++select_calls; }
static void chip_deselect(void) { ++deselect_calls; }
static uint8_t read_byte(void) { return 0u; }

static void write_byte(uint8_t value)
{
    ++byte_write_calls;
    if (written_length < sizeof(written_bytes)) {
        written_bytes[written_length++] = value;
    }
}

static void read_burst(uint8_t *buffer, uint16_t length)
{
    memset(buffer, 0, length);
}

static void write_burst(uint8_t *buffer, uint16_t length)
{
    uint16_t index;

    ++burst_write_calls;
    for (index = 0u;
         index < length && written_length < sizeof(written_bytes);
         ++index) {
        written_bytes[written_length++] = buffer[index];
    }
}

static uint8_t spi_not_busy(void) { return 0u; }
static int8_t spi_no_error(void) { return 0; }
static void spi_clear_error(void) {}

static void reset_trace(void)
{
    select_calls = 0u;
    deselect_calls = 0u;
    byte_write_calls = 0u;
    burst_write_calls = 0u;
    written_length = 0u;
    memset(written_bytes, 0, sizeof(written_bytes));
}

static void install_trace(uint8_t use_burst)
{
    reg_wizchip_cris_cbfunc(critical_enter, critical_exit);
    reg_wizchip_cs_cbfunc(chip_select, chip_deselect);
    reg_wizchip_spi_cbfunc(read_byte, write_byte);
    reg_wizchip_spiburst_cbfunc(use_burst != 0u ? read_burst : 0,
                                use_burst != 0u ? write_burst : 0);
    reg_wizchip_spistatus_cbfunc(spi_not_busy, spi_no_error,
                                 spi_clear_error);
    reset_trace();
}

static void test_scalar_write_prefers_one_burst(void)
{
    const uint32_t address = Sn_TX_WR(0);

    install_trace(1u);
    CHECK(wizchip_write8_checked(address, 0x5au) == 0,
          "scalar write reports success");
    CHECK(select_calls == 1u && deselect_calls == 1u,
          "scalar write uses one CS-delimited transaction");
    CHECK(burst_write_calls == 1u,
          "scalar write uses exactly one burst callback");
    CHECK(byte_write_calls == 0u,
          "scalar write does not split data into the byte callback");
    CHECK(written_length == 4u,
          "scalar write emits one three-byte header and one data byte");
    CHECK(written_bytes[0] == (uint8_t)(address >> 16) &&
              written_bytes[1] == (uint8_t)(address >> 8) &&
              written_bytes[2] ==
                  (uint8_t)(address | _W5500_SPI_WRITE_) &&
              written_bytes[3] == 0x5au,
          "scalar write preserves W5500 VDM byte order");
}

static void test_scalar_write_byte_fallback(void)
{
    const uint32_t address = Sn_RX_RD(1);

    install_trace(0u);
    CHECK(wizchip_write8_checked(address, 0xa5u) == 0,
          "byte-fallback scalar write reports success");
    CHECK(select_calls == 1u && deselect_calls == 1u,
          "byte-fallback write uses one CS-delimited transaction");
    CHECK(burst_write_calls == 0u,
          "byte-fallback write does not call an absent burst callback");
    CHECK(byte_write_calls == 4u && written_length == 4u,
          "byte-fallback write emits all four bytes through one callback");
    CHECK(written_bytes[0] == (uint8_t)(address >> 16) &&
              written_bytes[1] == (uint8_t)(address >> 8) &&
              written_bytes[2] ==
                  (uint8_t)(address | _W5500_SPI_WRITE_) &&
              written_bytes[3] == 0xa5u,
          "byte-fallback write preserves W5500 VDM byte order");
}

int main(void)
{
    test_scalar_write_prefers_one_burst();
    test_scalar_write_byte_fallback();

    if (failures != 0u) {
        fprintf(stderr, "\n%u SPI contract failures\n", failures);
        return 1;
    }
    printf("PASS: W5500 SPI scalar-write contract\n");
    return 0;
}
