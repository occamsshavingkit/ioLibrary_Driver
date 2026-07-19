#include "diag_w5500_stages.h"

#include "diag_usb.h"
#include "diag_watchdog.h"
#include "w5500_diag_board.h"
#include "diag_net.h"
#include "diag_protocol.h"
#include "pico/unique_id.h"
#include "pico/time.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "dhcp.h"
#include "socket.h"
#pragma GCC diagnostic pop

static int8_t connect_IO_6(uint8_t sn, uint8_t *addr, uint16_t port,
                           uint8_t addrlen) __attribute__((unused));
static int32_t sendto_IO_6(uint8_t sn, uint8_t *buf, uint16_t len,
                           uint8_t *addr, uint16_t port,
                           uint8_t addrlen) __attribute__((unused));
static int32_t recvfrom_IO_6(uint8_t sn, uint8_t *buf, uint16_t len,
                             uint8_t *addr, uint16_t *port,
                             uint8_t *addrlen) __attribute__((unused));

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define DIAG_DRIVER_PHASE 1u
#define DIAG_SOCKET 0u
#define DIAG_TX_WR_PATTERN 0x1234u
#define DIAG_RX_RD_PATTERN 0x5678u
#define DIAG_NETWORK_WATCHDOG_MS 5000u

#if DIAG_EXPECT_SPI_STATUS
static uint8_t diag_spi_error(void)
{
    return wizchip_get_spi_error();
}

static void diag_clear_spi_error(void)
{
    wizchip_clear_spi_error();
}
#else
static uint8_t diag_spi_error(void)
{
    return 0u;
}

static void diag_clear_spi_error(void)
{
}
#endif

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

static void take_failure_snapshot(diag_stage_context_t *context, diag_stage_id_t stage, const char *code)
{
    uint8_t spi_err = diag_spi_error();
    uint32_t timeout = stage == DIAG_STAGE_DHCP
                           ? DIAG_NETWORK_WATCHDOG_MS
                           : stage_timeout(stage);

    diag_watchdog_begin(stage, DIAG_PHASE_SNAPSHOT_SR, context->sequence, timeout);
    uint8_t sr = getSn_SR(0);
    diag_watchdog_complete();

    diag_watchdog_begin(stage, DIAG_PHASE_SNAPSHOT_IR, context->sequence, timeout);
    uint8_t ir = getSn_IR(0);
    diag_watchdog_complete();

    diag_watchdog_begin(stage, DIAG_PHASE_SNAPSHOT_TX_WR, context->sequence, timeout);
    uint16_t tw = wizchip_read16_5500(Sn_TX_WR(0));
    diag_watchdog_complete();

    diag_watchdog_begin(stage, DIAG_PHASE_SNAPSHOT_RX_RD, context->sequence, timeout);
    uint16_t rr = wizchip_read16_5500(Sn_RX_RD(0));
    diag_watchdog_complete();

    diag_watchdog_begin(stage, DIAG_PHASE_SNAPSHOT_TX_FSR, context->sequence, timeout);
    uint16_t tf = wizchip_read16_5500(Sn_TX_FSR(0));
    diag_watchdog_complete();

    diag_watchdog_begin(stage, DIAG_PHASE_SNAPSHOT_RX_RSR, context->sequence, timeout);
    uint16_t rs = wizchip_read16_5500(Sn_RX_RSR(0));
    diag_watchdog_complete();

    if (diag_spi_error()) {
        spi_err = 1;
    }

    diag_stage_set_details(context, "code=%s sr=%02x ir=%02x tw=%04x rr=%04x tf=%u rs=%u spi=%u",
                           code, sr, ir, tw, rr, tf, rs, spi_err);
}

