#include "diag_w5500_stages.h"

#include "diag_usb.h"
#include "diag_watchdog.h"
#include "w5500_diag_board.h"

#include "pico/time.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define DIAG_DRIVER_PHASE 1u
#define DIAG_SOCKET 0u
#define DIAG_TX_WR_PATTERN 0x1234u
#define DIAG_RX_RD_PATTERN 0x5678u

typedef enum {
    DIAG_POINTER_SEQUENTIAL,
    DIAG_POINTER_BURST,
    DIAG_POINTER_API
} diag_pointer_mode_t;

static const uint8_t burst_pattern[4] = {0xA5u, 0x5Au, 0x3Cu, 0xC3u};

static uint32_t stage_timeout(diag_stage_id_t stage)
{
    const diag_stage_descriptor_t *descriptor = diag_stage_descriptor(stage);

    return descriptor == NULL ? 0u : descriptor->timeout_ms;
}

static uint8_t read_byte(diag_stage_context_t *context,
                         diag_stage_id_t stage, uint16_t phase,
                         uint32_t address)
{
    uint8_t value;

    diag_watchdog_begin(stage, phase, context->sequence,
                        stage_timeout(stage));
    value = WIZCHIP_READ(address);
    diag_watchdog_complete();
    return value;
}

static void write_byte(diag_stage_context_t *context,
                       diag_stage_id_t stage, uint16_t phase,
                       uint32_t address, uint8_t value)
{
    diag_watchdog_begin(stage, phase, context->sequence,
                        stage_timeout(stage));
    WIZCHIP_WRITE(address, value);
    diag_watchdog_complete();
}

static void read_buffer(diag_stage_context_t *context,
                        diag_stage_id_t stage, uint16_t phase,
                        uint32_t address, uint8_t *buffer, uint16_t length)
{
    diag_watchdog_begin(stage, phase, context->sequence,
                        stage_timeout(stage));
    WIZCHIP_READ_BUF(address, buffer, length);
    diag_watchdog_complete();
}

static void write_buffer(diag_stage_context_t *context,
                         diag_stage_id_t stage, uint16_t phase,
                         uint32_t address, uint8_t *buffer, uint16_t length)
{
    diag_watchdog_begin(stage, phase, context->sequence,
                        stage_timeout(stage));
    WIZCHIP_WRITE_BUF(address, buffer, length);
    diag_watchdog_complete();
}

static uint16_t read_pointer_direct(diag_stage_context_t *context,
                                    diag_stage_id_t stage, uint16_t phase,
                                    uint32_t address)
{
    uint16_t high = read_byte(context, stage, phase, address);
    uint16_t low = read_byte(context, stage, phase,
                             WIZCHIP_OFFSET_INC(address, 1));

    return (uint16_t)((high << 8) | low);
}

static uint16_t read_pointer_burst(diag_stage_context_t *context,
                                   diag_stage_id_t stage, uint16_t phase,
                                   uint32_t address)
{
    uint8_t bytes[2];

    read_buffer(context, stage, phase, address, bytes, sizeof(bytes));
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static void write_pointer_sequential(diag_stage_context_t *context,
                                     diag_stage_id_t stage, uint16_t phase,
                                     uint32_t address, uint16_t value)
{
    write_byte(context, stage, phase, address, (uint8_t)(value >> 8));
    write_byte(context, stage, phase, WIZCHIP_OFFSET_INC(address, 1),
               (uint8_t)value);
}

static void write_pointer_burst(diag_stage_context_t *context,
                                diag_stage_id_t stage, uint16_t phase,
                                uint32_t address, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)(value >> 8), (uint8_t)value};

    write_buffer(context, stage, phase, address, bytes, sizeof(bytes));
}

static void write_pointer_api(diag_stage_context_t *context,
                              diag_stage_id_t stage, uint16_t phase,
                              bool tx_pointer, uint16_t value)
{
    diag_watchdog_begin(stage, phase, context->sequence,
                        stage_timeout(stage));
    if (tx_pointer) {
        setSn_TX_WR(DIAG_SOCKET, value);
    } else {
        setSn_RX_RD(DIAG_SOCKET, value);
    }
    diag_watchdog_complete();
}

