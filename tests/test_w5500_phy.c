#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "wizchip_conf.h"

#define PHYCFGR_OFFSET 0x002Eu
#define PHY_RST_BIT 0x80u
#define PHY_CONFIG_MASK 0x78u
#define PHY_RESET_HOLD_US 200u
#define PHY_WRITE_LOG_SIZE 16u
#define PHY_DEADLINE_ERROR (-16)

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL [%d]: %s\n", __LINE__, (message)); \
    } \
} while (0)

typedef struct {
    uint8_t value;
    uint64_t at_us;
} phy_write_t;

static unsigned int failures;
static uint8_t phycfgr;
static uint8_t suppress_rst_high;
static uint16_t spi_addr;
static uint8_t spi_header_bytes;
static uint64_t fake_now_us;
static uint64_t waited_us;
static phy_write_t phy_writes[PHY_WRITE_LOG_SIZE];
static size_t phy_write_count;
static unsigned int global_lock_depth;
static unsigned int global_lock_count;
static unsigned int global_unlock_count;
static unsigned int unlocked_phy_accesses;
static uint8_t require_phy_lock;

static void fake_critical_enter(void) {}
static void fake_critical_exit(void) {}

static void fake_socket_lock(uint8_t sn) { (void)sn; }
static void fake_socket_unlock(uint8_t sn) { (void)sn; }

static void fake_global_lock(void)
{
    ++global_lock_depth;
    ++global_lock_count;
}

static void fake_global_unlock(void)
{
    if (global_lock_depth > 0u) {
        --global_lock_depth;
    }
    ++global_unlock_count;
}

static void fake_select(void)
{
    spi_addr = 0u;
    spi_header_bytes = 0u;
}

static void fake_deselect(void) {}

static uint8_t fake_spi_read(void)
{
    uint8_t value = 0u;

    if (spi_addr == PHYCFGR_OFFSET) {
        if (require_phy_lock && global_lock_depth == 0u) {
            ++unlocked_phy_accesses;
        }
        value = phycfgr;
    }
    ++spi_addr;
    return value;
}

static void fake_spi_write(uint8_t value)
{
    if (spi_header_bytes == 0u) {
        spi_addr = (uint16_t)value << 8;
        ++spi_header_bytes;
        return;
    }
    if (spi_header_bytes == 1u) {
        spi_addr |= value;
        ++spi_header_bytes;
        return;
    }
    if (spi_header_bytes == 2u) {
        ++spi_header_bytes;
        return;
    }

    if (spi_addr == PHYCFGR_OFFSET) {
        if (require_phy_lock && global_lock_depth == 0u) {
            ++unlocked_phy_accesses;
        }
        if (phy_write_count < PHY_WRITE_LOG_SIZE) {
            phy_writes[phy_write_count].value = value;
            phy_writes[phy_write_count].at_us = fake_now_us;
            ++phy_write_count;
        }
        if (!(suppress_rst_high && (value & PHY_RST_BIT) != 0u)) {
            phycfgr = value;
        }
    }
    ++spi_addr;
}

static uint8_t fake_spi_busy(void)
{
    return 0u;
}

static int8_t fake_spi_error(void)
{
    return 0;
}

static void fake_spi_clear(void) {}

static uint64_t fake_now(void)
{
    return fake_now_us;
}

static void fake_wait(uint64_t us)
{
    waited_us += us;
    fake_now_us += us;
}

static void install_fake(void)
{
    reg_wizchip_cris_cbfunc(fake_critical_enter, fake_critical_exit);
    reg_wizchip_cs_cbfunc(fake_select, fake_deselect);
    reg_wizchip_spi_cbfunc(fake_spi_read, fake_spi_write);
    reg_wizchip_spiburst_cbfunc(NULL, NULL);
    reg_wizchip_spistatus_cbfunc(fake_spi_busy, fake_spi_error,
                                 fake_spi_clear);
    reg_wizchip_time_cbfunc(fake_now, fake_wait);
    (void)reg_wizchip_lock_cbfunc(fake_socket_lock, fake_socket_unlock,
                                  fake_global_lock, fake_global_unlock);
}