diag_stage_result_t diag_stage_socket_open(diag_stage_context_t *context)
{
    if (!context->network_configured) {
        take_failure_snapshot(context, DIAG_STAGE_SOCKET_OPEN,
                              "netinfo-unconfigured");
        return DIAG_STAGE_FAIL;
    }

    diag_clear_spi_error();

    diag_watchdog_begin(DIAG_STAGE_SOCKET_OPEN, DIAG_PHASE_SET_NETINFO, context->sequence, stage_timeout(DIAG_STAGE_SOCKET_OPEN));
    int8_t net_res = ctlnetwork(CN_SET_NETINFO, &context->network);
    diag_watchdog_complete();
    if (net_res != 0) {
        take_failure_snapshot(context, DIAG_STAGE_SOCKET_OPEN, "set-netinfo");
        return DIAG_STAGE_FAIL;
    }

    diag_watchdog_begin(DIAG_STAGE_SOCKET_OPEN, DIAG_PHASE_SOCKET_OPEN, context->sequence, stage_timeout(DIAG_STAGE_SOCKET_OPEN));
    int8_t sock_res = socket(0, Sn_MR_UDP, 49001, 0);
    diag_watchdog_complete();
    if (sock_res != 0) {
        take_failure_snapshot(context, DIAG_STAGE_SOCKET_OPEN, "socket-open");
        return DIAG_STAGE_FAIL;
    }

    uint8_t mode = SOCK_IO_NONBLOCK;
    diag_watchdog_begin(DIAG_STAGE_SOCKET_OPEN, DIAG_PHASE_SET_IOMODE, context->sequence, stage_timeout(DIAG_STAGE_SOCKET_OPEN));
    int8_t io_res = ctlsocket(0, CS_SET_IOMODE, &mode);
    diag_watchdog_complete();
    if (io_res != SOCK_OK) {
        take_failure_snapshot(context, DIAG_STAGE_SOCKET_OPEN, "set-iomode");
        return DIAG_STAGE_FAIL;
    }

    diag_watchdog_begin(DIAG_STAGE_SOCKET_OPEN, DIAG_PHASE_SOCKET_STATUS, context->sequence, stage_timeout(DIAG_STAGE_SOCKET_OPEN));
    uint8_t sr = getSn_SR(0);
    diag_watchdog_complete();
    if (sr != SOCK_UDP) {
        take_failure_snapshot(context, DIAG_STAGE_SOCKET_OPEN, "socket-status");
        return DIAG_STAGE_FAIL;
    }

    diag_watchdog_begin(DIAG_STAGE_SOCKET_OPEN, DIAG_PHASE_TX_FSR, context->sequence, stage_timeout(DIAG_STAGE_SOCKET_OPEN));
    uint16_t tx_fsr = getSn_TX_FSR(0);
    diag_watchdog_complete();
    if (tx_fsr == 0) {
        take_failure_snapshot(context, DIAG_STAGE_SOCKET_OPEN, "tx-fsr");
        return DIAG_STAGE_FAIL;
    }

    diag_watchdog_begin(DIAG_STAGE_SOCKET_OPEN, DIAG_PHASE_RX_RSR, context->sequence, stage_timeout(DIAG_STAGE_SOCKET_OPEN));
    uint16_t rx_rsr = getSn_RX_RSR(0);
    diag_watchdog_complete();
    if (rx_rsr != 0) {
        take_failure_snapshot(context, DIAG_STAGE_SOCKET_OPEN, "rx-rsr");
        return DIAG_STAGE_FAIL;
    }

    if (diag_spi_error() != 0) {
        take_failure_snapshot(context, DIAG_STAGE_SOCKET_OPEN, "spi-latch");
        return DIAG_STAGE_FAIL;
    }

    return DIAG_STAGE_PASS;
}