static void write_pointer(diag_stage_context_t *context,
                          diag_stage_id_t stage, uint16_t phase,
                          diag_pointer_mode_t mode, bool tx_pointer,
                          uint16_t value)
{
    uint32_t address = tx_pointer ? Sn_TX_WR(DIAG_SOCKET)
                                  : Sn_RX_RD(DIAG_SOCKET);

    if (mode == DIAG_POINTER_SEQUENTIAL) {
        write_pointer_sequential(context, stage, phase, address, value);
    } else if (mode == DIAG_POINTER_BURST) {
        write_pointer_burst(context, stage, phase, address, value);
    } else {
        write_pointer_api(context, stage, phase, tx_pointer, value);
    }
}

static uint16_t read_pointer(diag_stage_context_t *context,
                             diag_stage_id_t stage, uint16_t phase,
                             diag_pointer_mode_t mode, bool tx_pointer)
{
    uint32_t address = tx_pointer ? Sn_TX_WR(DIAG_SOCKET)
                                  : Sn_RX_RD(DIAG_SOCKET);

    if (mode == DIAG_POINTER_BURST) {
        return read_pointer_burst(context, stage, phase, address);
    }
    return read_pointer_direct(context, stage, phase, address);
}

static void recover_unverified_restoration(diag_stage_context_t *context,
                                           diag_stage_id_t stage)
{
    uint8_t tx_size[8] = {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u};
    uint8_t rx_size[8] = {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u};

    diag_runner_prepare_repeat(context->runner, DIAG_STAGE_CHIP_RESET);

    diag_watchdog_begin(stage, DIAG_PHASE_RESTORE, context->sequence,
                        stage_timeout(stage));
    w5500_diag_board_reset();
    diag_watchdog_complete();

    diag_watchdog_begin(stage, DIAG_PHASE_RESTORE, context->sequence,
                        stage_timeout(stage));
    (void)wizchip_init(tx_size, rx_size);
    diag_watchdog_complete();
}

void diag_stage_set_details(diag_stage_context_t *context,
                            const char *format, ...)
{
    va_list arguments;
    size_t index;

    if (context == NULL || format == NULL) {
        return;
    }

    va_start(arguments, format);
    (void)vsnprintf(context->details, sizeof(context->details), format,
                    arguments);
    va_end(arguments);

    for (index = 0u; context->details[index] != '\0'; ++index) {
        if (context->details[index] == '\n' || context->details[index] == '\r') {
            context->details[index] = '\0';
            break;
        }
    }
}

diag_stage_result_t diag_stage_callback_layout(diag_stage_context_t *context)
{
    w5500_diag_board_status_t status =
        w5500_diag_board_check_status_contract(DIAG_EXPECT_SPI_STATUS != 0);

    switch (status) {
    case W5500_DIAG_STATUS_OK:
        return DIAG_STAGE_PASS;
    case W5500_DIAG_STATUS_NOT_CLAIMED:
        diag_stage_set_details(context, "code=status-not-claimed");
        return DIAG_STAGE_PASS;
    case W5500_DIAG_STATUS_NO_API:
        diag_stage_set_details(context, "code=no-status-api");
        return DIAG_STAGE_FAIL;
    case W5500_DIAG_STATUS_ALIAS:
        diag_stage_set_details(context, "code=status-alias");
        return DIAG_STAGE_FAIL;
    default:
        diag_stage_set_details(context, "code=status-alias");
        return DIAG_STAGE_FAIL;
    }
}

diag_stage_result_t diag_stage_transport_init(diag_stage_context_t *context)
{
    w5500_diag_board_status_t status;

    diag_watchdog_begin(DIAG_STAGE_TRANSPORT_INIT, DIAG_DRIVER_PHASE,
                        context->sequence,
                        stage_timeout(DIAG_STAGE_TRANSPORT_INIT));
    status = w5500_diag_board_init();
    diag_watchdog_complete();
    if (status != W5500_DIAG_STATUS_OK) {
        diag_stage_set_details(context, "code=pio-open");
        return DIAG_STAGE_FAIL;
    }
    return DIAG_STAGE_PASS;
}

diag_stage_result_t diag_stage_chip_reset(diag_stage_context_t *context)
{
    diag_watchdog_begin(DIAG_STAGE_CHIP_RESET, DIAG_DRIVER_PHASE,
                        context->sequence, stage_timeout(DIAG_STAGE_CHIP_RESET));
    w5500_diag_board_reset();
    diag_watchdog_complete();
    return DIAG_STAGE_PASS;
}