static void reset_fake(uint8_t initial_phycfgr)
{
    wizchip_timeout_config_t timeouts = {10u, 100u, 10u};

    phycfgr = initial_phycfgr;
    suppress_rst_high = 0u;
    spi_addr = 0u;
    spi_header_bytes = 0u;
    fake_now_us = 0u;
    waited_us = 0u;
    phy_write_count = 0u;
    global_lock_depth = 0u;
    global_lock_count = 0u;
    global_unlock_count = 0u;
    unlocked_phy_accesses = 0u;
    require_phy_lock = 0u;
    (void)wizchip_set_timeout_config(&timeouts);
}

static void check_global_lock_transaction(const char *message)
{
    CHECK(unlocked_phy_accesses == 0u, message);
    CHECK(global_lock_count > 0u, "PHY transaction acquires global lock");
    CHECK(global_lock_count == global_unlock_count,
          "PHY transaction balances global lock callbacks");
    CHECK(global_lock_depth == 0u, "PHY transaction releases global lock");
}

static void test_phy_api_signatures(void)
{
    CHECK(_Generic(&wizphy_reset, int8_t (*)(void): 1, default: 0),
          "wizphy_reset has the status-returning API");
    CHECK(_Generic(&wizphy_powerdown, int8_t (*)(void): 1, default: 0),
          "wizphy_powerdown has the status-returning API");
    CHECK(_Generic(&wizphy_powerup, int8_t (*)(void): 1, default: 0),
          "wizphy_powerup has the status-returning API");
    CHECK(_Generic(&wizphy_setphyconf,
                   int8_t (*)(wiz_PhyConf *): 1, default: 0),
          "wizphy_setphyconf accepts wiz_PhyConf and returns status");
    CHECK(_Generic(&wizphy_getphyconf,
                   int8_t (*)(wiz_PhyConf *): 1, default: 0),
          "wizphy_getphyconf accepts wiz_PhyConf and returns status");
    CHECK(_Generic(&wizphy_getphystat,
                   int8_t (*)(wiz_PhyConf *): 1, default: 0),
          "wizphy_getphystat accepts wiz_PhyConf and returns status");
    CHECK(_Generic(&wizphy_setphypmode,
                   int8_t (*)(uint8_t): 1, default: 0),
          "wizphy_setphypmode accepts a mode and returns status");
    CHECK(_Generic(&wizphy_getphypmode, int8_t (*)(void): 1, default: 0),
          "wizphy_getphypmode returns PHY power status");
    CHECK(_Generic(&wizphy_getphylink, int8_t (*)(void): 1, default: 0),
          "wizphy_getphylink returns PHY link status");
}

static int call_getphyconf_with_null(void)
{
    return wizphy_getphyconf(NULL) < 0 ? 0 : 1;
}

static void test_getphyconf_rejects_null(void)
{
    pid_t child = fork();
    int status = 0;

    CHECK(child >= 0, "fork isolates the null-pointer regression");
    if (child == 0) {
        _exit(call_getphyconf_with_null());
    }
    if (child < 0) {
        return;
    }

    CHECK(waitpid(child, &status, 0) == child,
          "null-pointer child can be collected");
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "wizphy_getphyconf(NULL) rejects the pointer without crashing");
}

static void test_unsupported_power_mode_is_rejected(void)
{
    size_t writes_before;

    reset_fake((uint8_t)(PHY_RST_BIT | PHYCFGR_OPMD |
                         PHYCFGR_OPMDC_ALLA));
    writes_before = phy_write_count;
    CHECK(wizphy_setphypmode(0xFFu) < 0,
          "unsupported PHY power mode is rejected");
    CHECK(phy_write_count == writes_before,
          "unsupported PHY power mode performs no register write");
}

static void test_ctlwizchip_propagates_phy_failure(void)
{
    reset_fake((uint8_t)(PHY_RST_BIT | PHYCFGR_OPMD |
                         PHYCFGR_OPMDC_ALLA));
    suppress_rst_high = 1u;

    CHECK(ctlwizchip(CW_RESET_PHY, NULL) == PHY_DEADLINE_ERROR,
          "ctlwizchip propagates the PHY reset deadline error");
}