diag_stage_result_t diag_stage_udp(diag_stage_context_t *context)
{
    diag_udp_packet_t packet;

    diag_clear_spi_error();
    diag_net_encode_udp(context->sequence, (uint8_t *)&packet);

    diag_watchdog_begin(DIAG_STAGE_UDP, DIAG_PHASE_TX_WR_BEFORE, context->sequence, stage_timeout(DIAG_STAGE_UDP));
    uint16_t tx_wr_before = wizchip_read16_5500(Sn_TX_WR(0));
    diag_watchdog_complete();
    if (diag_spi_error() != 0) {
        take_failure_snapshot(context, DIAG_STAGE_UDP, "pointer-read");
        return DIAG_STAGE_FAIL;
    }

    diag_watchdog_begin(DIAG_STAGE_UDP, DIAG_PHASE_SEND_IR_CLEAR, context->sequence, stage_timeout(DIAG_STAGE_UDP));
    setSn_IR(0, (Sn_IR_SENDOK | Sn_IR_TIMEOUT));
    diag_watchdog_complete();
    if (diag_spi_error() != 0) {
        take_failure_snapshot(context, DIAG_STAGE_UDP, "send-ir-clear");
        return DIAG_STAGE_FAIL;
    }

    absolute_time_t send_deadline = make_timeout_time_ms(5000);
    bool send_done = false;
    while (!time_reached(send_deadline)) {
        diag_usb_task();
        diag_watchdog_begin(DIAG_STAGE_UDP, DIAG_PHASE_SENDTO, context->sequence, stage_timeout(DIAG_STAGE_UDP));
        int32_t send_res = sendto(0, (uint8_t *)&packet, sizeof(packet),
                                  context->host_ip, context->host_port);
        diag_watchdog_complete();

        if (send_res == 32) {
            send_done = true;
            break;
        } else if (send_res == SOCK_BUSY) {
            sleep_ms(1);
            continue;
        } else {
            take_failure_snapshot(context, DIAG_STAGE_UDP, "send");
            return DIAG_STAGE_FAIL;
        }
    }

    if (!send_done) {
        take_failure_snapshot(context, DIAG_STAGE_UDP, "send-deadline");
        return DIAG_STAGE_FAIL;
    }

    bool poll_done = false;
    while (!time_reached(send_deadline)) {
        diag_usb_task();
        diag_watchdog_begin(DIAG_STAGE_UDP, DIAG_PHASE_SEND_IR_READ, context->sequence, stage_timeout(DIAG_STAGE_UDP));
        uint8_t ir = getSn_IR(0);
        diag_watchdog_complete();

        if (diag_spi_error() != 0) {
            take_failure_snapshot(context, DIAG_STAGE_UDP, "send");
            return DIAG_STAGE_FAIL;
        }

        if (ir & Sn_IR_SENDOK) {
            diag_watchdog_begin(DIAG_STAGE_UDP, DIAG_PHASE_SEND_IR_CLEAR, context->sequence, stage_timeout(DIAG_STAGE_UDP));
            setSn_IR(0, (Sn_IR_SENDOK));
            diag_watchdog_complete();
            poll_done = true;
            break;
        }

        if (ir & Sn_IR_TIMEOUT) {
            take_failure_snapshot(context, DIAG_STAGE_UDP, "send-timeout");
            return DIAG_STAGE_FAIL;
        }

        diag_watchdog_begin(DIAG_STAGE_UDP, DIAG_PHASE_SOCKET_STATUS, context->sequence, stage_timeout(DIAG_STAGE_UDP));
        uint8_t sr = getSn_SR(0);
        diag_watchdog_complete();
        if (sr != SOCK_UDP) {
            take_failure_snapshot(context, DIAG_STAGE_UDP, "send");
            return DIAG_STAGE_FAIL;
        }

        sleep_ms(1);
    }

    if (!poll_done) {
        take_failure_snapshot(context, DIAG_STAGE_UDP, "send-deadline");
        return DIAG_STAGE_FAIL;
    }

    diag_watchdog_begin(DIAG_STAGE_UDP, DIAG_PHASE_TX_WR_AFTER, context->sequence, stage_timeout(DIAG_STAGE_UDP));
    uint16_t tx_wr_after = wizchip_read16_5500(Sn_TX_WR(0));
    diag_watchdog_complete();

    diag_watchdog_begin(DIAG_STAGE_UDP, DIAG_PHASE_RX_RD_BEFORE, context->sequence, stage_timeout(DIAG_STAGE_UDP));
    uint16_t rx_rd_before = wizchip_read16_5500(Sn_RX_RD(0));
    diag_watchdog_complete();
    if (diag_spi_error() != 0) {
        take_failure_snapshot(context, DIAG_STAGE_UDP, "pointer-read");
        return DIAG_STAGE_FAIL;
    }

    absolute_time_t recv_deadline = make_timeout_time_ms(5000);
    int32_t recv_res = 0;
    bool recv_done = false;
    uint8_t recv_buffer[sizeof(diag_udp_packet_t)];
    uint8_t actual_ip[4] = {0};
    uint16_t actual_port = 0;

    while (!time_reached(recv_deadline)) {
        uint16_t available;

        diag_usb_task();
        diag_watchdog_begin(DIAG_STAGE_UDP, DIAG_PHASE_RX_RSR_POLL,
                            context->sequence, stage_timeout(DIAG_STAGE_UDP));
        available = getSn_RX_RSR(0);
        diag_watchdog_complete();
        (void)available;
        if (diag_spi_error() != 0) {
            take_failure_snapshot(context, DIAG_STAGE_UDP, "recv");
            return DIAG_STAGE_FAIL;
        }
        diag_watchdog_begin(DIAG_STAGE_UDP, DIAG_PHASE_RECVFROM, context->sequence, stage_timeout(DIAG_STAGE_UDP));
        recv_res = recvfrom(0, recv_buffer, sizeof(recv_buffer), actual_ip, &actual_port);
        diag_watchdog_complete();

        if (recv_res > 0) {
            recv_done = true;
            break;
        } else if (recv_res == 0) {
            sleep_ms(1);
            continue;
        } else {
            take_failure_snapshot(context, DIAG_STAGE_UDP, "recv");
            return DIAG_STAGE_FAIL;
        }
    }

    if (!recv_done) {
        take_failure_snapshot(context, DIAG_STAGE_UDP, "recv-deadline");
        return DIAG_STAGE_FAIL;
    }

    if (actual_port != context->host_port || memcmp(actual_ip, context->host_ip, 4) != 0) {
        take_failure_snapshot(context, DIAG_STAGE_UDP, "source");
        return DIAG_STAGE_FAIL;
    }

    if (!diag_net_validate_udp_packet(recv_buffer, recv_res, context->sequence, context->host_ip, context->host_port, actual_ip, actual_port)) {
        take_failure_snapshot(context, DIAG_STAGE_UDP, "packet");
        return DIAG_STAGE_FAIL;
    }

    diag_watchdog_begin(DIAG_STAGE_UDP, DIAG_PHASE_RX_RD_AFTER, context->sequence, stage_timeout(DIAG_STAGE_UDP));
    uint16_t rx_rd_after = wizchip_read16_5500(Sn_RX_RD(0));
    diag_watchdog_complete();
    if (diag_spi_error() != 0) {
        take_failure_snapshot(context, DIAG_STAGE_UDP, "pointer-read");
        return DIAG_STAGE_FAIL;
    }

    uint16_t tx_delta = diag_net_ptr_delta(tx_wr_before, tx_wr_after);
    uint16_t rx_delta = diag_net_ptr_delta(rx_rd_before, rx_rd_after);

    if (tx_delta != 32) {
        take_failure_snapshot(context, DIAG_STAGE_UDP, "tx-delta");
        return DIAG_STAGE_FAIL;
    }
    if (rx_delta != 40) {
        take_failure_snapshot(context, DIAG_STAGE_UDP, "rx-delta");
        return DIAG_STAGE_FAIL;
    }

    diag_stage_set_details(context, "tw=%04x->%04x rr=%04x->%04x", tx_wr_before, tx_wr_after, rx_rd_before, rx_rd_after);
    return DIAG_STAGE_PASS;
}