diag_stage_result_t diag_stage_version(diag_stage_context_t *context)
{
    uint8_t version;

    diag_watchdog_begin(DIAG_STAGE_VERSION, DIAG_DRIVER_PHASE,
                        context->sequence, stage_timeout(DIAG_STAGE_VERSION));
    version = getVERSIONR();
    diag_watchdog_complete();
    if (version != 0x04u) {
        diag_stage_set_details(context,
                               "code=version expected=04 actual=%02X",
                               (unsigned int)version);
        return DIAG_STAGE_FAIL;
    }
    return DIAG_STAGE_PASS;
}

diag_stage_result_t diag_stage_memory_init(diag_stage_context_t *context)
{
    uint8_t tx_size[8] = {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u};
    uint8_t rx_size[8] = {2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u};
    int8_t status;

    diag_watchdog_begin(DIAG_STAGE_MEMORY_INIT, DIAG_DRIVER_PHASE,
                        context->sequence,
                        stage_timeout(DIAG_STAGE_MEMORY_INIT));
    status = wizchip_init(tx_size, rx_size);
    diag_watchdog_complete();
    if (status != 0) {
        diag_stage_set_details(context, "code=memory-init");
        return DIAG_STAGE_FAIL;
    }
    return DIAG_STAGE_PASS;
}

diag_stage_result_t diag_stage_phy_link(diag_stage_context_t *context)
{
    absolute_time_t deadline =
        make_timeout_time_ms(stage_timeout(DIAG_STAGE_PHY_LINK));

    do {
        int8_t link;

        diag_usb_task();
        diag_watchdog_begin(DIAG_STAGE_PHY_LINK, DIAG_DRIVER_PHASE,
                            context->sequence,
                            stage_timeout(DIAG_STAGE_PHY_LINK));
        link = wizphy_getphylink();
        diag_watchdog_complete();
        if (link == PHY_LINK_ON) {
            return DIAG_STAGE_PASS;
        }
        sleep_ms(10u);
    } while (!time_reached(deadline));

    diag_stage_set_details(context, "code=phy-link-down");
    return DIAG_STAGE_FAIL;
}

diag_stage_result_t diag_stage_single_register(diag_stage_context_t *context)
{
    const diag_stage_id_t stage = DIAG_STAGE_SINGLE_REGISTER;
    uint8_t saved = read_byte(context, stage, DIAG_PHASE_SAVE, _RCR_);
    uint8_t expected = (uint8_t)(saved ^ 0x5Au);
    uint8_t actual;
    uint8_t restored;

    write_byte(context, stage, DIAG_PHASE_WRITE_TX, _RCR_, expected);
    actual = read_byte(context, stage, DIAG_PHASE_READ_TX, _RCR_);
    write_byte(context, stage, DIAG_PHASE_RESTORE, _RCR_, saved);
    restored = read_byte(context, stage, DIAG_PHASE_RESTORE, _RCR_);

    if (restored != saved) {
        recover_unverified_restoration(context, stage);
        diag_stage_set_details(context, "code=restore");
        return DIAG_STAGE_FAIL;
    }
    if (actual != expected) {
        diag_stage_set_details(context,
                               "code=readback expected=%02X actual=%02X",
                               (unsigned int)expected, (unsigned int)actual);
        return DIAG_STAGE_FAIL;
    }
    return DIAG_STAGE_PASS;
}

diag_stage_result_t diag_stage_burst_register(diag_stage_context_t *context)
{
    const diag_stage_id_t stage = DIAG_STAGE_BURST_REGISTER;
    uint8_t saved[4];
    uint8_t expected[4];
    uint8_t actual[4];
    uint8_t restored[4];
    size_t mismatch = sizeof(actual);
    size_t index;

    memcpy(expected, burst_pattern, sizeof(expected));
    read_buffer(context, stage, DIAG_PHASE_SAVE, GAR, saved, sizeof(saved));
    write_buffer(context, stage, DIAG_PHASE_WRITE_TX, GAR, expected,
                 sizeof(expected));
    read_buffer(context, stage, DIAG_PHASE_READ_TX, GAR, actual,
                sizeof(actual));
    for (index = 0u; index < sizeof(actual); ++index) {
        if (mismatch == sizeof(actual) && actual[index] != expected[index]) {
            mismatch = index;
        }
    }

    write_buffer(context, stage, DIAG_PHASE_RESTORE, GAR, saved, sizeof(saved));
    read_buffer(context, stage, DIAG_PHASE_RESTORE, GAR, restored,
                sizeof(restored));
    if (memcmp(restored, saved, sizeof(saved)) != 0) {
        recover_unverified_restoration(context, stage);
        diag_stage_set_details(context, "code=restore");
        return DIAG_STAGE_FAIL;
    }
    if (mismatch != sizeof(actual)) {
        diag_stage_set_details(
            context, "code=readback offset=%u expected=%02X actual=%02X",
            (unsigned int)mismatch, (unsigned int)expected[mismatch],
            (unsigned int)actual[mismatch]);
        return DIAG_STAGE_FAIL;
    }
    return DIAG_STAGE_PASS;
}