static void test_reset_hold_and_rst_sequence(void)
{
    const uint8_t initial = (uint8_t)(PHY_RST_BIT | PHYCFGR_OPMD |
                                      PHYCFGR_OPMDC_100F |
                                      PHYCFGR_DPX_FULL |
                                      PHYCFGR_SPD_100 |
                                      PHYCFGR_LNK_ON);

    reset_fake(initial);
    CHECK(wizphy_reset() == 0, "PHY reset succeeds after verified readback");
    CHECK(waited_us >= PHY_RESET_HOLD_US,
          "PHY reset uses an observable 200 us engineering hold");
    CHECK(phy_write_count >= 2u, "PHY reset writes RST low and then high");
    if (phy_write_count >= 2u) {
        CHECK((phy_writes[0].value & PHY_RST_BIT) == 0u,
              "first PHY reset write drives RST low");
        CHECK((phy_writes[1].value & PHY_RST_BIT) != 0u,
              "second PHY reset write restores RST high");
        CHECK(phy_writes[1].at_us - phy_writes[0].at_us >=
                  PHY_RESET_HOLD_US,
              "RST remains low for at least the engineering hold interval");
        CHECK((phy_writes[0].value & PHY_CONFIG_MASK) ==
                  (initial & PHY_CONFIG_MASK),
              "RST-low read-modify-write preserves PHY configuration bits");
        CHECK((phy_writes[1].value & PHY_CONFIG_MASK) ==
                  (initial & PHY_CONFIG_MASK),
              "RST-high read-modify-write preserves PHY configuration bits");
    }
    CHECK((phycfgr & (uint8_t)(PHY_RST_BIT | PHY_CONFIG_MASK)) ==
              (initial & (uint8_t)(PHY_RST_BIT | PHY_CONFIG_MASK)),
          "masked reset readback retains the written configuration bits");
}

static void test_phy_reset_reports_deadline(void)
{
    reset_fake((uint8_t)(PHY_RST_BIT | PHYCFGR_OPMD |
                         PHYCFGR_OPMDC_ALLA));
    suppress_rst_high = 1u;

    CHECK(wizphy_reset() == PHY_DEADLINE_ERROR,
          "PHY reset reports a deadline when RST-high readback never matches");
    CHECK(fake_now_us >= PHY_RESET_HOLD_US,
          "deadline failure occurs only after the reset hold");
    CHECK((phycfgr & PHY_RST_BIT) == 0u,
          "stuck reset model remains observable at the deadline");
}

static void test_setphyconf_preserves_status_and_uses_global_lock(void)
{
    const uint8_t status = (uint8_t)(PHYCFGR_DPX_FULL |
                                     PHYCFGR_SPD_100 |
                                     PHYCFGR_LNK_ON);
    const uint8_t expected = (uint8_t)(PHY_RST_BIT | PHYCFGR_OPMD |
                                       PHYCFGR_OPMDC_10H | status);
    wiz_PhyConf conf = {
        PHY_CONFBY_SW,
        PHY_MODE_MANUAL,
        PHY_SPEED_10,
        PHY_DUPLEX_HALF
    };

    reset_fake((uint8_t)(PHY_RST_BIT | PHYCFGR_OPMD |
                         PHYCFGR_OPMDC_ALLA | status));
    require_phy_lock = 1u;

    CHECK(wizphy_setphyconf(&conf) == 0,
          "PHY configuration succeeds after exact reset readback");
    CHECK(phycfgr == expected,
          "PHY configuration preserves status bits and leaves RST high");
    check_global_lock_transaction(
        "PHY configuration accesses PHYCFGR only under global lock");
}