static bool emit_stage_event(uint32_t sequence, const char *stage, const char *event, const char *details)
{
    char line[DIAG_LINE_MAX + 1u];
    if (diag_format_event(line, sizeof(line), sequence, stage, event, details) < 0) {
        return false;
    }
    return diag_usb_write_line(line);
}

static bool dhcp_timer_callback(struct repeating_timer *t)
{
    (void)t;
    DHCP_time_handler();
    return true;
}

diag_stage_result_t diag_stage_dhcp(diag_stage_context_t *context)
{
    diag_stage_result_t final_result = DIAG_STAGE_FAIL;
    bool timer_started = false;
    bool timer_cancelled = true;
    struct repeating_timer timer;

    context->runner->passed_mask &= ~((1u << DIAG_STAGE_SOCKET_OPEN) |
                                      (1u << DIAG_STAGE_UDP) |
                                      (1u << DIAG_STAGE_DHCP));

    diag_clear_spi_error();
    diag_watchdog_begin(DIAG_STAGE_DHCP, DIAG_PHASE_CHIP_RESET,
                        context->sequence, DIAG_NETWORK_WATCHDOG_MS);
    w5500_diag_board_reset();
    diag_watchdog_complete();

    uint8_t tx_size[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    uint8_t rx_size[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    diag_watchdog_begin(DIAG_STAGE_DHCP, DIAG_PHASE_MEMORY_INIT,
                        context->sequence, DIAG_NETWORK_WATCHDOG_MS);
    int8_t mem_res = wizchip_init(tx_size, rx_size);
    diag_watchdog_complete();
    if (mem_res != 0 || diag_spi_error() != 0) {
        take_failure_snapshot(context, DIAG_STAGE_DHCP, "memory-init");
        goto cleanup_no_stop;
    }

    absolute_time_t phy_deadline = make_timeout_time_ms(5000);
    bool phy_up = false;
    while (!time_reached(phy_deadline)) {
        diag_usb_task();
        diag_watchdog_begin(DIAG_STAGE_DHCP, DIAG_PHASE_PHY_LINK,
                            context->sequence, DIAG_NETWORK_WATCHDOG_MS);
        int8_t link = wizphy_getphylink();
        diag_watchdog_complete();
        if (link == PHY_LINK_ON) {
            phy_up = true;
            break;
        }
        if (diag_spi_error() != 0) {
            take_failure_snapshot(context, DIAG_STAGE_DHCP, "spi-latch");
            goto cleanup_no_stop;
        }
        sleep_ms(10);
    }
    if (!phy_up) {
        take_failure_snapshot(context, DIAG_STAGE_DHCP, "phy-failure");
        goto cleanup_no_stop;
    }

    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    wiz_NetInfo dhcp_netinfo = {0};
    diag_net_derive_mac(board_id.id, dhcp_netinfo.mac);
    dhcp_netinfo.dhcp = NETINFO_DHCP;

    diag_watchdog_begin(DIAG_STAGE_DHCP, DIAG_PHASE_SET_NETINFO,
                        context->sequence, DIAG_NETWORK_WATCHDOG_MS);
    int8_t net_res = ctlnetwork(CN_SET_NETINFO, &dhcp_netinfo);
    diag_watchdog_complete();
    if (net_res != 0 || diag_spi_error() != 0) {
        take_failure_snapshot(context, DIAG_STAGE_DHCP, "set-netinfo");
        goto cleanup_no_stop;
    }

    reg_dhcp_cbfunc(NULL, NULL, NULL);

    timer_started = add_repeating_timer_ms(-1000, dhcp_timer_callback, NULL, &timer);
    if (!timer_started) {
        take_failure_snapshot(context, DIAG_STAGE_DHCP, "timer-failure");
        goto cleanup_no_stop;
    }

    diag_watchdog_begin(DIAG_STAGE_DHCP, DIAG_PHASE_DHCP_INIT,
                        context->sequence, DIAG_NETWORK_WATCHDOG_MS);
    DHCP_init(0, context->dhcp_buffer);
    diag_watchdog_complete();

    absolute_time_t dhcp_deadline = make_timeout_time_ms(60000);
    uint8_t current_state = 0xFF;
    bool dhcp_success = false;

    while (!time_reached(dhcp_deadline)) {
        diag_usb_task();

        diag_watchdog_begin(DIAG_STAGE_DHCP, DIAG_PHASE_DHCP_RUN,
                            context->sequence, DIAG_NETWORK_WATCHDOG_MS);
        uint8_t new_state = DHCP_run();
        diag_watchdog_complete();

        if (diag_spi_error() != 0) {
            take_failure_snapshot(context, DIAG_STAGE_DHCP, "spi-latch");
            goto cleanup;
        }

        if (new_state != current_state) {
            current_state = new_state;
            char val_details[32];
            int written = snprintf(val_details, sizeof(val_details),
                                   "value=%s",
                                   diag_net_dhcp_state_name(current_state));
            if (written < 0 || (size_t)written >= sizeof(val_details) ||
                !emit_stage_event(context->sequence, "dhcp", "STATE",
                                  val_details)) {
                take_failure_snapshot(context, DIAG_STAGE_DHCP,
                                      "state-event");
                goto cleanup;
            }
        }

        if (new_state == DHCP_FAILED) {
            take_failure_snapshot(context, DIAG_STAGE_DHCP, "dhcp-failed");
            goto cleanup;
        }

        diag_watchdog_begin(DIAG_STAGE_DHCP, DIAG_PHASE_PHY_LINK,
                            context->sequence, DIAG_NETWORK_WATCHDOG_MS);
        int8_t link = wizphy_getphylink();
        diag_watchdog_complete();
        if (link != PHY_LINK_ON) {
            take_failure_snapshot(context, DIAG_STAGE_DHCP, "phy-failure");
            goto cleanup;
        }

        if (new_state == DHCP_IP_ASSIGN || new_state == DHCP_IP_CHANGED || new_state == DHCP_IP_LEASED) {
            uint8_t lease_ip[4] = {0};
            uint8_t lease_gw[4] = {0};
            uint8_t lease_sn[4] = {0};
            uint8_t lease_dns[4] = {0};
            getIPfromDHCP(lease_ip);
            getGWfromDHCP(lease_gw);
            getSNfromDHCP(lease_sn);
            getDNSfromDHCP(lease_dns);

            if (diag_net_dhcp_lease_complete(lease_ip, lease_gw, lease_sn, lease_dns)) {
                wiz_NetInfo dhcp_applied = {0};
                memcpy(dhcp_applied.ip, lease_ip, 4);
                memcpy(dhcp_applied.gw, lease_gw, 4);
                memcpy(dhcp_applied.sn, lease_sn, 4);
                memcpy(dhcp_applied.dns, lease_dns, 4);
                memcpy(dhcp_applied.mac, dhcp_netinfo.mac, 6);
                dhcp_applied.dhcp = NETINFO_DHCP;

                diag_watchdog_begin(DIAG_STAGE_DHCP, DIAG_PHASE_LEASE_APPLY,
                                    context->sequence,
                                    DIAG_NETWORK_WATCHDOG_MS);
                int8_t apply_res = ctlnetwork(CN_SET_NETINFO, &dhcp_applied);
                diag_watchdog_complete();
                if (apply_res != 0 || diag_spi_error() != 0) {
                    take_failure_snapshot(context, DIAG_STAGE_DHCP, "lease-apply");
                    goto cleanup;
                }

                context->network = dhcp_applied;
                diag_stage_set_details(
                    context, "ip=%u.%u.%u.%u gw=%u.%u.%u.%u",
                    lease_ip[0], lease_ip[1], lease_ip[2], lease_ip[3],
                    lease_gw[0], lease_gw[1], lease_gw[2], lease_gw[3]);
                dhcp_success = true;
                final_result = DIAG_STAGE_PASS;
                break;
            } else {
                take_failure_snapshot(context, DIAG_STAGE_DHCP, "incomplete-lease");
                goto cleanup;
            }
        }

        sleep_ms(10);
    }

    if (!dhcp_success && final_result != DIAG_STAGE_PASS) {
        take_failure_snapshot(context, DIAG_STAGE_DHCP, "deadline");
    }

cleanup:
    if (timer_started) {
        timer_cancelled = cancel_repeating_timer(&timer);
        timer_started = false;
        if (!timer_cancelled) {
            take_failure_snapshot(context, DIAG_STAGE_DHCP, "timer-failure");
            final_result = DIAG_STAGE_FAIL;
        }
    }
    diag_watchdog_begin(DIAG_STAGE_DHCP, DIAG_PHASE_DHCP_STOP,
                        context->sequence, DIAG_NETWORK_WATCHDOG_MS);
    DHCP_stop();
    diag_watchdog_complete();
    if (diag_spi_error() != 0) {
        take_failure_snapshot(context, DIAG_STAGE_DHCP, "spi-latch");
        final_result = DIAG_STAGE_FAIL;
    }

cleanup_no_stop:
    if (timer_started) {
        if (!cancel_repeating_timer(&timer)) {
            take_failure_snapshot(context, DIAG_STAGE_DHCP, "timer-failure");
        }
    }

    return final_result;
}

/* GCC 16 diagnoses socket.h's never-defined statics at TU finalization. */
#pragma GCC diagnostic ignored "-Wunused-function"
