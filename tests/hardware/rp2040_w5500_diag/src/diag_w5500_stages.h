#ifndef W5500_DIAG_STAGES_H
#define W5500_DIAG_STAGES_H

#include "diag_runner.h"
#include "wizchip_conf.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct diag_stage_context {
    diag_runner_t *runner;
    uint32_t sequence;
    bool network_configured;
    wiz_NetInfo network;
    uint8_t host_ip[4];
    uint16_t host_port;
    uint8_t dhcp_buffer[2048];
    char details[96];
} diag_stage_context_t;

void diag_stage_set_details(diag_stage_context_t *context,
                            const char *format, ...);

diag_stage_result_t diag_stage_callback_layout(diag_stage_context_t *context);
diag_stage_result_t diag_stage_transport_init(diag_stage_context_t *context);
diag_stage_result_t diag_stage_chip_reset(diag_stage_context_t *context);
diag_stage_result_t diag_stage_version(diag_stage_context_t *context);
diag_stage_result_t diag_stage_memory_init(diag_stage_context_t *context);
diag_stage_result_t diag_stage_phy_link(diag_stage_context_t *context);
diag_stage_result_t diag_stage_single_register(diag_stage_context_t *context);
diag_stage_result_t diag_stage_burst_register(diag_stage_context_t *context);
diag_stage_result_t diag_stage_pointer_sequential(diag_stage_context_t *context);
diag_stage_result_t diag_stage_pointer_burst(diag_stage_context_t *context);
diag_stage_result_t diag_stage_pointer_api(diag_stage_context_t *context);

#endif