static void test_setphyconf_rejects_invalid_values(void)
{
    wiz_PhyConf conf = {
        2u,
        PHY_MODE_MANUAL,
        PHY_SPEED_10,
        PHY_DUPLEX_HALF
    };

    reset_fake((uint8_t)(PHY_RST_BIT | PHYCFGR_OPMD |
                         PHYCFGR_OPMDC_ALLA));
    CHECK(wizphy_setphyconf(&conf) < 0,
          "PHY configuration rejects an invalid configuration source");
    conf.by = PHY_CONFBY_SW;
    conf.mode = 2u;
    CHECK(wizphy_setphyconf(&conf) < 0,
          "PHY configuration rejects an invalid mode");
    conf.mode = PHY_MODE_MANUAL;
    conf.speed = 2u;
    CHECK(wizphy_setphyconf(&conf) < 0,
          "PHY configuration rejects an invalid speed");
    conf.speed = PHY_SPEED_10;
    conf.duplex = 2u;
    CHECK(wizphy_setphyconf(&conf) < 0,
          "PHY configuration rejects an invalid duplex");
    CHECK(phy_write_count == 0u,
          "invalid PHY configurations perform no register write");
}

static void test_phy_getters_use_global_lock_and_decode_exactly(void)
{
    wiz_PhyConf conf = {0u, 0u, 0u, 0u};

    reset_fake((uint8_t)(PHY_RST_BIT | PHYCFGR_OPMD |
                         PHYCFGR_OPMDC_100F | PHYCFGR_DPX_FULL |
                         PHYCFGR_SPD_100 | PHYCFGR_LNK_ON));
    require_phy_lock = 1u;

    CHECK(wizphy_getphyconf(&conf) == 0,
          "PHY configuration getter succeeds");
    CHECK(conf.by == PHY_CONFBY_SW && conf.mode == PHY_MODE_MANUAL &&
              conf.speed == PHY_SPEED_100 &&
              conf.duplex == PHY_DUPLEX_FULL,
          "PHY configuration getter decodes the exact masked mode");
    CHECK(wizphy_getphystat(&conf) == PHY_LINK_ON,
          "PHY status getter returns the sampled hardware link state");
    CHECK(conf.speed == PHY_SPEED_100 && conf.duplex == PHY_DUPLEX_FULL,
          "PHY status getter decodes hardware speed and duplex bits");
    check_global_lock_transaction(
        "PHY getters access PHYCFGR only under global lock");

    reset_fake(PHY_RST_BIT);
    require_phy_lock = 1u;
    CHECK(wizphy_getphystat(&conf) == PHY_LINK_OFF,
          "PHY status getter returns hardware link-down state");
    check_global_lock_transaction(
        "link-down status read accesses PHYCFGR only under global lock");
}

static void test_setphypmode_preserves_status_and_uses_global_lock(void)
{
    const uint8_t status = (uint8_t)(PHYCFGR_DPX_FULL |
                                     PHYCFGR_SPD_100 |
                                     PHYCFGR_LNK_ON);

    reset_fake((uint8_t)(PHY_RST_BIT | PHYCFGR_OPMD |
                         PHYCFGR_OPMDC_ALLA | status));
    require_phy_lock = 1u;

    CHECK(wizphy_setphypmode(PHY_POWER_DOWN) == 0,
          "PHY power-down succeeds after exact reset readback");
    CHECK(phycfgr == (uint8_t)(PHY_RST_BIT | PHYCFGR_OPMD |
                               PHYCFGR_OPMDC_PDOWN | status),
          "PHY power-down preserves status bits and leaves RST high");
    check_global_lock_transaction(
        "PHY power transaction accesses PHYCFGR only under global lock");
}

int main(void)
{
    install_fake();
    test_phy_api_signatures();
    test_getphyconf_rejects_null();
    test_unsupported_power_mode_is_rejected();
    test_ctlwizchip_propagates_phy_failure();
    test_reset_hold_and_rst_sequence();
    test_phy_reset_reports_deadline();
    test_setphyconf_preserves_status_and_uses_global_lock();
    test_setphyconf_rejects_invalid_values();
    test_phy_getters_use_global_lock_and_decode_exactly();
    test_setphypmode_preserves_status_and_uses_global_lock();

    if (failures != 0u) {
        fprintf(stderr, "\n%u FAILURES\n", failures);
        return 1;
    }
    printf("PASS: all %s checks\n", __FILE__);
    return 0;
}
