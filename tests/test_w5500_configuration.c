#include <stdint.h>
#include <stdio.h>

#include "wizchip_conf.h"
#include "w5500.h"
#include "support/w5500_spi_model.h"

_Static_assert(_WIZCHIP_ == W5500, "root tests require W5500");
_Static_assert(_WIZCHIP_SOCK_NUM_ == 8, "W5500 exposes eight sockets");
_Static_assert((_WIZCHIP_IO_MODE_ & _WIZCHIP_IO_MODE_SPI_) != 0,
               "root tests require SPI mode");
_Static_assert(!((_WIZCHIP_IO_MODE_ & _WIZCHIP_IO_MODE_BUS_) != 0 &&
                 (_WIZCHIP_IO_MODE_ & _WIZCHIP_IO_MODE_SPI_) != 0),
               "BUS and SPI interface modes are mutually exclusive");
_Static_assert(_WIZCHIP_POLL_MAX_ > 0u,
               "poll fallback must have a finite positive bound");

static unsigned int failures;

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL: %s\n", (message)); \
    } \
} while (0)

static void test_shared_model_contract(void)
{
    w5500_model_t model;
    uint8_t frame[303];
    uint32_t tx_address = ((uint32_t)0x0010u << 8) |
                          ((uint32_t)WIZCHIP_TXBUF_BLOCK(0) << 3);
    size_t i;

    model_init(&model);
    CHECK(model_read8(&model, VERSIONR) == 0x04u,
          "fake VERSIONR is deterministic");

    model_write8(&model, GAR, 192u);
    CHECK(model_read8(&model, GAR) == 192u,
          "fake common-register state persists");

    model_write8(&model, Sn_MR(0), Sn_MR_UDP);
    model_write8(&model, Sn_CR(0), Sn_CR_OPEN);
    CHECK(model_read8(&model, Sn_CR(0)) == 0u,
          "fake Sn_CR command self-clears");
    CHECK(model_read8(&model, Sn_SR(0)) == SOCK_UDP,
          "fake OPEN status follows UDP protocol");

    model_write8(&model, MR, MR_RST);
    CHECK(model_read8(&model, MR) == 0u,
          "fake MR reset self-clears");
    CHECK(model_read8(&model, GAR) == 0u,
          "fake MR reset clears mutable registers");
    CHECK(model_read8(&model, VERSIONR) == 0x04u,
          "fake MR reset preserves VERSIONR");

    model.spi_error = (int8_t)-12;
    CHECK(model.spi_error == -12,
          "fake SPI error status preserves signed Pico errors");

    frame[0] = (uint8_t)(tx_address >> 16);
    frame[1] = (uint8_t)(tx_address >> 8);
    frame[2] = (uint8_t)(tx_address | _W5500_SPI_WRITE_);
    for (i = 3u; i < sizeof(frame); ++i) {
        frame[i] = (uint8_t)i;
    }
    model_cs_select(&model);
    model_spi_write_burst(&model, frame, (uint16_t)sizeof(frame));
    model_cs_deselect(&model);
    CHECK(model.tx_data[0][0x0010u] == frame[3] &&
              model.tx_data[0][0x0010u + 252u] == frame[255] &&
              model.tx_data[0][0x0010u + 299u] == frame[302],
          "fake decoder preserves complete long VDM frames");
}

int main(void)
{
    test_shared_model_contract();
    CHECK(WIZCHIP.if_mode == _WIZCHIP_IO_MODE_,
          "runtime interface mode matches the build configuration");
    CHECK(WIZCHIP.SPISTATUS._check_busy != NULL,
          "SPI busy callback has a safe default");
    CHECK(WIZCHIP.SPISTATUS._get_error != NULL,
          "SPI error callback has a safe default");
    CHECK(WIZCHIP.SPISTATUS._clear_error != NULL,
          "SPI clear callback has a safe default");
    CHECK(WIZCHIP.LOCK._sock_enter != NULL &&
              WIZCHIP.LOCK._sock_exit != NULL &&
              WIZCHIP.LOCK._global_enter != NULL &&
              WIZCHIP.LOCK._global_exit != NULL,
          "lock callbacks have single-task defaults");
    CHECK(wizchip_get_state() == WIZCHIP_STATE_UNINITIALIZED,
          "root lifecycle starts uninitialized");
    if (failures != 0u) {
        fprintf(stderr, "\n%u configuration failures\n", failures);
        return 1;
    }
    puts("PASS: W5500 build and default configuration");
    return 0;
}
