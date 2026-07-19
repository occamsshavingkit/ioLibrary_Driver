#include "diag_w5500_stages.h"

#include "diag_usb.h"
#include "diag_watchdog.h"
#include "w5500_diag_board.h"

#include "pico/time.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#define DIAG_DRIVER_PHASE 1u

static uint32_t stage_timeout(diag_stage_id_t stage)
{
    const diag_stage_descriptor_t *descriptor = diag_stage_descriptor(stage);

    return descriptor == NULL ? 0u : descriptor->timeout_ms;
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
