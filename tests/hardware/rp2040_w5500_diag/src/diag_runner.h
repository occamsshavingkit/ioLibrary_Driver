#ifndef W5500_DIAG_RUNNER_H
#define W5500_DIAG_RUNNER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DIAG_STAGE_CALLBACK_LAYOUT = 0,
    DIAG_STAGE_TRANSPORT_INIT = 1,
    DIAG_STAGE_CHIP_RESET = 2,
    DIAG_STAGE_VERSION = 3,
    DIAG_STAGE_MEMORY_INIT = 4,
    DIAG_STAGE_PHY_LINK = 5,
    DIAG_STAGE_SINGLE_REGISTER = 6,
    DIAG_STAGE_BURST_REGISTER = 7,
    DIAG_STAGE_POINTER_SEQUENTIAL = 8,
    DIAG_STAGE_POINTER_BURST = 9,
    DIAG_STAGE_POINTER_API = 10,
    DIAG_STAGE_SOCKET_OPEN = 11,
    DIAG_STAGE_UDP = 12,
    DIAG_STAGE_DHCP = 13,
    DIAG_STAGE_COUNT = 14
} diag_stage_id_t;

typedef enum {
    DIAG_STAGE_NOT_RUN = 0,
    DIAG_STAGE_PASS,
    DIAG_STAGE_FAIL,
    DIAG_STAGE_TIMEOUT
} diag_stage_result_t;

typedef struct {
    diag_stage_id_t id;
    const char *name;
    uint32_t required_mask;
    uint32_t timeout_ms;
    bool requires_network_config;
} diag_stage_descriptor_t;

typedef struct {
    uint32_t passed_mask;
    uint32_t sequence;
    bool recovery_mode;
} diag_runner_t;

void diag_runner_init(diag_runner_t *runner);
const diag_stage_descriptor_t *diag_stage_descriptor(diag_stage_id_t id);
const diag_stage_descriptor_t *diag_stage_by_name(const char *name);
bool diag_runner_can_run(const diag_runner_t *runner, diag_stage_id_t id,
                         bool network_configured);
uint32_t diag_runner_begin(diag_runner_t *runner, diag_stage_id_t id);
void diag_runner_finish(diag_runner_t *runner, diag_stage_id_t id,
                        diag_stage_result_t result);
void diag_runner_prepare_repeat(diag_runner_t *runner, diag_stage_id_t id);

#endif