static diag_stage_result_t run_pointer_stage(diag_stage_context_t *context,
                                             diag_stage_id_t stage,
                                             diag_pointer_mode_t mode)
{
    uint8_t socket_status = read_byte(context, stage, DIAG_PHASE_SAVE,
                                      Sn_SR(DIAG_SOCKET));
    uint16_t saved_tx;
    uint16_t saved_rx;
    uint16_t actual_tx;
    uint16_t actual_rx;
    uint16_t restored_tx;
    uint16_t restored_rx;

    if (socket_status != SOCK_CLOSED) {
        diag_stage_set_details(
            context, "code=socket-state expected=%02X actual=%02X",
            (unsigned int)SOCK_CLOSED, (unsigned int)socket_status);
        return DIAG_STAGE_FAIL;
    }

    saved_tx = read_pointer(context, stage, DIAG_PHASE_SAVE, mode, true);
    saved_rx = read_pointer(context, stage, DIAG_PHASE_SAVE, mode, false);

    write_pointer(context, stage, DIAG_PHASE_WRITE_TX, mode, true,
                  DIAG_TX_WR_PATTERN);
    actual_tx = read_pointer(context, stage, DIAG_PHASE_READ_TX, mode, true);
    write_pointer(context, stage, DIAG_PHASE_WRITE_RX, mode, false,
                  DIAG_RX_RD_PATTERN);
    actual_rx = read_pointer(context, stage, DIAG_PHASE_READ_RX, mode, false);

    write_pointer(context, stage, DIAG_PHASE_RESTORE, mode, true, saved_tx);
    write_pointer(context, stage, DIAG_PHASE_RESTORE, mode, false, saved_rx);
    restored_tx = read_pointer_direct(context, stage, DIAG_PHASE_RESTORE,
                                      Sn_TX_WR(DIAG_SOCKET));
    restored_rx = read_pointer_direct(context, stage, DIAG_PHASE_RESTORE,
                                      Sn_RX_RD(DIAG_SOCKET));

    if (restored_tx != saved_tx || restored_rx != saved_rx) {
        recover_unverified_restoration(context, stage);
        diag_stage_set_details(context, "code=restore");
        return DIAG_STAGE_FAIL;
    }
    if (actual_tx != DIAG_TX_WR_PATTERN) {
        diag_stage_set_details(
            context, "code=readback pointer=tx expected=%04X actual=%04X",
            (unsigned int)DIAG_TX_WR_PATTERN, (unsigned int)actual_tx);
        return DIAG_STAGE_FAIL;
    }
    if (actual_rx != DIAG_RX_RD_PATTERN) {
        diag_stage_set_details(
            context, "code=readback pointer=rx expected=%04X actual=%04X",
            (unsigned int)DIAG_RX_RD_PATTERN, (unsigned int)actual_rx);
        return DIAG_STAGE_FAIL;
    }
    return DIAG_STAGE_PASS;
}

diag_stage_result_t diag_stage_pointer_sequential(diag_stage_context_t *context)
{
    return run_pointer_stage(context, DIAG_STAGE_POINTER_SEQUENTIAL,
                             DIAG_POINTER_SEQUENTIAL);
}

diag_stage_result_t diag_stage_pointer_burst(diag_stage_context_t *context)
{
    return run_pointer_stage(context, DIAG_STAGE_POINTER_BURST,
                             DIAG_POINTER_BURST);
}

diag_stage_result_t diag_stage_pointer_api(diag_stage_context_t *context)
{
    return run_pointer_stage(context, DIAG_STAGE_POINTER_API,
                             DIAG_POINTER_API);
}
